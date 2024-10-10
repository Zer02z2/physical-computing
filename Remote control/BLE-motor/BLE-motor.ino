#include <Stepper.h>
#include <ArduinoBLE.h>

const int stepsPerRevolution = 512;  // change this to fit the number of steps per revolution
// for your motor

// initialize the stepper library on pins 8 through 11:
Stepper myStepper(stepsPerRevolution, 2, 3, 4, 5);
int steps = 64;
long lastTime = 0;

void setup() {
  // initialize the serial port:
  Serial.begin(9600);
  //while (!Serial);

  BLE.begin();
  BLE.scanForUuid("00c09c59-82e4-45bf-ac98-23437e0ca62b");

  myStepper.setSpeed(50);
}

void loop() {
  BLEDevice peripheral = BLE.available();


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

    while (peripheral.connected()) {
    // while the peripheral is connected
      byte value = 2;
      controlCharacteristic.readValue(value);
      Serial.println(value);
      if (value == 1) {
        myStepper.step(steps);
      }
      else if (value == 0) {
        myStepper.step(-steps);
      }
    }

    // peripheral disconnected, start scanning again
    BLE.scanForUuid("00c09c59-82e4-45bf-ac98-23437e0ca62b");
  }
  //myStepper.step(steps);
}