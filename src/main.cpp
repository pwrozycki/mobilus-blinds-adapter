#include <WiFi.h>
#include <ArduinoMqttClient.h>


void buttonOnOff(int pin);

void buttonOnOffCustomDelay(int pin1, int pin2, int msec);

bool displayEquals22();

void markSynchronizedIfBlind22Reached();

void onMqttMessage(int messageSize);

void reconnectWifiIfNeeded();

void setup_pins();

bool currentBlindKnown();

void synchronize();

void switchToBlindByNum(int num);

bool handleBlindCommand(const char *message, const char *operationPrefix, void (*callback)());

bool handleGlobalCommand(const char *message, const char *operationPrefix, void (*callback)());

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

const int DURATION_WAKING_PRESS_MS = 75;
const int DURATION_NORMAL_PRESS_MS = 35;
const int DURATION_PROGRAMMING_PRESS_MS = 2000;
const int DELAY_BETWEEN_NAV_MS = 35;

const int DURATION_DISPLAY_DIGIT_PROBE_MS = 10;

const int DELAY_ONLINE_MESSAGE_MS = 1000;
const int DELAY_RECONNECTION_RETRIAL = 10000;

const int SEGMENT_INACTIVE_MVOLTS_THRESHOLD = 2000;

/**
 * Adapt this number to maximum channel on your remote (by default 99).
 * You cannot set it below 22, because synchronization needs to display "22".
 */
const int MAX_BLINDS_ON_REMOTE = 99;

/**
 * Synchronization blind is always 22 no matter what MAX_BLINDS_ON_REMOTE setting you use.
 * It is so, because only if two digits are displaying "2" both "c" segments are off.
 */
const int SYNCHRONIZATION_BLIND = 22;

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
  markSynchronizedIfBlind22Reached();
}

bool currentBlindKnown() { return currentBlind != -1; }

void markSynchronizedIfBlind22Reached() {
  if (displayEquals22()) {
    currentBlind = SYNCHRONIZATION_BLIND;
  }
}

bool displayEquals22() {
  int now = millis();
  while (millis() < now + DURATION_DISPLAY_DIGIT_PROBE_MS) {
    int milliVolts = analogReadMilliVolts(LED_LEFT_PIN);
    if (milliVolts < SEGMENT_INACTIVE_MVOLTS_THRESHOLD) {
      return false;
    }
  }
  return true;
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

  WiFi.setHostname(HOSTNAME);
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

void resynchronize() {
  currentBlind = -1;
}

void enterLeaveBlindProgramming() {
  buttonOnOffCustomDelay(BUTTON_MID_PIN, BUTTON_UP_PIN, DURATION_PROGRAMMING_PRESS_MS);
  delay(DELAY_AFTER_COMMAND_MS);
}

void onMqttMessage(int messageSize) {
  while (mqttClient.available()) {
    char message[100];
    mqttClient.read((uint8_t *) message, 100);

    handleBlindCommand(message, "UP ", moveUp) ||
    handleBlindCommand(message, "DO ", moveDown) ||
    handleBlindCommand(message, "ST ", stop) ||
    handleBlindCommand(message, "PR ", enterLeaveBlindProgramming) ||
    handleBlindCommand(message, "NO ", nop) ||
    handleGlobalCommand(message, "SY", resynchronize);
  }
}

bool handleBlindCommand(const char *message, const char *operationPrefix, void (*callback)()) {
  if (strncasecmp(message, operationPrefix, 3) == 0) {
    int num = atoi(message + 3);
    if (num > 0 && num <= MAX_BLINDS_ON_REMOTE) {
      if (num != currentBlind) {
        switchToBlindByNum(num);
      }
      delay(DELAY_BETWEEN_NAV_MS);
      callback();
    }
    return true;
  }
  return false;
}

bool handleGlobalCommand(const char *message, const char *operationPrefix, void (*callback)()) {
  if (strncasecmp(message, operationPrefix, strlen(operationPrefix)) == 0) {
      callback();
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
    delay(DURATION_WAKING_PRESS_MS);
  } else {
    delay(DURATION_NORMAL_PRESS_MS);
  }
  lastButtonPressMillis = now;
}

void delayOnButtonPress(int msec) {
  int now = millis();
  delay(msec);
  lastButtonPressMillis = now;
}

void buttonOnOffCustomDelay(int pin1, int pin2, int msec) {
  digitalWrite(pin1, 1);
  digitalWrite(pin2, 1);
  delayOnButtonPress(msec);
  digitalWrite(pin1, 0);
  digitalWrite(pin2, 0);
}

void switchToBlindByNum(int num) {
  int delta = num - currentBlind;
  int halfOfDistance = MAX_BLINDS_ON_REMOTE / 2;
  if (abs(delta) > halfOfDistance) {
    if (delta < 0) {
      delta = delta + MAX_BLINDS_ON_REMOTE;
    } else {
      delta = delta - MAX_BLINDS_ON_REMOTE;
    }
  }

  for (int i = 0; i < abs(delta); i++) {
    if (i > 0) {
      delay(DELAY_BETWEEN_NAV_MS);
    }
    buttonOnOff(delta > 0 ? BUTTON_RIGHT_PIN : BUTTON_LEFT_PIN);
    currentBlind = num;
  }
}