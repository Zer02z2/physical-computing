#include <Stepper.h>
#include "ESP32_NOW.h"
#include "WiFi.h"
#include <esp_mac.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <Adafruit_SleepyDog.h>

// Wi-Fi interface to be used by the ESP-NOW protocol
#define ESPNOW_WIFI_IFACE WIFI_IF_STA
#define ESPNOW_WIFI_CHANNEL 2
#define ESPNOW_PEER_COUNT 6
#define ESPNOW_SEND_INTERVAL_MS 100
// Primary Master Key (PMK) and Local Master Key (LMK)
#define ESPNOW_EXAMPLE_PMK "pmk19991216"
#define ESPNOW_EXAMPLE_LMK "lmk19991216"

#define PULSE_MESSAGE 0
#define CALIBRATE_LEFT 1
#define CALIBRATE_RIGHT 2
#define CALIBRATE_CENTER 3
#define CALIBRATE_ANGLE 4
#define CALIBRATE_ANGLE 4
#define TURN_CLOCKWISE 5
#define TURN_ANTICLOCKWISE 6

#define DEVICE_ID 6

const int stepsPerRevolution = 512;
Stepper myStepper(stepsPerRevolution, 5, 6, 9, 10);
int steps = 1;

const int lightPin = A5;
bool calibrateLeft = false;
bool calibrateRight = false;
bool calibrateCenter = false;
bool calibrateAngle = false;
bool ticking = false;
bool turnClockwise = false;
bool turnAntiClockwise = false;
bool locked = true;
int stepsToCalibrate = 0;
int stepsToTurn = 0;
bool bounce = false;
long systemBootTime;
long lastAliveMessageTime;

typedef struct {
  uint32_t count;
  uint32_t priority;
  uint32_t data;
  uint32_t target;
  uint32_t command;
  uint32_t id;
  uint32_t firstCal;
  uint32_t secondCal;
  uint32_t turn;
  uint32_t steps;
  uint32_t bounce;
  bool ready;
  char str[7];
} __attribute__((packed)) esp_now_data_t;

/* Global Variables */

uint32_t self_priority = 0;      // Priority of this device
uint8_t current_peer_count = 0;  // Number of peers that have been found
bool device_is_master = false;   // Flag to indicate if this device is the master
bool master_decided = false;     // Flag to indicate if the master has been decided
uint32_t sent_msg_count = 0;     // Counter for the messages sent. Only starts counting after all peers have been found
uint32_t recv_msg_count = 0;     // Counter for the messages received. Only starts counting after all peers have been found
esp_now_data_t new_msg;          // Message that will be sent to the peers

class ESP_NOW_Network_Peer : public ESP_NOW_Peer {
public:
  uint32_t priority;
  bool peer_is_master = false;
  bool peer_ready = false;

  ESP_NOW_Network_Peer(const uint8_t *mac_addr, uint32_t priority = 0, const uint8_t *lmk = (const uint8_t *)ESPNOW_EXAMPLE_LMK)
    : ESP_NOW_Peer(mac_addr, ESPNOW_WIFI_CHANNEL, ESPNOW_WIFI_IFACE, lmk), priority(priority) {}

  ~ESP_NOW_Network_Peer() {}

  bool begin() {
    // In this example the ESP-NOW protocol will already be initialized as we require it to receive broadcast messages.
    if (!add()) {
      log_e("Failed to initialize ESP-NOW or register the peer");
      return false;
    }
    return true;
  }

  bool send_message(const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) {
      log_e("Data to be sent is NULL or has a length of 0");
      return false;
    }

    // Call the parent class method to send the data
    return send(data, len);
  }

  void onReceive(const uint8_t *data, size_t len, bool broadcast) {
    esp_now_data_t *msg = (esp_now_data_t *)data;

    if (peer_ready == false && msg->ready == true) {
      Serial.printf("Peer " MACSTR " reported ready\n", MAC2STR(addr()));
      peer_ready = true;
    }

    if (!broadcast) {
      if (device_is_master) {
        Serial.printf("Received a message from peer " MACSTR "\n", MAC2STR(addr()));
        Serial.printf("  Count: %lu\n", msg->count);
        Serial.printf("  Random data: %lu\n", msg->data);
      } else if (peer_is_master) {
        lastAliveMessageTime = millis();
        if (msg->target == DEVICE_ID) {
          Serial.println(msg->command);
          if (msg->command == TURN_CLOCKWISE) {
            calibrateCenter = false;
            calibrateLeft = false;
            calibrateRight = false;
            calibrateAngle = false;
            ticking = false;
            turnAntiClockwise = false;
            if (turnClockwise == false) {
              turnClockwise = true;
              locked = false;
            }
            stepsToTurn = msg->steps;
            bounce = msg->bounce;
          } else if (msg->command == TURN_ANTICLOCKWISE) {
            calibrateCenter = false;
            calibrateLeft = false;
            calibrateRight = false;
            calibrateAngle = false;
            ticking = false;
            turnClockwise = false;
            if (turnAntiClockwise == false) {
              turnAntiClockwise = true;
              locked = false;
            }
            stepsToTurn = msg->steps;
            bounce = msg->bounce;
          } else if (msg->command == PULSE_MESSAGE) {
            calibrateCenter = false;
            calibrateLeft = false;
            calibrateRight = false;
            calibrateAngle = false;
            ticking = true;
            locked = false;
          } else if (msg->command == CALIBRATE_CENTER) {
            if (calibrateCenter == false) {
              locked = false;
              calibrateCenter = true;
            }
          } else if (msg->command == CALIBRATE_LEFT) {
            if (calibrateLeft == false) {
              locked = false;
              calibrateLeft = true;
            }
          } else if (msg->command == CALIBRATE_RIGHT) {
            if (calibrateRight == false) {
              locked = false;
              calibrateRight = true;
            }
          } else if (msg->command == CALIBRATE_ANGLE) {
            calibrateCenter = false;
            calibrateLeft = false;
            calibrateRight = false;
            if (calibrateAngle == false) {
              locked = false;
              calibrateAngle = true;
              stepsToCalibrate = msg->steps;
              //Serial.println(stepsToCalibrate);
            }
          }
        }
      } else {
        Serial.printf("Peer " MACSTR " says: %s\n", MAC2STR(addr()), msg->str);
        new_msg.turn = 0;
        turnClockwise = false;
        turnAntiClockwise = false;
      }
    }
  }

  void onSent(bool success) {
    bool broadcast = memcmp(addr(), ESP_NOW.BROADCAST_ADDR, ESP_NOW_ETH_ALEN) == 0;
    if (broadcast) {
      log_i("Broadcast message reported as sent %s", success ? "successfully" : "unsuccessfully");
    } else {
      log_i("Unicast message reported as sent %s to peer " MACSTR, success ? "successfully" : "unsuccessfully", MAC2STR(addr()));
    }
  }
};

/* Peers */

std::vector<ESP_NOW_Network_Peer *> peers;                             // Create a vector to store the peer pointers
ESP_NOW_Network_Peer broadcast_peer(ESP_NOW.BROADCAST_ADDR, 0, NULL);  // Register the broadcast peer (no encryption support for the broadcast address)
ESP_NOW_Network_Peer *master_peer = nullptr;                           // Pointer to peer that is the master

void fail_reboot() {
  Serial.println("Rebooting in 5 seconds...");
  delay(5000);
  ESP.restart();
}

// Function to check which device has the highest priority
uint32_t check_highest_priority() {
  uint32_t highest_priority = 0;
  for (auto &peer : peers) {
    if (peer->priority > highest_priority) {
      highest_priority = peer->priority;
    }
  }
  return std::max(highest_priority, self_priority);
}

// Function to check if all peers are ready
bool check_all_peers_ready() {
  for (auto &peer : peers) {
    if (!peer->peer_ready) {
      return false;
    }
  }
  return true;
}

/* Callbacks */

// Callback called when a new peer is found
void register_new_peer(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  esp_now_data_t *msg = (esp_now_data_t *)data;
  int priority = msg->priority;

  if (priority == self_priority) {
    Serial.println("ERROR! Device has the same priority as this device. Unsupported behavior.");
    fail_reboot();
  }

  if (current_peer_count < ESPNOW_PEER_COUNT) {
    Serial.printf("New peer found: " MACSTR " with priority %d\n", MAC2STR(info->src_addr), priority);
    ESP_NOW_Network_Peer *new_peer = new ESP_NOW_Network_Peer(info->src_addr, priority);
    if (new_peer == nullptr || !new_peer->begin()) {
      Serial.println("Failed to create or register the new peer");
      delete new_peer;
      return;
    }
    peers.push_back(new_peer);
    current_peer_count++;
    if (current_peer_count == ESPNOW_PEER_COUNT) {
      Serial.println("All peers have been found");
      new_msg.ready = true;
    }
  }
}

void setup() {
  Watchdog.enable(5000);
  lastAliveMessageTime = millis();
  systemBootTime = millis();
  uint8_t self_mac[6];
  Serial.begin(115200);
  // while (!Serial) 1;
  myStepper.setSpeed(60);
  pinMode(lightPin, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
    Serial.println("WIFI.STA start failed");
  }

  Serial.println("ESP-NOw - Network Peer");
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  // Generate yhis device's priority based on the 3 last bytes of the MAC address
  WiFi.macAddress(self_mac);
  self_priority = ESPNOW_PEER_COUNT - DEVICE_ID;
  Serial.printf("This device's priority: %lu\n", self_priority);

  // Initialize the ESP-NOW protocol
  if (!ESP_NOW.begin((const uint8_t *)ESPNOW_EXAMPLE_PMK)) {
    Serial.println("Failed to initialize ESP-NOW");
    fail_reboot();
  }

  if (!broadcast_peer.begin()) {
    Serial.println("Failed to initialize broadcast peer");
    fail_reboot();
  }

  // Register the callback to be called when a new peer is found
  ESP_NOW.onNewPeer(register_new_peer, NULL);

  Serial.println("Setup complete. Broadcasting own priority to find the master...");
  memset(&new_msg, 0, sizeof(new_msg));
  strncpy(new_msg.str, "Hello!", sizeof(new_msg.str));
  new_msg.priority = self_priority;
  new_msg.id = DEVICE_ID;
}

void loop() {
  if (!master_decided) {
    if (millis() - systemBootTime > 5000) {
      Serial.println("rebooting now...");
      while (true) 1;
    }
    // Broadcast the priority to find the master
    if (!broadcast_peer.send_message((const uint8_t *)&new_msg, sizeof(new_msg))) {
      Serial.println("Failed to broadcast message");
    }

    // Check if all peers have been found
    if (current_peer_count == ESPNOW_PEER_COUNT) {
      // Wait until all peers are ready
      if (check_all_peers_ready()) {
        Serial.println("All peers are ready");
        // Check which device has the highest priority
        master_decided = true;
        uint32_t highest_priority = check_highest_priority();
        if (highest_priority == self_priority) {
          device_is_master = true;
          Serial.println("This device is the master");
        } else {
          for (int i = 0; i < ESPNOW_PEER_COUNT; i++) {
            if (peers[i]->priority == highest_priority) {
              peers[i]->peer_is_master = true;
              master_peer = peers[i];
              Serial.printf("Peer " MACSTR " is the master with priority %lu\n", MAC2STR(peers[i]->addr()), highest_priority);
              break;
            }
          }
        }
        Serial.println("The master has been decided");
      } else {
        Serial.println("Waiting for all peers to be ready...");
      }
    }
  } else {
    if (!device_is_master) {
      checkAlive();
      if (ticking) {
        Serial.println("tick stage");
        tickClock();
      } else if (calibrateCenter) {
        Serial.println("center stage");
        calibrate(0);
      } else if (calibrateLeft) {
        Serial.println("left stage");
        calibrate(-1);
      } else if (calibrateRight) {
        Serial.println("right stage");
        calibrate(1);
      } else if (calibrateAngle) {
        Serial.println("angle stage");
        calibrateToStep(stepsToCalibrate);
      } else if (turnClockwise) {
        turnToStep(1, stepsToTurn);
      } else if (turnAntiClockwise) {
        turnToStep(-1, stepsToTurn);
      } else {
        sendAliveMessage();
      }
    }
  }

  delay(ESPNOW_SEND_INTERVAL_MS);
}

void turnToStep(int direction, int steps) {
  if (locked) {
    new_msg.turn = 1;
    sendToMaster();
    return;
  }
  int currentStep = 0;
  while (currentStep < steps) {
    myStepper.step(direction);
    delay(5);
    currentStep++;
    keepAlive();
  }
  if (bounce == true) {
    currentStep = 0;
    while (currentStep < steps) {
      myStepper.step(-direction);
      delay(5);
      currentStep++;
      keepAlive();
    }
  }
  locked = true;
}

void calibrateToStep(int steps) {
  if (locked) {
    new_msg.secondCal = 1;
    sendToMaster();
    return;
  }
  int currentStep = 0;
  while (currentStep < steps) {
    myStepper.step(1);
    delay(1);
    currentStep++;
    keepAlive();
  }
  locked = true;
}

void calibrate(int direction) {
  if (locked) {
    new_msg.firstCal = 1;
    sendToMaster();
    return;
  }
  int currentStep = 0;
  int minLight = 4095;
  int stepsToMinLight = 0;
  while (currentStep < stepsPerRevolution * 4) {
    keepAlive();
    myStepper.step(1);
    delay(1);
    const int lightReading = analogRead(lightPin);
    Serial.println(lightReading);
    if (lightReading < minLight) {
      minLight = lightReading;
      stepsToMinLight = currentStep;
    }
    //Serial.println(currentStep);
    currentStep++;
  }
  Serial.println(stepsToMinLight);
  while (stepsToMinLight > 0) {
    myStepper.step(1);
    stepsToMinLight--;
  }
  int stepsToPosition = 0;
  if (direction == -1) {
    stepsToPosition = stepsPerRevolution * 3;
  } else if (direction == 1) {
    stepsToPosition = stepsPerRevolution;
  }
  while (stepsToPosition > 0) {
    myStepper.step(1);
    stepsToPosition--;
    keepAlive();
  }
  locked = true;
}

void tickClock() {
  if (locked) return;
  int remainingSteps = steps;
  while (remainingSteps > 0) {
    myStepper.step(1);
    remainingSteps--;
    keepAlive();
  }
  ticking = false;
}

void sendToMaster() {
  for (auto &peer : peers) {
    if (!master_peer->send_message((const uint8_t *)&new_msg, sizeof(new_msg))) {
      Serial.printf("Failed to send message to peer " MACSTR "\n", MAC2STR(peer->addr()));
    } else {
      // Serial.printf(
      //   "Sent message to peer " MACSTR ". Recv: %lu, Sent: %lu, Avg: %lu\n", MAC2STR(peer->addr()), new_msg.data);
    }
  }
}

void checkAlive() {
  if (millis() - lastAliveMessageTime > 5000 && millis() - lastAliveMessageTime < 0) {
    while (true) 1;
  }
  else {
    Watchdog.reset();
  }
}

void keepAlive() {
  lastAliveMessageTime = millis();
  Watchdog.reset();
}

void sendAliveMessage() {
  sendToMaster();
}