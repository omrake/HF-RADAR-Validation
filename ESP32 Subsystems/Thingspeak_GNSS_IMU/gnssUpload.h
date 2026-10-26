// Header file for GNSS uploading

// ThingSpeak MQTT
#define THINGSPEAK_MQTT_USERNAME  "PC8WHQ4CJBsfAh8aLzImAiE"
#define THINGSPEAK_MQTT_CLIENT_ID "PC8WHQ4CJBsfAh8aLzImAiE"
#define THINGSPEAK_MQTT_PASSWORD  "gF4KNHHIsXylj+/1QxnCb1K4"

#define THINGSPEAK_CHANNEL_ID "3502498"
#define THINGSPEAK_TOPIC "channels/" THINGSPEAK_CHANNEL_ID "/publish"

// Connect to the LTE network
bool lteconnect();

// Wait for network connection to establish
bool waitForNetwork();

// Selects state of WalterModem with LTE connection
bool networkSelect();

// Disconects from GNSS
bool lteDisconnect();

// Handles different events for GNSS
void gnssEventHandler();

// attempt a GNSS fix
bool attemptGNSSFix();

// attempt to connect to MQTT
bool mqttconnect();

// sets up all other methods
void setupGNSS();

// Loops through all other methods
void loopGNSS();
