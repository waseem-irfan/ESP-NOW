#include <stdio.h>
#include "string.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "protocol_examples_common.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "driver/gpio.h"
#include "driver/touch_pad.h"

#define ESP_NOW_TAG "ESP-NOW"
#define PIN 2
#define RELAY_PIN GPIO_NUM_19
uint8_t esp_1[6]= {0xec,0x62,0x60,0x9b,0x18,0x40};
uint8_t esp_2[6] = {0xb0,0xb2,0x1c,0xa6,0x2b,0x48};

static char * MQTT_TAG = "MQTT";
static void mqtt_event_handler(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data);
static void test_send_messages(void);
static esp_mqtt_client_handle_t client;

void on_receive(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
char *mac_to_str(char *buffer, uint8_t *mac)
{
  sprintf(buffer, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buffer;
}


void on_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  char buffer[13];
  switch (status)
  {
  case ESP_NOW_SEND_SUCCESS:
    ESP_LOGI(ESP_NOW_TAG, "message sent to %s", mac_to_str(buffer, (uint8_t *)mac_addr));
    break;
  case ESP_NOW_SEND_FAIL:
    ESP_LOGE(ESP_NOW_TAG, "message sent to %s failed", mac_to_str(buffer, (uint8_t *)mac_addr));
    break;
  }
}

void on_receive(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    if(strstr((char*) data,"Toggle LED") != NULL){
    gpio_set_direction(PIN, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(PIN,!gpio_get_level(PIN));
    ESP_LOGI(ESP_NOW_TAG, "got message from " MACSTR, MAC2STR(esp_now_info->src_addr));
    printf("message: %.*s\n", data_len, data);
    printf("LED State: %d\n",gpio_get_level(PIN));
    gpio_set_direction(RELAY_PIN,GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(RELAY_PIN,!gpio_get_level(RELAY_PIN));
    printf("BULB State: %d\n",!gpio_get_level(RELAY_PIN));
    test_send_messages();
    }   
}


void app_main(void)
{
    uint8_t my_mac[6];
    esp_efuse_mac_get_default(my_mac);
    char my_mac_str[13];
     ESP_LOGI(ESP_NOW_TAG, "My mac %s", mac_to_str(my_mac_str, my_mac));
    bool is_current_esp1 = memcmp(my_mac, esp_1, 6) == 0;
    uint8_t *peer_mac = is_current_esp1 ? esp_2 : esp_1;
// Touch pad Configuration
    touch_pad_init();
    touch_pad_set_voltage(TOUCH_HVOLT_2V5,TOUCH_LVOLT_0V7,TOUCH_HVOLT_ATTEN_1V);
    touch_pad_config(TOUCH_PAD_NUM4,-1);

    
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_mqtt_client_config_t esp_mqtt_client_config ={
        .broker.address.uri = "mqtt://test.mosquitto.org:1883"
     };
    client = esp_mqtt_client_init(&esp_mqtt_client_config);
    esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,mqtt_event_handler,NULL);
    esp_mqtt_client_start(client);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_receive));
 
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, peer_mac, 6);

    esp_now_add_peer(&peer);


while(true)
    {
        uint16_t raw;
        uint32_t raw_MAX =4095;
        touch_pad_read(TOUCH_PAD_NUM4,&raw); //Reading from PIN
        double voltage = raw*(2.5-0.7)/raw_MAX + 0.7; // Processing Raw data to voltage
        char send_buffer[250];
        if(voltage > 0.7 && voltage <1.13){
            sprintf(send_buffer, "Hello! SomeOne touched me from %s Toggle LED", my_mac_str);
            ESP_ERROR_CHECK(esp_now_send(NULL, (uint8_t *)send_buffer, strlen(send_buffer)));
            
        }
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }

    ESP_ERROR_CHECK(esp_now_deinit());
    ESP_ERROR_CHECK(esp_wifi_stop());

    
}
static void mqtt_event_handler(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data){
     esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, "esp32/sensor/touch/data", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(MQTT_TAG, "MQTT_EVENT_SUBSCRIBED");
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(MQTT_TAG, "MQTT_EVENT_UNSUBSCRIBED");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(MQTT_TAG, "MQTT_EVENT_PUBLISHED");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DATA");
        printf("topic: %.*s\n", event->topic_len, event->topic);
        printf("message: %.*s\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(MQTT_TAG, "ERROR %s", strerror(event->error_handle->esp_transport_sock_errno));
        break;
    default:
        break;
    }
}

int mqtt_send(const char *topic, const char *payload)
{
    return esp_mqtt_client_publish(client, topic, payload, strlen(payload), 1,0);
}

static void test_send_messages(void)
{
    char message[200];
    sprintf(message,"LED State: %d\n BULB state: %d\n",gpio_get_level(PIN),!gpio_get_level(RELAY_PIN));
    mqtt_send("esp32/sensor/touch/data", message);
}
