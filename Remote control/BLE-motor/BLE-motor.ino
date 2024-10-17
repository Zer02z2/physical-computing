#include <Servo.h>
#include <ArduinoBLE.h>

Servo myservo;

long lastTime = 0;
long lastBlinkTime = 0;
int lastBlinkState = 0;
const int ledPin = 3;

void setup() {
  pinMode(ledPin, OUTPUT);
  // initialize the serial port:
  Serial.begin(9600);
  //while (!Serial);

  myservo.attach(2);
  myservo.write(0);

  BLE.begin();
  BLE.scanForUuid("00c09c59-82e4-45bf-ac98-23437e0ca62b");
}

void loop() {
  BLEDevice peripheral = BLE.available();

  if (millis() - lastBlinkTime >= 200) {
    if (lastBlinkState == 0) lastBlinkState = 1;
    else if (lastBlinkState == 1) lastBlinkState = 0;
    digitalWrite(ledPin, lastBlinkState);
    lastBlinkTime = millis();
  }
  if (peripheral) {
    // discovered a peripheral, print out address, local name, and advertised service
    Serial.print("Connecting... ");

    // stop scanning
    BLE.stopScan();

    if (peripheral.connect()) {
      Serial.println("Connected");
    } else {
      Serial.println("Failed to connect!");
      return;
    }

    Serial.println("Discovering attributes ...");
    if (peripheral.discoverAttributes()) {
      Serial.println("Attributes discovered");
    } else {
      Serial.println("Attribute discovery failed!");
      peripheral.disconnect();
      return;
    }
    BLECharacteristic controlCharacteristic = peripheral.characteristic("00c09c59-82e4-45bf-ac98-23437e0ca62b");

    byte lastValue = 0;

    while (peripheral.connected()) {
    // while the peripheral is connected
    digitalWrite(ledPin, HIGH);
      byte value = 2;
      controlCharacteristic.readValue(value);
      Serial.println(value);
      if (value == 1 && value != lastValue) {
        myservo.write(15);
        delay(1000);
      }
      else if (value == 0 && value != lastValue) {
        myservo.write(0);
        delay(1000);
      }
      lastValue = value;
    }

    // peripheral disconnected, start scanning again
    BLE.scanForUuid("00c09c59-82e4-45bf-ac98-23437e0ca62b");
  }
  //myStepper.step(steps);
}