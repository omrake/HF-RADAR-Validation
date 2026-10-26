#include <HardwareSerial.h>
#include <WalterModem.h>
#include <inttypes.h>
#include <gnssUpload.h>

// GNSS

#define MAX_GNSS_CONFIDENCE 100.0

// Maximum time to wait for a GNSS fix
#define GNSS_TIMEOUT_MS 120000


// Modem

WalterModem modem;

WalterModemRsp rsp;

WalterModemGNSSFix latestGnssFix = {};

volatile bool gnssFixRcvd = false;



// LTE

bool lteConnect() {

  WalterModemNetworkRegState regState =
      modem.getNetworkRegState();

  return (
    regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
    regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING
  );
}


bool waitForNetwork() {

  Serial.println("Waiting for LTE...");

  for (int timeout = 0; timeout < 300; timeout++) {

    if (lteConnect()) {
      Serial.println("Connected to the network");
      return true;
    }

    delay(1000);
  }

  Serial.println("LTE: TIMEOUT");

  return false;
}


bool NetworkSelect() {

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



// LTE disconnect for GNSS

bool lteDisconnect() {

  Serial.println("Turning LTE off for GNSS...");

  if (!modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {

    Serial.println("LTE disconnect: FAIL");
    return false;
  }

  Serial.println("LTE: OFF");

  return true;
}



// GNSS event handler

void gnssEventHandler(
    const WalterModemGNSSFix* fix,
    void* args) {

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
    "\nGNSS update: Lat %.6f, Lon %.6f, Sats %d, Good sats %d, Confidence %.2f\n",
    latestGnssFix.latitude,
    latestGnssFix.longitude,
    latestGnssFix.satCount,
    goodSatCount,
    latestGnssFix.estimatedConfidence
  );

  // Only mark it as a real fix if it actually looks valid.
  if (
    latestGnssFix.satCount > 0 &&
    latestGnssFix.estimatedConfidence <= MAX_GNSS_CONFIDENCE
  ) {

    gnssFixRcvd = true;

    Serial.println("GNSS: VALID FIX");
  }
}



// GNSS fix

bool attemptGNSSFix() {

  gnssFixRcvd = false;
  latestGnssFix = {};

  Serial.println();
  Serial.println("Starting GNSS...");

  // Turn LTE off.
  if (!lteDisconnect()) {
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
    "Latitude:  %.6f\n"
    "Longitude: %.6f\n"
    "Satellites: %d\n"
    "Confidence: %.2f\n",
    latestGnssFix.latitude,
    latestGnssFix.longitude,
    latestGnssFix.satCount,
    latestGnssFix.estimatedConfidence
  );

  return true;
}



// MQTT

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

  // Give the modem time to complete the MQTT connection.
  delay(5000);

  return true;
}



// SETUP

void setupGNSS() {

  Serial.begin(115200);

  delay(5000);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" Walter ThingSpeak + GNSS");
  Serial.println("==============================");


  // GNSS event handler
  modem.gnssSetEventHandler(
    gnssEventHandler,
    NULL
  );


  // Initialize modem
  if (!WalterModem::begin(&Serial2)) {

    Serial.println("MODEM: FAIL");
    return;
  }

  Serial.println("MODEM: OK");


  // Connect LTE initially
  if (!lteConnect()) {

    Serial.println("LTE: FAIL");
    return;
  }

  Serial.println("Initial LTE connection successful.");
}



// LOOP

void loopGNSS() {

  Serial.println();
  Serial.println("==============================");
  Serial.println("New measurement");
  Serial.println("==============================");


 
  // Temperature

  float temp = temperatureRead();

  Serial.printf(
    "Temperature: %.2f C\n",
    temp
  );


  
  // GNSS

  bool gotGNSS = attemptGNSSFix();


  
  // Reconnect LTE

  Serial.println();
  Serial.println("Reconnecting LTE...");

  if (!lteConnect()) {

    Serial.println("LTE reconnect FAILED");

    delay(15000);

    return;
  }


 
  // MQTT

  if (!mqttConnect()) {

    Serial.println("MQTT reconnect FAILED");

    delay(15000);

    return;
  }


 
  // Prepare ThingSpeak payload

  char outgoingMsg[128];

  if (gotGNSS) {

    snprintf(
      outgoingMsg,
      sizeof(outgoingMsg),
      "field1=%.2f&field2=%.6f&field3=%.6f",
      temp,
      latestGnssFix.latitude,
      latestGnssFix.longitude
    );

  } else {

    snprintf(
      outgoingMsg,
      sizeof(outgoingMsg),
      "field1=%.2f",
      temp
    );
  }


  Serial.println();
  Serial.printf(
    "Publishing: %s\n",
    outgoingMsg
  );


 
  // Publish

  if (modem.mqttPublish(
        THINGSPEAK_TOPIC,
        (uint8_t*)outgoingMsg,
        strlen(outgoingMsg),
        0)) {

    Serial.println("THINGSPEAK: PUBLISH OK");

  } else {

    Serial.println("THINGSPEAK: PUBLISH FAILED");
  }



  // Wait

  Serial.println("Waiting 15 seconds...");

  delay(15000);
}
