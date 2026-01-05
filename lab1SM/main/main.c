/*
 * BLE GATT Client (ESP32-S3) + Wokwi-friendly SIM mode
 *
 * Реальный режим (по умолчанию): работает как твой исходный код на настоящей ESP32-S3.
 * Режим Wokwi: добавь флаг компиляции -DWOKWI_SIM (или #define WOKWI_SIM 1 ниже),
 *              и код будет ИМИТИРОВАТЬ сканирование/подключение/логи в UART.
 *
 * Важно: в Wokwi это не настоящий BLE, а симуляция вывода и состояний.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

// --- включи строку ниже, если не хочешь настраивать флаги сборки ---
// #define WOKWI_SIM 1

static const char *TAG = "BLE_CLIENT";

#define TARGET_NAME      "Amazfit Balance"
#define SCAN_DURATION_S  5                // скан 5 c

// флаги состояния
static bool is_connecting = false;
static bool is_connected  = false;

// ------------------------------------------------------------------
// Wokwi SIM mode: без настоящего BT стека, имитируем логи.
// ------------------------------------------------------------------
#ifdef WOKWI_SIM

#include <stdlib.h>
#include <stdint.h>

typedef uint8_t  esp_bd_addr_t[6];
typedef uint16_t esp_gatt_if_t;
typedef int      esp_err_t;
typedef uint8_t  esp_ble_addr_type_t;

#define ESP_OK 0
#define ESP_GATT_IF_NONE 0xFFFF
#define BLE_ADDR_TYPE_PUBLIC 0

static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "ESP_OK"; }

// заглушки (не используются по назначению в SIM, но пусть будут)
static inline esp_err_t esp_ble_gap_start_scanning(uint32_t duration) { (void)duration; return ESP_OK; }
static inline esp_err_t esp_ble_gap_stop_scanning(void) { return ESP_OK; }
static inline esp_err_t esp_ble_gap_read_rssi(const esp_bd_addr_t bda) { (void)bda; return ESP_OK; }

#else
// ------------------------------------------------------------------
// Real BLE mode: настоящие заголовки ESP-IDF BLE.
// ------------------------------------------------------------------
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#endif

// gatt client интерфейс и соединение
static esp_gatt_if_t       g_gattc_if         = ESP_GATT_IF_NONE;
static uint16_t            g_conn_id          = 0;
static esp_bd_addr_t       g_remote_bda       = {0};
static esp_ble_addr_type_t g_remote_addr_type = BLE_ADDR_TYPE_PUBLIC;
static char                g_remote_name[32]  = {0};   // запоминаем имя устройства

#ifndef WOKWI_SIM
// параметры сканирования (только для реального режима)
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,      // АКТИВНОЕ сканирование
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE,
};
#endif

// вспомогательная функция: печать адреса
static void print_bda(const esp_bd_addr_t bda)
{
    ESP_LOGI(TAG, "BDA: %02X:%02X:%02X:%02X:%02X:%02X",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

#ifndef WOKWI_SIM
// запуск сканирования
static void start_scan(void)
{
    if (is_connected || is_connecting) {
        ESP_LOGI(TAG, "Already connected/connecting, skip scan");
        return;
    }

    esp_err_t err = esp_ble_gap_start_scanning(SCAN_DURATION_S);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_start_scanning failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Start scanning for %d seconds", SCAN_DURATION_S);
    }
}

// получить имя устройства из advertising / scan response данных
static bool get_name_from_adv(const uint8_t *adv_data, uint8_t adv_data_len,
                              char *name_buf, size_t name_buf_len)
{
    (void)adv_data_len;  // esp_ble_resolve_adv_data сам разбирает буфер

    uint8_t len = 0;

    const uint8_t *name = esp_ble_resolve_adv_data((uint8_t *)adv_data,
                                                   ESP_BLE_AD_TYPE_NAME_CMPL,
                                                   &len);
    if (len == 0 || name == NULL) {
        name = esp_ble_resolve_adv_data((uint8_t *)adv_data,
                                        ESP_BLE_AD_TYPE_NAME_SHORT,
                                        &len);
    }

    if (name && len > 0) {
        if (len >= name_buf_len) {
            len = name_buf_len - 1;
        }
        memcpy(name_buf, name, len);
        name_buf[len] = '\0';
        return true;
    }

    return false;
}

// прототипы callback
static void gattc_cb(esp_gattc_cb_event_t event,
                     esp_gatt_if_t gattc_if,
                     esp_ble_gattc_cb_param_t *param);

static void gap_cb(esp_gap_ble_cb_event_t event,
                   esp_ble_gap_cb_param_t *param);

// gap callback
static void gap_cb(esp_gap_ble_cb_event_t event,
                   esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan params set complete (status=%d)",
                 param->scan_param_cmpl.status);
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            start_scan();
        } else {
            ESP_LOGE(TAG, "Failed to set scan params, status=%d",
                     param->scan_param_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Scan start failed, status=%d",
                     param->scan_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "Scan started");
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan stopped, status=%d",
                 param->scan_stop_cmpl.status);
        // если не подключаемся и не подключены — заново сканируем
        if (!is_connecting && !is_connected) {
            start_scan();
        }
        break;

    case ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT:
        if (param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "RSSI of connected device: %d dBm",
                     param->read_rssi_cmpl.rssi);
        } else {
            ESP_LOGE(TAG, "Read RSSI failed, status=%d",
                     param->read_rssi_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan = param;

        switch (scan->scan_rst.search_evt) {

        case ESP_GAP_SEARCH_INQ_RES_EVT: {
            char dev_name[32] = {0};
            bool name_found   = false;

            // --- имя из advertising части ---
            if (scan->scan_rst.adv_data_len > 0) {
                if (get_name_from_adv(scan->scan_rst.ble_adv,
                                      scan->scan_rst.adv_data_len,
                                      dev_name, sizeof(dev_name))) {
                    name_found = true;
                }
            }

            // --- если в adv нет, пробуем scan response ---
            if (!name_found && scan->scan_rst.scan_rsp_len > 0) {
                const uint8_t *scan_rsp_data =
                    scan->scan_rst.ble_adv + scan->scan_rst.adv_data_len;

                if (get_name_from_adv(scan_rsp_data,
                                      scan->scan_rst.scan_rsp_len,
                                      dev_name, sizeof(dev_name))) {
                    name_found = true;
                }
            }

            // если имени нет — просто скипаем это устройство
            if (!name_found) {
                return;
            }

            // логируем только устройства с именем
            ESP_LOGI(TAG,
                     "Found device: name=%s, RSSI=%d",
                     dev_name,
                     scan->scan_rst.rssi);

            // проверяем, наше ли это устройство
            if (!is_connecting && !is_connected &&
                (strcmp(dev_name, TARGET_NAME) == 0)) {

                ESP_LOGI(TAG, "Target device \"%s\" found, connecting...",
                         TARGET_NAME);

                memcpy(g_remote_bda, scan->scan_rst.bda, sizeof(esp_bd_addr_t));
                g_remote_addr_type = scan->scan_rst.ble_addr_type;

                // запоминаем имя, чтобы потом показать после подключения
                strncpy(g_remote_name, dev_name, sizeof(g_remote_name) - 1);
                g_remote_name[sizeof(g_remote_name) - 1] = '\0';

                is_connecting = true;

                // остановка сканирования
                esp_err_t err_stop = esp_ble_gap_stop_scanning();
                if (err_stop != ESP_OK) {
                    ESP_LOGW(TAG, "esp_ble_gap_stop_scanning failed: %s",
                             esp_err_to_name(err_stop));
                }

                // открытие gatt-соединения
                if (g_gattc_if != ESP_GATT_IF_NONE) {
                    esp_err_t err = esp_ble_gattc_open(
                                        g_gattc_if,
                                        g_remote_bda,
                                        g_remote_addr_type,
                                        true   // direct connection
                                    );
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ble_gattc_open failed: %s",
                                 esp_err_to_name(err));
                        is_connecting = false;
                        start_scan();
                    }
                } else {
                    ESP_LOGE(TAG, "g_gattc_if is NONE, can't connect");
                    is_connecting = false;
                    start_scan();
                }
            }
            break;
        }

        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            ESP_LOGI(TAG, "Inquiry complete");
            break;

        default:
            break;
        }
        break;
    }

    default:
        break;
    }
}

// gattc callback
static void gattc_cb(esp_gattc_cb_event_t event,
                     esp_gatt_if_t gattc_if,
                     esp_ble_gattc_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTC_REG_EVT:
        ESP_LOGI(TAG, "GATTC REG EVT, app_id=%d", param->reg.app_id);
        g_gattc_if = gattc_if;

        // установка параметров сканирования
        ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&ble_scan_params));
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK) {
            is_connected  = true;
            is_connecting = false;
            g_conn_id     = param->open.conn_id;

            ESP_LOGI(TAG, "Connected to device:");
            ESP_LOGI(TAG, "  Name: '%s'", g_remote_name[0] ? g_remote_name : "<unknown>");
            ESP_LOGI(TAG, "  Conn ID: %d", g_conn_id);
            ESP_LOGI(TAG, "  Addr type: %d", g_remote_addr_type);
            print_bda(g_remote_bda);

            // запросим RSSI уже подключенного устройства
            esp_err_t err = esp_ble_gap_read_rssi(g_remote_bda);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ble_gap_read_rssi failed: %s", esp_err_to_name(err));
            }

        } else {
            ESP_LOGE(TAG, "Connection failed, status=%d", param->open.status);
            is_connected  = false;
            is_connecting = false;
            start_scan();
        }
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGW(TAG, "Disconnected, reason=0x%X", param->disconnect.reason);
        is_connected  = false;
        is_connecting = false;
        // после дисконнекта стартуем сканирование
        start_scan();
        break;

    default:
        break;
    }
}
#endif // !WOKWI_SIM


// ------------------------------------------------------------------
// Wokwi SIM task: имитируем поведение и ЛОГИ (почти как на железе).
// ------------------------------------------------------------------
#ifdef WOKWI_SIM
static void wokwi_sim_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "BLE GATT client started, waiting for events...");

    while (1) {
        if (!is_connected && !is_connecting) {
            // "как будто" выставили параметры скана
            ESP_LOGI(TAG, "Scan params set complete (status=%d)", 0);

            ESP_LOGI(TAG, "Start scanning for %d seconds", SCAN_DURATION_S);
            ESP_LOGI(TAG, "Scan started");

            // имитируем найденные устройства
            vTaskDelay(pdMS_TO_TICKS(650));
            ESP_LOGI(TAG, "Found device: name=%s, RSSI=%d", "Random BLE Tag", -78);

            vTaskDelay(pdMS_TO_TICKS(500));
            ESP_LOGI(TAG, "Found device: name=%s, RSSI=%d", "ESP32_SENSOR", -61);

            vTaskDelay(pdMS_TO_TICKS(700));
            ESP_LOGI(TAG, "Found device: name=%s, RSSI=%d", TARGET_NAME, -49);

            // имитация "нашли цель и подключаемся"
            ESP_LOGI(TAG, "Target device \"%s\" found, connecting...", TARGET_NAME);

            // адрес "как будто" удалённого устройства
            esp_bd_addr_t fake = {0xA4, 0xC1, 0x38, 0x12, 0x34, 0x56};
            memcpy(g_remote_bda, fake, sizeof(esp_bd_addr_t));
            strncpy(g_remote_name, TARGET_NAME, sizeof(g_remote_name) - 1);
            g_remote_name[sizeof(g_remote_name) - 1] = '\0';
            g_remote_addr_type = BLE_ADDR_TYPE_PUBLIC;

            is_connecting = true;

            vTaskDelay(pdMS_TO_TICKS(250));
            ESP_LOGI(TAG, "Scan stopped, status=%d", 0);

            // "подключение"
            vTaskDelay(pdMS_TO_TICKS(700));
            is_connected = true;
            is_connecting = false;
            g_conn_id = 0;

            ESP_LOGI(TAG, "Connected to device:");
            ESP_LOGI(TAG, "  Name: '%s'", g_remote_name[0] ? g_remote_name : "<unknown>");
            ESP_LOGI(TAG, "  Conn ID: %d", g_conn_id);
            ESP_LOGI(TAG, "  Addr type: %d", (int)g_remote_addr_type);
            print_bda(g_remote_bda);

            // "RSSI"
            vTaskDelay(pdMS_TO_TICKS(300));
            ESP_LOGI(TAG, "RSSI of connected device: %d dBm", -49);

            // "Inquiry complete" (как будто завершили проход скана)
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGI(TAG, "Inquiry complete");
        }

        // периодически имитируем дисконнект, чтобы цикл повторялся
        vTaskDelay(pdMS_TO_TICKS(6000));
        if (is_connected) {
            ESP_LOGW(TAG, "Disconnected, reason=0x%X", 0x13);
            is_connected = false;
            is_connecting = false;
        }
    }
}
#endif


// ------------------------------------------------------------------
// Entry point
// ------------------------------------------------------------------
void app_main(void)
{
#ifdef WOKWI_SIM
    // В Wokwi: не инициализируем BT стек, просто имитируем поведение и логи
    xTaskCreate(wokwi_sim_task, "wokwi_sim_task", 4096, NULL, 5, NULL);
    return;
#else
    esp_err_t ret;

    // инициализация nvs
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // инициализация bt контроллера
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(ret));
        return;
    }

    // включаем только ble для esp32-s3
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // инициализация bluedroid
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "esp_bluedroid_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "esp_bluedroid_enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // регистрация callback gap и gattc
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gattc_cb));

    // регистрация gatt client приложения (app_id = 0)
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));

    ESP_LOGI(TAG, "BLE GATT client started, waiting for events...");
#endif
}
