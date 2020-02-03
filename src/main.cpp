#include "ESP8266WiFi.h"
#include <PubSubClient.h>
#include <CD74HC4067.h>

// Read settingd from config.h
#include "config.h"

#ifdef DEBUG
  #define DEBUG_PRINT(x) Serial.print (x)
  #define DEBUG_PRINTLN(x) Serial.println (x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// Logic switches
bool readyToUpload = false;
long lastMsg = 0;
bool initialPublish = false;

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espClient;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure espClient;

// Initialize MQTT
PubSubClient mqttClient(espClient);
// used for splitting arguments to effects
String s = String();

// Variable to store Wifi retries (required to catch some problems when i.e. the wifi ap mac address changes)
uint8_t wifiConnectionRetries = 0;

// Create mux objects
CD74HC4067 relaisMux(20, 19, 18, 17); // D1, D2, D3, D4
CD74HC4067 switchMux(5, 6, 7, 8); // D5, D6, D7, D8
const int relaisDataPin = D0; // Pin 4
const int switchDataPin = A0; // Pin 2

bool mqttReconnect() {
  // Create a client ID based on the MAC address
  String clientId = String("RetroKallaxControl") + "-";
  clientId += String(WiFi.macAddress());

  // Loop 5 times or until we're reconnected
  int counter = 0;
  while (!mqttClient.connected()) {
    counter++;
    if (counter > 5) {
      DEBUG_PRINTLN("Exiting MQTT reconnect loop");
      return false;
    }

    DEBUG_PRINT("Attempting MQTT connection...");

    // Attempt to connect
    String clientMac = WiFi.macAddress();
    char lastWillTopic[46] = "/RetroKallaxControl/lastwill/";
    strcat(lastWillTopic, clientMac.c_str());
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, lastWillTopic, 1, 1, clientMac.c_str())) {
      DEBUG_PRINTLN("connected");

      // clearing last will message
      mqttClient.publish(lastWillTopic, "", true);

      // subscribe to "all" topic
      mqttClient.subscribe("/RetroKallaxControl/all", 1);

      // subscript to the mac address (private) topic
      char topic[37];
      strcat(topic, "/RetroKallaxControl/");
      strcat(topic, clientMac.c_str());
      mqttClient.subscribe(topic, 1);
      return true;
    } else {
      DEBUG_PRINT("failed, rc=");
      DEBUG_PRINT(mqttClient.state());
      DEBUG_PRINTLN(" try again in 2 seconds");
      // Wait 1 second before retrying
      delay(1000);
    }
  }
  return false;
}

// connect to wifi
bool wifiConnect() {
  bool blinkState = true;
  wifiConnectionRetries += 1;
  int retryCounter = CONNECT_TIMEOUT * 1000;
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA); //  Force the ESP into client-only mode
  delay(1);
  DEBUG_PRINT("My Mac: ");
  DEBUG_PRINTLN(WiFi.macAddress());
  DEBUG_PRINT("Reconnecting to Wifi ");
  DEBUG_PRINT(wifiConnectionRetries);
  DEBUG_PRINT("/20 ");
  while (WiFi.status() != WL_CONNECTED) {
    retryCounter--;
    if (retryCounter <= 0) {
      DEBUG_PRINTLN(" timeout reached!");
      if (wifiConnectionRetries > 19) {
        DEBUG_PRINTLN("Wifi connection not sucessful after 20 tries. Resetting ESP8266!");
        ESP.restart();
      }
      return false;
    }
    delay(1);
  }
  DEBUG_PRINT(" done, got IP: ");
  DEBUG_PRINTLN(WiFi.localIP().toString());
  wifiConnectionRetries = 0;
  return true;
}

// logic
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  unsigned int numOfOptions = 0;
  DEBUG_PRINT("Message arrived: Topic [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] | Data [");
  for (unsigned int i = 0; i < length; i++) {
    DEBUG_PRINT((char)payload[i]);
    if ((char)payload[i] == ';') {
      numOfOptions++;
    }
  }
  DEBUG_PRINT("] - Found ");
  DEBUG_PRINT(numOfOptions);
  DEBUG_PRINTLN(" options.");

  // setting lastMsg to push the next publish cycle into the future
  lastMsg = millis();

  if ((char)payload[0] == '0') {
      // blah
  }

  //return false;
}

void setup() {
  #ifdef DEBUG
  Serial.begin(SERIAL_BAUD); // initialize serial connection
  // delay for the serial monitor to start
  delay(3000);
  #endif

  // Start the Pub/Sub client
  mqttClient.setServer(MQTT_SERVER, MQTT_SERVERPORT);
  mqttClient.setCallback(mqttCallback);

  // Set pin modes
  pinMode(relaisDataPin, OUTPUT);
  pinMode(switchDataPin, INPUT);

  // initial delay to let millis not be 0
  delay(1);

  // initial wifi connect
  wifiConnect();
}

void loop() {

  // Check if the wifi is connected
  if (WiFi.status() != WL_CONNECTED) {

    DEBUG_PRINTLN("Calling wifiConnect() as it seems to be required");
    wifiConnect();
    DEBUG_PRINTLN("My MAC: " + String(WiFi.macAddress()));
  }

  if ((WiFi.status() == WL_CONNECTED) && (!mqttClient.connected())) {
    DEBUG_PRINTLN("MQTT is not connected, let's try to reconnect");
    if (! mqttReconnect()) {
      // This should not happen, but seems to...
      DEBUG_PRINTLN("MQTT was unable to connect! Exiting the upload loop");
      // force reconnect to mqtt
      initialPublish = false;
    } else {
      // readyToUpload = true;
      DEBUG_PRINTLN("MQTT successfully reconnected");
    }
  }

  if ((WiFi.status() == WL_CONNECTED) && (!initialPublish)) {
    DEBUG_PRINT("MQTT discovery publish loop:");

    String clientMac = WiFi.macAddress(); // 17 chars
    char topic[47] = "/RetroKallaxControl/discovery/";
    strcat(topic, clientMac.c_str());

    if (mqttClient.publish(topic, VERSION, true)) {
      // Publishing values successful, removing them from cache
      DEBUG_PRINTLN(" successful");

      initialPublish = true;

    } else {
      DEBUG_PRINTLN(" FAILED!");
    }
  }

  // dummy example
  int value = 0;
  for (int i = 0; i < 16; i++) {

    switchMux.channel(i);
    relaisMux.channel(i);

    // read value
    value = analogRead(switchDataPin);

    // set same value on relais (makes no sense, because analog to digital)
    digitalWrite(relaisDataPin, value);
  }


  // calling loop at the end as proposed
  mqttClient.loop();
}
