#include <WiFi.h>
#include <ArduinoMqttClient.h>


void buttonOnOff(int pin);

void buttonOnOff(int pin1, int pin2);

bool displayContainsDigitTwo();

void markSynchronizedIfBlind20Reached();

void onMqttMessage(int messageSize);

void reconnectWifiIfNeeded();

void setup_pins();

bool currentBlindKnown();

void synchronize();

void switchToBlindByNum(int num);

bool handleCommand(const char *message, const char *operationPrefix, void (*callback)());

void sendOnlineMessage();

void delayOnButtonPress();

void reconnectMqttIfNeeded();

void reconnectIfNeeded();

const int LED_LEFT_PIN = 3;
const int LED_RIGHT_PIN = 5;

const int BUTTON_LEFT_PIN = 9;
const int BUTTON_DOWN_PIN = 11;
const int BUTTON_UP_PIN = 12;
const int BUTTON_RIGHT_PIN = 16;
const int BUTTON_MID_PIN = 18;

#if __has_include("secrets.h")

#include "secrets.h"

#endif

#ifndef SSID
#define SSID "SSID"
#define PASSWORD  "PASSWORD"
#define HOSTNAME  "mobilus-blinds-adapter"
#define MQTT_BROKER "BROKER_IP_ADDRESS"
#endif

const int MQTT_PORT = 1883;
const char *COMMAND_TOPIC = "mobilus/blinds/command";
const char *ONLINE_TOPIC = "mobilus/blinds/available";

const int DELAY_AFTER_COMMAND_MS = 1500;
const int DELAY_REMOTE_INACTIVE_MS = 1000;
const int DELAY_WAKING_PRESS_MS = 75;
const int DELAY_NORMAL_PRESS = 25;
const int DELAY_BETWEEN_NAV_MS = 35;

const int DELAY_ONLINE_MESSAGE_MS = 1000;
const int DELAY_RECONNECTION_RETRIAL = 10000;

const int SEGMENT_INACTIVE_MVOLTS_THRESHOLD = 2000;


// synchronization
int countOfConsecutiveTwos = 0;

// position
int currentBlind = -1;

// last <action> timestamps
int lastOnlineMessageSentMillis = -DELAY_RECONNECTION_RETRIAL;

int lastCheckedConnectionMillis = -DELAY_RECONNECTION_RETRIAL;

int lastButtonPressMillis = -DELAY_RECONNECTION_RETRIAL;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);


void setup() {
  setup_pins();
  mqttClient.onMessage(onMqttMessage);
}

void setup_pins() {
  pinMode(LED_RIGHT_PIN, INPUT);
  pinMode(LED_LEFT_PIN, INPUT);

  pinMode(BUTTON_LEFT_PIN, OUTPUT);
  pinMode(BUTTON_DOWN_PIN, OUTPUT);
  pinMode(BUTTON_UP_PIN, OUTPUT);
  pinMode(BUTTON_RIGHT_PIN, OUTPUT);
  pinMode(BUTTON_MID_PIN, OUTPUT);
}

void loop() {
  if (!currentBlindKnown()) {
    synchronize();
    return;
  }

  reconnectIfNeeded();

  if (mqttClient.connected()) {
    sendOnlineMessage();
    mqttClient.poll();
  }
}

void synchronize() {
  buttonOnOff(BUTTON_LEFT_PIN);
  delay(DELAY_BETWEEN_NAV_MS);
  markSynchronizedIfBlind20Reached();
}

bool currentBlindKnown() { return currentBlind != -1; }

void markSynchronizedIfBlind20Reached() {
  if (!displayContainsDigitTwo()) {
    countOfConsecutiveTwos = 0;
  } else {
    countOfConsecutiveTwos += 1;
  }

  if (countOfConsecutiveTwos == 10) {
    countOfConsecutiveTwos = 0;
    currentBlind = 20;
  }
}

bool displayContainsDigitTwo() {
  int segmentInactiveCount = 0;
  for (int i = 0; i < 10; i++) {
    delay(5);
    int milliVolts = analogReadMilliVolts(LED_LEFT_PIN);
    if (milliVolts > SEGMENT_INACTIVE_MVOLTS_THRESHOLD) {
      segmentInactiveCount += 1;
    }
  }
  return segmentInactiveCount > 3;
}

void reconnectIfNeeded() {
  int now = millis();
  if (now - lastCheckedConnectionMillis > DELAY_RECONNECTION_RETRIAL) {
    reconnectWifiIfNeeded();
    reconnectMqttIfNeeded();

    lastCheckedConnectionMillis = now;
  }
}

void reconnectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.begin(SSID, PASSWORD);
  for (int i = 0; i < 10; i++) {
    delay(1000);
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }
  }
}

void reconnectMqttIfNeeded() {
  bool reconnected = !mqttClient.connected() && mqttClient.connect(MQTT_BROKER, MQTT_PORT);
  if (reconnected) {
    mqttClient.subscribe(COMMAND_TOPIC);
  }
}

void sendOnlineMessage() {
  int now = millis();
  if (now - lastOnlineMessageSentMillis > DELAY_ONLINE_MESSAGE_MS) {
    mqttClient.beginMessage(ONLINE_TOPIC);
    mqttClient.print("online");
    mqttClient.endMessage();
    lastOnlineMessageSentMillis = now;
  }
}

void moveUp() {
  buttonOnOff(BUTTON_UP_PIN);
  delay(DELAY_AFTER_COMMAND_MS);
}

void moveDown() {
  buttonOnOff(BUTTON_DOWN_PIN);
  delay(DELAY_AFTER_COMMAND_MS);
}

void stop() {
  buttonOnOff(BUTTON_MID_PIN);
  delay(DELAY_AFTER_COMMAND_MS);
}

void nop() {

}

void enterLeaveBlindProgramming() {
  buttonOnOff(BUTTON_MID_PIN, BUTTON_UP_PIN);
  delay(DELAY_AFTER_COMMAND_MS);
}

void onMqttMessage(int messageSize) {
  while (mqttClient.available()) {
    char message[100];
    mqttClient.read((uint8_t *) message, 100);

    handleCommand(message, "UP ", moveUp) ||
    handleCommand(message, "DO ", moveDown) ||
    handleCommand(message, "ST ", stop) ||
    handleCommand(message, "NO ", nop) ||
    handleCommand(message, "PR ", enterLeaveBlindProgramming);
  }
}

bool handleCommand(const char *message, const char *operationPrefix, void (*callback)()) {
  if (strncasecmp(message, operationPrefix, 3) == 0) {
    int num = atoi(message + 3);
    if (num != 0) {
      switchToBlindByNum(num);
      delay(DELAY_BETWEEN_NAV_MS);
      callback();
    }
    return true;
  }
  return false;
}

void buttonOnOff(int pin) {
  digitalWrite(pin, 1);
  delayOnButtonPress();
  digitalWrite(pin, 0);
}

void delayOnButtonPress() {
  int now = millis();
  if (now - lastButtonPressMillis > DELAY_REMOTE_INACTIVE_MS) {
    delay(DELAY_WAKING_PRESS_MS);
  } else {
    delay(DELAY_NORMAL_PRESS);
  }
  lastButtonPressMillis = now;
}

void buttonOnOff(int pin1, int pin2) {
  digitalWrite(pin1, 1);
  digitalWrite(pin2, 1);
  delayOnButtonPress();
  digitalWrite(pin1, 0);
  digitalWrite(pin2, 0);
}

void switchToBlindByNum(int num) {
  int delta = num - currentBlind;
  if (delta != 0 && abs(delta) < 99) {
    for (int i = 0; i < abs(delta); i++) {
      if (i > 0) {
        delay(DELAY_BETWEEN_NAV_MS);
      }
      buttonOnOff(delta > 0 ? BUTTON_RIGHT_PIN : BUTTON_LEFT_PIN);
      currentBlind = num;
    }
  }
}