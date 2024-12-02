#include "ESP32_NOW.h"
#include "WiFi.h"
#include <esp_mac.h>
#include <stdio.h>
#include <stdlib.h>
#include "RTClib.h"

// Wi-Fi interface to be used by the ESP-NOW protocol
#define ESPNOW_WIFI_IFACE WIFI_IF_STA
#define ESPNOW_WIFI_CHANNEL 2
#define ESPNOW_PEER_COUNT 1
#define ESPNOW_SEND_INTERVAL_MS 100
// Primary Master Key (PMK) and Local Master Key (LMK)
#define ESPNOW_EXAMPLE_PMK "pmk19991216"
#define ESPNOW_EXAMPLE_LMK "lmk19991216"

#define PULSE_MESSAGE 0
#define CALIBRATE_LEFT 1
#define CALIBRATE_RIGHT 2
#define CALIBRATE_CENTER 3
#define CALIBRATE_ANGLE 4

#define DEVICE_ID 0


int calibrateRightPin = 6;
int calibrateLeftPin = 5;

RTC_DS3231 rtc;
int lastMin = 0;

bool firstCalibrate = true;
bool secondCalibrate = false;
int firstCalibrateResults[ESPNOW_PEER_COUNT];
int calibrateCount = 0;

typedef struct {
  uint32_t count;
  uint32_t priority;
  uint32_t data;
  uint32_t target;
  uint32_t command;
  uint32_t id;
  uint32_t firstCal;
  uint32_t secondCal;
  uint32_t steps;
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
      recv_msg_count++;
      if (device_is_master) {
        // Serial.printf("Received a message from peer " MACSTR "\n", MAC2STR(addr()));
        if (firstCalibrate) {
          if (msg->firstCal == 1) {
            bool IDFound = false;
            for (int i = 0; i < calibrateCount; i++) {
              if (firstCalibrateResults[i] == msg->id) IDFound = true;
            }
            if (!IDFound) {
              firstCalibrateResults[calibrateCount] = msg->id;
              calibrateCount++;
            }
          }
          for (int i = 0; i < ESPNOW_PEER_COUNT; i++) {
            if (firstCalibrateResults[i] == 0) return;
          }
          firstCalibrate = false;
          secondCalibrate = true;
        } else if (secondCalibrate) {
          Serial.println(msg->secondCal);
          if (msg->secondCal == 1) {
            secondCalibrate = false;
          }
        }
      } else if (peer_is_master) {
        Serial.println("Received a message from the master");
        Serial.printf("  Average data: %lu\n", msg->data);
      } else {
        Serial.printf("Peer " MACSTR " says: %s\n", MAC2STR(addr()), msg->str);
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
  uint8_t self_mac[6];
  Serial.begin(115200);
  //while (!Serial) 1;

  pinMode(calibrateLeftPin, INPUT);
  pinMode(calibrateRightPin, INPUT);

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize the Wi-Fi module
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

  for (int i = 0; i < ESPNOW_PEER_COUNT; i++) {
    firstCalibrateResults[i] = 0;
  }

  DateTime now = rtc.now();
  lastMin = now.minute();
}

void loop() {
  if (!master_decided) {
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
      // Send a message to the master
      new_msg.count = sent_msg_count + 1;
      new_msg.data = random(10000);
      if (!master_peer->send_message((const uint8_t *)&new_msg, sizeof(new_msg))) {
        Serial.println("Failed to send message to the master");
      } else {
        Serial.printf("Sent message to the master. Count: %lu, Data: %lu\n", new_msg.count, new_msg.data);
        sent_msg_count++;
      }
    } else {
      if (firstCalibrate) {
        Serial.println("first cal");
        new_msg.command = CALIBRATE_CENTER;
        new_msg.target = 1;
        sendToPeer();
      } else if (secondCalibrate) {
        Serial.println("second cal");
        new_msg.command = CALIBRATE_ANGLE;
        new_msg.target = 1;
        DateTime now = rtc.now();
        int minute = now.minute();
        int steps = 512 * 4 / 60 * minute;
        new_msg.steps = steps;
        sendToPeer();
      } else if (digitalRead(calibrateLeftPin) == HIGH) {
        new_msg.command = CALIBRATE_LEFT;
        new_msg.target = 1;
        sendToPeer();
      } else if (digitalRead(calibrateRightPin) == HIGH) {
        new_msg.command = CALIBRATE_RIGHT;
        new_msg.target = 1;
        sendToPeer();
      } else {
        DateTime now = rtc.now();
        int minute = now.minute();
        if (minute != lastMin) {
          new_msg.command = PULSE_MESSAGE;
          new_msg.target = 1;
          lastMin = minute;
          sendToPeer();
        }
      }
    }
  }
  delay(ESPNOW_SEND_INTERVAL_MS);
}

void sendToPeer() {
  for (auto &peer : peers) {
    if (!peer->send_message((const uint8_t *)&new_msg, sizeof(new_msg))) {
      Serial.printf("Failed to send message to peer " MACSTR "\n", MAC2STR(peer->addr()));
    } else {
      // Serial.printf(
      //   "Sent message to peer " MACSTR ". Recv: %lu, Sent: %lu, Avg: %lu\n", new_msg.command, new_msg.target);
    }
  }
}

void sendMessage(int number) {
  char buffer[20];
  itoa(number, buffer, 10);

  Serial.printf("Broadcasting message: %s\n", buffer);

  if (!broadcast_peer.send_message((uint8_t *)buffer, sizeof(buffer))) {
    Serial.println("Failed to broadcast message");
  }
}
