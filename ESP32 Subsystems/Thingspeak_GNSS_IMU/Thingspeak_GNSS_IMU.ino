#include <HardwareSerial.h>
#include <WalterModem.h>
#include <inttypes.h>
#include <IMU_Readings.h>

// IMU pins
#define SDA_PIN 12
#define SCL_PIN 11

// ThingSpeak MQTT

#define THINGSPEAK_MQTT_USERNAME  "PC8WHQ4CJBsfAh8aLzImAiE"
#define THINGSPEAK_MQTT_CLIENT_ID "PC8WHQ4CJBsfAh8aLzImAiE"
#define THINGSPEAK_MQTT_PASSWORD  "gF4KNHHIsXylj+/1QxnCb1K4"

#define THINGSPEAK_CHANNEL_ID "3502498"
#define THINGSPEAK_TOPIC "channels/" THINGSPEAK_CHANNEL_ID "/publish"

/*
Built by the HF Radar Sense Enterprise team
*/
void setup() {
  setupIMU();
}

void loop(){
  loopIMU();
}
