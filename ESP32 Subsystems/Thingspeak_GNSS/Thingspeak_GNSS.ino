#include <WalterModem.h>
#include <inttypes.h>
#include <esp_mac.h>
#include <HardwareSerial.h>


// The ThingSpeak device MQTT username.
#define THINGSPEAK_MQTT_USERNAME "PC8WHQ4CJBsfAh8aLzImAiE"

//The ThingSpeak device MQTT client id.
#define THINGSPEAK_MQTT_CLIENT_ID "PC8WHQ4CJBsfAh8aLzImAiE"

// The ThingSpeak device MQTT password.
#define THINGSPEAK_MQTT_PASSWORD "gF4KNHHIsXylj+/1QxnCb1K49"

//The id of the ThingSpeak channel to publish data to
#define THINGSPEAK_CHANNEL_ID "3502498"

//The topic to publish ThingSpeak data to.
#define THINGSPEAK_TOPIC "channels/" THINGSPEAK_CHANNEL_ID "/publish"

//fixes with a confidence below this number are considered ok
#define MAX_GNSS_CONFIDENCE 100.0


// The size in bytes of a minimal sensor + GNSS + cellinfo packet
#define PACKET_SIZE 30

//Sets the prefered raido access technology
#define RADIO_TECHNOLOGY WALTER_MODEM_RAT_LTEM



//creates modem interface objects with names modem and rsp
WalterModem modem;

WalterModemRsp rsp;



bool mqttConnected = false;  // tracks MQTT connection state


//function ONLY CHECKS if we are connected to the cellular network

bool lteConnected() {
  WalterModemNetworkRegState regState = modem.getNetworkRegState();
  return (regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
          regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING);
}



//function WAITS for network if not connected

bool waitForNetwork() {
  /* Wait for the network to become available */
  int timeout = 0;
  while (!lteConnected()) {
    delay(1000);
    timeout++;
    if (timeout > 300) {
      ESP.restart();
      return false;
    }
  }
  Serial.println("Connected to the network");
  return true;
}


//function CONNECTS modem to cellular network

bool lteConnect() {
  if (modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
    Serial.println("Successfully set operational state to NO RF");
  } else {
    Serial.println("Error: Could not set operational state to NO RF");
    return false;
  }

  // Create PDP context
  if (modem.definePDPContext()) {
    Serial.println("Created PDP context");
  } else {
    Serial.println("Error: Could not create PDP context");
    return false;
  }

  // Set the operational state to full
  if (modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
    Serial.println("Successfully set operational state to FULL");
  } else {
    Serial.println("Error: Could not set operational state to FULL");
    return false;
  }

  // Set the network operator selection to automatic
  if (modem.setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
    Serial.println("Network selection mode to was set to automatic");
  } else {
    Serial.println(
        "Error: Could not set the network selection mode to automatic");
    return false;
  }

  return waitForNetwork();
}





//function Disconnects from network preparing for GNSS fix

bool lteDisconnect() {
    if(modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
        Serial.println("LTE disconnected, op state MINIMUM");
    } else {
        Serial.println("Error: Could not set op state MINIMUM");
        return false;
    }

    WalterModemNetworkRegState regState = modem.getNetworkRegState();
    while(regState != WALTER_MODEM_NETWORK_REG_NOT_SEARCHING) {
        delay(100);
        regState = modem.getNetworkRegState();
    }

    return true;
}



volatile bool gnssFixRcvd = false;           // tracks if GNSS fix is received
WalterModemGNSSFix latestGnssFix = {};       // stores latest GNSS fix




//Validating clock is synced
bool validateGNSSClock(WalterModemRsp* rsp)
{
    // Validate the GNSS subsystem clock
    modem.gnssGetUTCTime(rsp);
    if (rsp->data.clock.epochTime > 4) {
        return true;
    }

    Serial.println("System clock invalid, LTE time sync required");

    if (!lteConnected() && !lteConnect()) {
        Serial.println("Error: Could not connect to LTE network");
        return false;
    }

    for (int i = 0; i < 5; ++i) {
        modem.gnssGetUTCTime(rsp);
        if (rsp->data.clock.epochTime > 4) {
            Serial.printf("Clock synchronized: %" PRIi64 "\n", rsp->data.clock.epochTime);
            return true;
        }
        delay(2000);
    }

    Serial.println("Error: Could not sync GNSS clock with LTE");
    return false;
}





//function attempts to get a GNSS fix

bool attemptGNSSFix() {
    WalterModemRsp rspLocal = {};

    // Make sure the GNSS clock is valid
    if(!validateGNSSClock(&rspLocal)) {
        Serial.println("Error: Could not validate GNSS clock");
        return false;
    }

    // Disconnect LTE if needed (required for GNSS)
    if(lteConnected() && !lteDisconnect()) {
        Serial.println("Error: Could not disconnect LTE");
        return false;
    }

    // Attempt up to 5 fixes
    const int maxAttempts = 5;
    for(int attempt = 0; attempt < maxAttempts; ++attempt) {
        gnssFixRcvd = false;

        if(!modem.gnssPerformAction()) {
            Serial.println("Error: Could not request GNSS fix");
            return false;
        }

        while(!gnssFixRcvd) {
            Serial.print(".");
            delay(500);
        }

        if(latestGnssFix.estimatedConfidence <= MAX_GNSS_CONFIDENCE) {
            Serial.println("Successfully obtained a valid GNSS fix");
            return true;
        } else {
            Serial.printf("GNSS fix confidence %.02f too low, retrying...\n",
                          latestGnssFix.estimatedConfidence);
        }
    }

    return false;
}




//GNSS Event Handler
//function sends GNSS data to latestGNSSFix for thigspeak to useGNSS event handler

void gnssEventHandler(const WalterModemGNSSFix* fix, void* args) {
    memcpy(&latestGnssFix, fix, sizeof(WalterModemGNSSFix));

    uint8_t goodSatCount = 0;
    for(int i = 0; i < latestGnssFix.satCount; ++i) {
        if(latestGnssFix.sats[i].signalStrength >= 30) ++goodSatCount;
    }

    Serial.printf("GNSS fix received: Lat %.6f, Lon %.6f, Satcount %d, Good sats %d\n",
                  latestGnssFix.latitude, latestGnssFix.longitude,
                  latestGnssFix.satCount, goodSatCount);

    gnssFixRcvd = true;
}




void setup() {
  Serial.begin(115200);
  delay(5000);

  Serial.println("Walter Thingspeak GNSS");

  modem.gnssSetEventHandler(gnssEventHandler, NULL);

  if (WalterModem::begin(&Serial2)) {
    Serial.println("Modem initialization OK");
  } else {
    Serial.println("Error: Modem initialization ERROR");
    return;
  }

  /* Connect the modem to the lte network */
  if (!lteConnect()) {
    Serial.println("Error: Could Not Connect to LTE");
    return;
  }

  /* 
   * Configure MQTT for ThingSpeak.
   */
  if (modem.mqttConfig(THINGSPEAK_MQTT_CLIENT_ID, THINGSPEAK_MQTT_USERNAME, THINGSPEAK_MQTT_PASSWORD)) {
    Serial.println("MQTT configuration succeeded");
  } else {
    Serial.println("Error: MQTT configuration failed");
    return;
  }

  /* Connect to the ThingSpeak MQTT broker */
  if (modem.mqttConnect("mqtt3.thingspeak.com", 1883)) {
    Serial.println("MQTT connection succeeded");
  } else {
    Serial.println("Error: MQTT connection failed");
  }
}




void loop() {
    //Read temperature
    float temp = temperatureRead();
    Serial.printf("Walter's SoC temp: %.2f °C\n", temp);

    //Attempt GNSS fix
    if (!attemptGNSSFix()) {
        Serial.println("Warning: Could not get GNSS fix, publishing only temp");
    }

    //Ensure LTE is connected before publishing
    if (!lteConnected() && !lteConnect()) {
        Serial.println("Error: LTE reconnect failed, skipping publish");
        delay(5000);
        return;
    }

    //Checks if MQTT is still connected and reconnects if needed
    if (!mqttConnected) {
        mqttConnected = modem.mqttConnect("mqtt3.thingspeak.com", 1883);
        if (!mqttConnected) {
            Serial.println("Error: MQTT reconnect failed, skipping publish");
            delay(5000);
            return;
        } else {
            Serial.println("MQTT connected");
        }
    }


    //Prepare MQTT message
    static char outgoingMsg[128]; // bigger buffer for multiple fields
    // If GNSS fix succeeded, include lat/lon; otherwise only temperature
    if (gnssFixRcvd) {
        sprintf(outgoingMsg, "field1=%.2f&field2=%.6f&field3=%.6f",
                temp, latestGnssFix.latitude, latestGnssFix.longitude);
    } else {
        sprintf(outgoingMsg, "field1=%.2f", temp);
    }

    Serial.printf("Going to publish '%s' to ThingSpeak\n", outgoingMsg);

    //Publish to ThingSpeak
    if (modem.mqttPublish(THINGSPEAK_TOPIC, (uint8_t*) outgoingMsg, strlen(outgoingMsg), 0)) {
        Serial.printf("Published to ThingSpeak on topic %s\n", THINGSPEAK_TOPIC);
    } else {
        Serial.println("Could not publish to ThingSpeak");
    }


    delay(15000); //15 second delay
}




