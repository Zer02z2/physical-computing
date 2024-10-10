#include <ArduinoBLE.h>

int lastTime = 0;
int lastState = 0;

const int buttonPin = 2;

BLEService controlService("00c09c59-82e4-45bf-ac98-23437e0ca62b");
BLEByteCharacteristic controlCharacteristic("00c09c59-82e4-45bf-ac98-23437e0ca62b", BLERead | BLENotify);
 
void setup() {
  pinMode(buttonPin, INPUT);
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
 
  const int reading = digitalRead(buttonPin);
  if (reading == HIGH && reading != lastState) {
    Serial.println("1");
    controlCharacteristic.writeValue(1);
    delay(200);
    lastTime = millis();
  }
  else {
    if (millis() - lastTime > 200) {
      controlCharacteristic.writeValue(0);
    }
  }
  lastState = reading;
}