/*
 * Button monitoring example using FreeRTOS with AWS IoT Core MQTT
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "certs.h"
#include <inttypes.h>

#define BUTTON_PIN GPIO_NUM_9
#define DEBOUNCE_DELAY_MS 50
#define TASK_STACK_SIZE 4096

#define WIFI_SSID      "AndroidAPDCFD"
#define WIFI_PASS      "zaffi@1012"

static const char *TAG = "BUTTON_EXAMPLE";
static bool last_button_state = true;  // HIGH state is true for INPUT_PULLUP
static esp_mqtt_client_handle_t mqtt_client = NULL;

// MQTT event handler (correct signature)
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to AWS IoT Core");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected from AWS IoT Core");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

// Initialize MQTT client
static void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "Starting MQTT client...");
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtts://a1cgkxm1csqtha-ats.iot.ap-south-1.amazonaws.com:8883",
        .broker.verification.certificate = (const char *)AWS_ROOT_CA_CERT,
        .credentials.authentication.certificate = (const char *)DEVICE_CERT,
        .credentials.authentication.key = (const char *)DEVICE_PRIVATE_KEY,
        .credentials.client_id = "button_n8n_testing_publisher",
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }
    
    esp_err_t err = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "MQTT event handler registered successfully");

    err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "MQTT client started successfully");
}

// Publish button state to MQTT
static void publish_button_state(bool button_state)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "button_state", button_state ? "released" : "pressed");

    char *json_str = cJSON_Print(root);
    if (json_str) {
        esp_mqtt_client_publish(mqtt_client, AWS_IOT_TOPIC, json_str, strlen(json_str), 1, 0);
        free(json_str);
    }

    cJSON_Delete(root);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        mqtt_app_start();
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
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
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        bool current_button_state = gpio_get_level(BUTTON_PIN);

        if (current_button_state != last_button_state) {
            publish_button_state(current_button_state);
            last_button_state = current_button_state;
        }

        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_DELAY_MS));
    }
}

void app_main(void)
{
    wifi_init_sta();
    xTaskCreate(button_task, "button_task", TASK_STACK_SIZE, NULL, 10, NULL);
}
