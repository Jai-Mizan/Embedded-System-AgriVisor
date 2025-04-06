#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/freertos.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/adc.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_adc_cal.h"
#include "cJSON.h"
#include "mqtt_client.h"
#include "esp_tls.h"

// Configuration
#define TAG "ARDUINO_IOT"

// WiFi Configuration
#define WIFI_SSID "jaiiphone"
#define WIFI_PASS "jaihensem"

// MQTT Configuration for Arduino IoT Cloud
#define MQTT_BROKER "mqtts-sa.iot.arduino.cc"
#define MQTT_PORT 8883
#define DEVICE_ID "6930d669-af0f-4ca8-b372-0c53db4ef0d3"
#define SECRET_KEY "6aA6C?PDe?2m??6dp06pRYVgm"  // Removed leading space

// Arduino IoT Cloud Root CA Certificate (need to manually broke through AWS GATE)
static const char ARDUINO_IOT_ROOT_CA[] = 
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIglz5j5bZkd3B5hZ4Q5+\\n"
"-----END CERTIFICATE-----\n";

// Sensor Pins
#define SOIL_MOISTURE_PIN ADC1_CHANNEL_3  // GPIO34
#define DHT_PIN GPIO_NUM_5
#define NPK_UART_NUM UART_NUM_2
#define NPK_TX_PIN GPIO_NUM_7
#define NPK_RX_PIN GPIO_NUM_6

// Calibration values
#define DRY_VALUE 3000
#define WET_VALUE 500
#define SENSOR_READ_INTERVAL_MS 10000

// Global variables
static esp_mqtt_client_handle_t client;
static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

// Sensor data structure
typedef struct {
    float humidity;
    int soil_moisture;
    int nitrogen;
    int phosphorus;
    int potassium;
    float temperature;  // Added temperature
} sensor_data_t;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                             int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

// Initialize WiFi
static void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to Arduino IoT Cloud");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT Published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error: %s", event->error_handle->esp_tls_last_esp_err);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT Received: %.*s", event->data_len, event->data);
            break;
        default:
            ESP_LOGI(TAG, "MQTT Other event id:%d", event->event_id);
            break;
    }
}

// Initialize MQTT client
static void mqtt_app_start() {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = "mqtts://" MQTT_BROKER,
                .port = MQTT_PORT,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = {
                .certificate = ARDUINO_IOT_ROOT_CA,
            },
        },
        .credentials = {
            .username = DEVICE_ID,
            .client_id = DEVICE_ID,
            .authentication = {
                .password = SECRET_KEY,
            },
        },
        .session = {
            .keepalive = 60,
            .disable_clean_session = false,
        },
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
}

// Initialize NPK sensor UART
static void npk_uart_init() {
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(NPK_UART_NUM, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(NPK_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(NPK_UART_NUM, NPK_TX_PIN, NPK_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// Read NPK values from sensor
static int read_npk_value(uint8_t *command, size_t cmd_len) {
    uint8_t data[32] = {0};
    uart_write_bytes(NPK_UART_NUM, (const char*)command, cmd_len);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    int len = uart_read_bytes(NPK_UART_NUM, data, sizeof(data), 20 / portTICK_PERIOD_MS);
    
    if (len > 0 && data[0] == 0x01) {
        return (data[3] << 8) | data[4]; // Combine two bytes into a value
    }
    ESP_LOGE(TAG, "Failed to read NPK value");
    return -1;
}

// Read soil moisture
static int read_soil_moisture() {
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    
    int raw = adc1_get_raw(ADC1_CHANNEL_6);
    int percentage = 100 - ((raw - WET_VALUE) * 100 / (DRY_VALUE - WET_VALUE));
    
    percentage = (percentage < 0) ? 0 : (percentage > 100) ? 100 : percentage;
    ESP_LOGI(TAG, "Soil Moisture - Raw: %d, Percentage: %d%%", raw, percentage);
    return percentage;
}

// Read DHT11 data (placeholder - implement proper DHT library)
static void read_dht_data(float *humidity, float *temperature) {
    // Replace with actual DHT sensor reading
    *humidity = 45.0 + (esp_random() % 20); // Random between 45-65%
    *temperature = 25.0 + (esp_random() % 10); // Random between 25-35°C
}

// Publish sensor data to Arduino IoT Cloud
static void publish_sensor_data(sensor_data_t *data) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "humidity", data->humidity);
    cJSON_AddNumberToObject(root, "soilMoisture", data->soil_moisture);
    cJSON_AddNumberToObject(root, "temperature", data->temperature);
    cJSON_AddNumberToObject(root, "nitrogen", data->nitrogen);
    cJSON_AddNumberToObject(root, "phosphorus", data->phosphorus);
    cJSON_AddNumberToObject(root, "potassium", data->potassium);
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        int msg_id = esp_mqtt_client_publish(client, "/v2/things/" DEVICE_ID "/data", json_str, 0, 1, 0);
        ESP_LOGI(TAG, "Published data with msg_id: %d", msg_id);
        free(json_str);
    }
    cJSON_Delete(root);
}

// Main sensor task
static void sensor_task(void *pvParameter) {
    // NPK sensor commands (modify based on your sensor protocol)
    uint8_t nitrogen_cmd[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C};
    uint8_t phosphorus_cmd[] = {0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC};
    uint8_t potassium_cmd[] = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0};
    
    while (1) {
        sensor_data_t data = {0};
        
        // Read all sensors
        read_dht_data(&data.humidity, &data.temperature);
        data.soil_moisture = read_soil_moisture();
        data.nitrogen = read_npk_value(nitrogen_cmd, sizeof(nitrogen_cmd));
        data.phosphorus = read_npk_value(phosphorus_cmd, sizeof(phosphorus_cmd));
        data.potassium = read_npk_value(potassium_cmd, sizeof(potassium_cmd));
        
        ESP_LOGI(TAG, "Sensor Data - Hum: %.1f%%, Temp: %.1fC, Soil: %d%%, N: %d, P: %d, K: %d", 
                data.humidity, data.temperature, data.soil_moisture, 
                data.nitrogen, data.phosphorus, data.potassium);
        
        publish_sensor_data(&data);
        vTaskDelay(SENSOR_READ_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

void app_main() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize components
    wifi_init();
    mqtt_app_start();
    npk_uart_init();
    
    // Configure ADC for soil moisture
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SOIL_MOISTURE_PIN, ADC_ATTEN_DB_11);
    
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    
    // Start sensor task
    xTaskCreate(&sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "System initialization complete. Starting sensor monitoring...");
}