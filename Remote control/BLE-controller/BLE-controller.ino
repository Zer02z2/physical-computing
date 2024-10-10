#include <EncoderStepCounter.h>
#include <ArduinoBLE.h>

int lastTime = 0;
 
// encoder pins:
const int pin1 = 10;
const int pin2 = 2;
const int ledPin = LED_BUILTIN;

// Create encoder instance:
EncoderStepCounter encoder(pin1, pin2);

BLEService controlService("00c09c59-82e4-45bf-ac98-23437e0ca62b");
BLEByteCharacteristic controlCharacteristic("00c09c59-82e4-45bf-ac98-23437e0ca62b", BLERead | BLENotify);
 
// encoder previous position:
int oldPosition = 0;
 
void setup() {
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

  // Initialize encoder
  encoder.begin();
}
 
void loop() {
  BLE.poll();
  encoder.tick();

  // read encoder position:
  int position = encoder.getPosition();
 
  // if there's been a change, print it:
  if (position != oldPosition) {
    if (position > oldPosition){
      Serial.println("left");
      controlCharacteristic.writeValue(0);
      lastTime = millis();
    }

    if (position < oldPosition){
      Serial.println("right");
      controlCharacteristic.writeValue(1);
      lastTime = millis();
    }
    // Serial.println(position);
    oldPosition = position;
  }
  else {
    if (millis() - lastTime > 200) {
      controlCharacteristic.writeValue(2);
    }
  }
}