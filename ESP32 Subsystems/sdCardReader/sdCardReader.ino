#include <SPI.h>
#include <SD.h>

// define pins (pin numbers will change from these temporary values)
#define MOSI_PIN 0
#define MISO_PIN 1
#define SCK 2
#define CS 3

// define file for data
File buoyData;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    // comeback too;
  }

Serial.print("Initializing SD card"); // wait for SD card to initialize

if (!SD.begin(3)) {
  Serial.println("Initialization failed");
  while(1);
}
Serial.println("Initialization complete");

//check if file exists
if (!SD.exists("dataTextFile.csv")) {
    buoyData = SD.open("dataTextFile.csv", FILE_WRITE);

   // write to file
    if (buoyData) {
      Serial.println("Writing headers to dataTextFile.csv");
      buoyData.println("ax,ay,az,gx,gy,gz,lat,lon,temp");
      buoyData.close();
    } else {
    Serial.println("Error writing headers");
    }
  }
}

void loop() {

  sensors_event_t a, g;
  mpu.getEvent(&a, &g);
  
  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;

  float gx = g.gyro.x;
  float gy = g.gyro.y;
  float gz = g.gyro.z;

  float lat = latestGnssFix.latitude;
  float lon = latestGnssFix.longitude;
  float temp = temperatureRead();

  buoyData = SD.open("dataTextFile.csv", FILE_APPEND);

  if (buoyData) {
    Serial.println("Writing data to dataTextFile.csv");

    buoyData.print(ax);
    buoyData.print(",");

    buoyData.print(ay);
    buoyData.print(",");

    buoyData.print(az);
    buoyData.print(",");

    buoyData.print(gx);
    buoyData.print(",");

    buoyData.print(gy);
    buoyData.print(",");

    buoyData.print(gz);
    buoyData.print(",");

    buoyData.print(lat);
    buoyData.print(",");

    buoyData.print(lon);
    buoyData.print(",");

    buoyData.println(temp);
    buoyData.close();  

    } else {
    Serial.println("Error opening file");
    }

delay(2000);
}