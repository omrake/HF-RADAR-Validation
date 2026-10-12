
#include <HardwareSerial.h>
#include <WalterModem.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

// ============================================================
// ThingSpeak MQTT credentials
// ============================================================

#define THINGSPEAK_MQTT_USERNAME  "PC8WHQ4CJBsfAh8aLzImAiE"
#define THINGSPEAK_MQTT_CLIENT_ID "PC8WHQ4CJBsfAh8aLzImAiE"
#define THINGSPEAK_MQTT_PASSWORD  "gF4KNHHIsXylj+/1QxnCb1K4"

#define THINGSPEAK_CHANNEL_ID "3502498"
#define THINGSPEAK_TOPIC "channels/" THINGSPEAK_CHANNEL_ID "/publish"

// ============================================================
// GNSS configuration
// ============================================================

#define MAX_GNSS_CONFIDENCE 100.0
#define GNSS_TIMEOUT_MS     120000UL

// ============================================================
// Timing
// ============================================================

#define LTE_TIMEOUT_SECONDS 300
#define MQTT_CONNECT_DELAY_MS 5000
#define RETRY_DELAY_MS 15000
#define MEASUREMENT_DELAY_MS 15000

// ============================================================
// Modem state
// ============================================================

WalterModem modem;

WalterModemGNSSFix latestGnssFix = {};

volatile bool gnssFixRcvd = false;

// ============================================================
// LTE network functions
// ============================================================

bool lteConnected() {
  WalterModemNetworkRegState regState =
      modem.getNetworkRegState();

  return (
    regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
    regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING
  );
}

bool waitForNetwork() {
  Serial.println("Waiting for LTE...");

  for (int timeout = 0;
       timeout < LTE_TIMEOUT_SECONDS;
       timeout++) {

    if (lteConnected()) {
      Serial.println("LTE: CONNECTED");
      return true;
    }

    delay(1000);
  }

  Serial.println("LTE: TIMEOUT");
  return false;
}

bool lteConnect() {
  Serial.println("Connecting to LTE...");

  if (!modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
    Serial.println("LTE: could not enter NO_RF");
    return false;
  }

  if (!modem.definePDPContext()) {
    Serial.println("LTE: PDP context failed");
    return false;
  }

  if (!modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
    Serial.println("LTE: could not enter FULL");
    return false;
  }

  if (!modem.setNetworkSelectionMode(
          WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
    Serial.println("LTE: network selection failed");
    return false;
  }

  return waitForNetwork();
}

bool lteDisconnect() {
  Serial.println("Turning LTE off for GNSS...");

  if (!modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
    Serial.println("LTE disconnect: FAIL");
    return false;
  }

  Serial.println("LTE: OFF");
  return true;
}

// ============================================================
// GNSS validation
// ============================================================

bool validGnssFix(const WalterModemGNSSFix& fix) {
  if (fix.satCount == 0) {
    return false;
  }

  if (!isfinite(fix.latitude) ||
      !isfinite(fix.longitude) ||
      !isfinite(fix.estimatedConfidence)) {
    return false;
  }

  if (fix.latitude < -90.0 ||
      fix.latitude > 90.0) {
    return false;
  }

  if (fix.longitude < -180.0 ||
      fix.longitude > 180.0) {
    return false;
  }

  if (fix.estimatedConfidence < 0.0 ||
      fix.estimatedConfidence > MAX_GNSS_CONFIDENCE) {
    return false;
  }

  return true;
}

// ============================================================
// GNSS event handler
// ============================================================

void gnssEventHandler(
    const WalterModemGNSSFix* fix,
    void* args) {

  if (fix == nullptr) {
    return;
  }

  memcpy(
    &latestGnssFix,
    fix,
    sizeof(WalterModemGNSSFix)
  );

  uint8_t goodSatCount = 0;

  for (int i = 0; i < latestGnssFix.satCount; i++) {
    if (latestGnssFix.sats[i].signalStrength >= 30) {
      goodSatCount++;
    }
  }

  Serial.printf(
    "\nGNSS update: Lat %.6f, Lon %.6f, "
    "Sats %d, Good sats %d, Confidence %.2f\n",
    latestGnssFix.latitude,
    latestGnssFix.longitude,
    latestGnssFix.satCount,
    goodSatCount,
    latestGnssFix.estimatedConfidence
  );

  if (validGnssFix(latestGnssFix)) {
    gnssFixRcvd = true;
    Serial.println("GNSS: VALID FIX");
  } else {
    Serial.println("GNSS: FIX REJECTED");
  }
}

// ============================================================
// Obtain a valid GNSS fix
// ============================================================

bool attemptGNSSFix() {
  gnssFixRcvd = false;
  latestGnssFix = {};

  Serial.println();
  Serial.println("==============================");
  Serial.println("Starting GNSS");
  Serial.println("==============================");

  if (!lteDisconnect()) {
    Serial.println("GNSS: could not disable LTE");
    return false;
  }

  Serial.println("Requesting GNSS fix...");

  if (!modem.gnssPerformAction()) {
    Serial.println("GNSS: request failed");
    return false;
  }

  unsigned long startTime = millis();

  while (!gnssFixRcvd) {
    delay(500);

    Serial.print(".");

    if (millis() - startTime >= GNSS_TIMEOUT_MS) {
      Serial.println();
      Serial.println("GNSS: TIMEOUT");
      return false;
    }
  }

  Serial.println();
  Serial.println("GNSS: FIX SUCCESS");

  Serial.printf(
    "Latitude:   %.6f\n"
    "Longitude:  %.6f\n"
    "Satellites: %d\n"
    "Confidence: %.2f\n",
    latestGnssFix.latitude,
    latestGnssFix.longitude,
    latestGnssFix.satCount,
    latestGnssFix.estimatedConfidence
  );

  return true;
}

// ============================================================
// MQTT connection
// ============================================================

bool mqttConnect() {
  Serial.println("Configuring MQTT...");

  if (!modem.mqttConfig(
        THINGSPEAK_MQTT_CLIENT_ID,
        THINGSPEAK_MQTT_USERNAME,
        THINGSPEAK_MQTT_PASSWORD)) {

    Serial.println("MQTT CONFIG: FAIL");
    return false;
  }

  Serial.println("MQTT CONFIG: OK");
  Serial.println("Connecting to ThingSpeak...");

  if (!modem.mqttConnect(
        "mqtt3.thingspeak.com",
        1883)) {

    Serial.println("MQTT CONNECT: FAIL");
    return false;
  }

  Serial.println("MQTT CONNECT: COMMAND OK");

  // Allow time for the modem to establish the MQTT connection.
  delay(MQTT_CONNECT_DELAY_MS);

  return true;
}

// ============================================================
// Publish one complete measurement
// ============================================================

bool publishMeasurement(float temp,
                        const WalterModemGNSSFix& fix) {

  // Never publish a partial or invalid measurement.
  if (!validGnssFix(fix)) {
    Serial.println("PUBLISH ABORTED: invalid GNSS data");
    return false;
  }

  // Validate coordinates again immediately before publishing.
  Serial.println();
  Serial.println("Preparing ThingSpeak measurement...");

  Serial.printf(
    "Temperature: %.2f C\n"
    "Latitude:    %.6f\n"
    "Longitude:   %.6f\n",
    temp,
    fix.latitude,
    fix.longitude
  );

  char outgoingMsg[160];

  int payloadLength = snprintf(
    outgoingMsg,
    sizeof(outgoingMsg),
    "field1=%.2f&field2=%.6f&field3=%.6f",
    temp,
    fix.latitude,
    fix.longitude
  );

  if (payloadLength <= 0 ||
      payloadLength >= (int)sizeof(outgoingMsg)) {
    Serial.println("PUBLISH ABORTED: payload error");
    return false;
  }

  Serial.printf("Payload: %s\n", outgoingMsg);

  if (!modem.mqttPublish(
        THINGSPEAK_TOPIC,
        (uint8_t*)outgoingMsg,
        strlen(outgoingMsg),
        0)) {

    Serial.println("THINGSPEAK: PUBLISH FAILED");
    return false;
  }

  Serial.println("THINGSPEAK: PUBLISH COMMAND OK");
  Serial.println(
    "Check the ThingSpeak channel feed to confirm the entry."
  );

  return true;
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(5000);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" Walter ThingSpeak + GNSS");
  Serial.println("==============================");

  // Register the GNSS event handler.
  modem.gnssSetEventHandler(
    gnssEventHandler,
    NULL
  );

  // Initialize the modem.
  if (!WalterModem::begin(&Serial2)) {
    Serial.println("MODEM: FAIL");
    return;
  }

  Serial.println("MODEM: OK");

  // Establish initial LTE connectivity.
  if (!lteConnect()) {
    Serial.println("INITIAL LTE CONNECTION: FAIL");
    return;
  }

  Serial.println("Initial LTE connection successful.");
}

// ============================================================
// Main measurement loop
// ============================================================

void loop() {
  Serial.println();
  Serial.println("==============================");
  Serial.println("New measurement");
  Serial.println("==============================");

  // 1. Read temperature.
  float temp = temperatureRead();

  Serial.printf("Temperature: %.2f C\n", temp);

  // 2. Obtain a valid GNSS fix.
  bool gotGNSS = attemptGNSSFix();

  if (!gotGNSS) {
    Serial.println();
    Serial.println("MEASUREMENT SKIPPED");
    Serial.println("No valid GNSS fix; nothing published.");

    // Restore LTE for the next measurement cycle.
    Serial.println("Restoring LTE...");

    if (!lteConnect()) {
      Serial.println("LTE RESTORE FAILED");
    }

    delay(RETRY_DELAY_MS);
    return;
  }

  // 3. Restore LTE.
  Serial.println();
  Serial.println("Reconnecting LTE...");

  if (!lteConnect()) {
    Serial.println("LTE reconnect FAILED");
    delay(RETRY_DELAY_MS);
    return;
  }

  // 4. Connect to MQTT.
  if (!mqttConnect()) {
    Serial.println("MQTT reconnect FAILED");
    delay(RETRY_DELAY_MS);
    return;
  }

  // 5. Publish temperature, latitude, and longitude together.
  bool published = publishMeasurement(
    temp,
    latestGnssFix
  );

  if (published) {
    Serial.println("MEASUREMENT PUBLISHED");
  } else {
    Serial.println("MEASUREMENT NOT PUBLISHED");
  }

  // 6. Wait before the next measurement cycle.
  Serial.printf(
    "Waiting %lu seconds...\n",
    MEASUREMENT_DELAY_MS / 1000
  );

  delay(MEASUREMENT_DELAY_MS);
}