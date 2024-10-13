#include <ArduinoBLE.h>

const int upPin = 2;
const int downPin = 3;

BLEService controlService("00c09c59-82e4-45bf-ac98-23437e0ca62b");
BLEByteCharacteristic controlCharacteristic("00c09c59-82e4-45bf-ac98-23437e0ca62b", BLERead | BLENotify);

void setup() {
  pinMode(upPin, INPUT);
  pinMode(downPin, INPUT);
  Serial.begin(9600);
  //while (!Serial);

  while (!BLE.begin()) {
    Serial.println("starting Bluetooth® Low Energy module failed!");
    delay(1000);
  }
  BLE.setLocalName("AbsurdController");
  // set the UUID for the service this peripheral advertises:
  BLE.setAdvertisedService(controlService);

  // add the characteristics to the service
  controlService.addCharacteristic(controlCharacteristic);

  // add the service
  BLE.addService(controlService);

  // start advertising
  BLE.advertise();

  Serial.println("Bluetooth® device active, waiting for connections...");
}

void loop() {
  BLE.poll();

  const int upReading = digitalRead(upPin);
  const int downReading = digitalRead(downPin);

  if (upReading == HIGH) {
    Serial.println("1");
    controlCharacteristic.writeValue(1);
  } else if (downReading == HIGH) {
    Serial.println("0");
    controlCharacteristic.writeValue(0);
  } else {
    controlCharacteristic.writeValue(0);
  }
  delay(200);
}