#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ws2811.h>
#include <json-c/json.h>
#include <MQTTClient.h>

#define TARGET_FREQ    WS2811_TARGET_FREQ
#define GPIO_PIN      18
#define DMA           10
#define STRIP_TYPE    WS2811_STRIP_GRB

#define LED_COUNT     144
#define QOS           1
#define TIMEOUT       10000L

// MQTT Configuration
#define MQTT_BROKER   "ssl://a1cgkxm1csqtha-ats.iot.ap-south-1.amazonaws.com:8883"
#define MQTT_CLIENTID "button_state_update_subscriber_n8n"
#define MQTT_TOPIC    "n8n/button/state"

// AWS IoT Certificate paths
#define CA_PATH       "AmazonRootCA1.pem"
#define CERT_PATH     "button_state_update_subscriber_n8n.cert.pem"
#define KEY_PATH      "button_state_update_subscriber_n8n.private.key"

ws2811_t ledstring;
MQTTClient client;
static int running = 1;

void ctrl_c_handler(int signo) {
    (void)(signo);
    running = 0;
}

void setup_handlers(void) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);
}

void delivered(void *context, MQTTClient_deliveryToken dt) {
    (void)(context);
    (void)(dt);
    printf("Message delivered\n");
}

void connlost(void *context, char *cause) {
    (void)(context);
    printf("Connection lost: %s\n", cause);
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    (void)(context);
    (void)(topicLen);
    
    printf("Message arrived on topic: %s\n", topicName);
    
    // Parse JSON message
    char *payload = (char*)message->payload;
    json_object *json = json_tokener_parse(payload);
    
    if (json == NULL) {
        printf("Failed to parse JSON\n");
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }
    
    // Extract switch_state from JSON
    json_object *switch_state_obj;
    
    if (json_object_object_get_ex(json, "switch_state", &switch_state_obj)) {
        const char *switch_state = json_object_get_string(switch_state_obj);
        printf("Switch state: %s\n", switch_state);
        
        if (strcmp(switch_state, "pressed") == 0) {
            // Turn on LEDs with white color
            printf("Turning ON LEDs with white color\n");
            for (int i = 0; i < LED_COUNT; i++) {
                ledstring.channel[0].leds[i] = 0xFFFFFF; // White
            }
            ws2811_render(&ledstring);
        } else if (strcmp(switch_state, "released") == 0) {
            // Turn off LEDs
            printf("Turning OFF LEDs\n");
            for (int i = 0; i < LED_COUNT; i++) {
                ledstring.channel[0].leds[i] = 0x000000; // Off
            }
            ws2811_render(&ledstring);
        } else {
            printf("Unknown switch_state: %s\n", switch_state);
        }
    } else {
        printf("No 'switch_state' field found in JSON message\n");
        printf("Received JSON: %s\n", payload);
    }
    
    json_object_put(json);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int main(void) {
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    
    // Initialize LED string
    ledstring.freq = TARGET_FREQ;
    ledstring.dmanum = DMA;
    ledstring.channel[0].gpionum = GPIO_PIN;
    ledstring.channel[0].count = LED_COUNT;
    ledstring.channel[0].invert = 0;
    ledstring.channel[0].brightness = 255;
    ledstring.channel[1].gpionum = 0;
    ledstring.channel[1].count = 0;
    ledstring.channel[1].invert = 0;
    ledstring.channel[1].brightness = 0;

    ws2811_return_t ret_val;
    if ((ret_val = ws2811_init(&ledstring)) != WS2811_SUCCESS) {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret_val));
        return ret_val;
    }

    // Initialize MQTT client
    if ((ret = MQTTClient_create(&client, MQTT_BROKER, MQTT_CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to create MQTT client, return code %d\n", ret);
        return ret;
    }

    if ((ret = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to set callbacks, return code %d\n", ret);
        return ret;
    }

    // Configure SSL options with your certificate paths
    ssl_opts.trustStore = CA_PATH;
    ssl_opts.keyStore = CERT_PATH;
    ssl_opts.privateKey = KEY_PATH;
    ssl_opts.enableServerCertAuth = 1;
    conn_opts.ssl = &ssl_opts;

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    printf("Connecting to MQTT broker: %s\n", MQTT_BROKER);
    printf("Client ID: %s\n", MQTT_CLIENTID);
    printf("Topic: %s\n", MQTT_TOPIC);
    printf("CA: %s\n", CA_PATH);
    printf("Cert: %s\n", CERT_PATH);
    printf("Key: %s\n", KEY_PATH);
    printf("Waiting for switch_state messages...\n");
    printf("Expected JSON format: {\"switch_state\": \"pressed\"} or {\"switch_state\": \"released\"}\n");
    
    if ((ret = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", ret);
        printf("Note: Make sure the certificate files exist and are readable\n");
        
        // For now, just run the LED test pattern
        setup_handlers();
        printf("Running LED test pattern instead...\n");
        
        // Simple test pattern
        int i;
        while (running) {
            // Red pattern
            for (i = 0; i < LED_COUNT; i++) {
                ledstring.channel[0].leds[i] = 0xFF0000;
            }
            ws2811_render(&ledstring);
            usleep(1000000);

            // Green pattern
            for (i = 0; i < LED_COUNT; i++) {
                ledstring.channel[0].leds[i] = 0x00FF00;
            }
            ws2811_render(&ledstring);
            usleep(1000000);

            // Blue pattern
            for (i = 0; i < LED_COUNT; i++) {
                ledstring.channel[0].leds[i] = 0x0000FF;
            }
            ws2811_render(&ledstring);
            usleep(1000000);

            // Clear
            for (i = 0; i < LED_COUNT; i++) {
                ledstring.channel[0].leds[i] = 0x000000;
            }
            ws2811_render(&ledstring);
            usleep(1000000);
        }
    } else {
        printf("Connected to MQTT broker successfully!\n");
        
        // Subscribe to LED control topic
        if ((ret = MQTTClient_subscribe(client, MQTT_TOPIC, QOS)) != MQTTCLIENT_SUCCESS) {
            printf("Failed to subscribe, return code %d\n", ret);
            return ret;
        }
        printf("Subscribed to topic: %s\n", MQTT_TOPIC);
        printf("Waiting for MQTT messages...\n");
        
        setup_handlers();
        
        // Keep the application running
        while (running) {
            usleep(100000); // 100ms
        }
        
        // Disconnect
        MQTTClient_disconnect(client, 10000);
    }

    // Cleanup
    MQTTClient_destroy(&client);
    
    for (int i = 0; i < LED_COUNT; i++) {
        ledstring.channel[0].leds[i] = 0x000000;
    }
    ws2811_render(&ledstring);
    ws2811_fini(&ledstring);

    printf("\nLED Matrix shutdown complete.\n");
    return 0;
} 