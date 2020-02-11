#include "ESP8266WiFi.h"
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <pcf8574_esp.h>
#include <Wire.h>

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
//long lastMsg = 0;
bool initialPublish = false;

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espClient;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure espClient;

// Initialize MQTT
PubSubClient mqttClient(espClient);

// Variable to store Wifi retries (required to catch some problems when i.e. the wifi ap mac address changes)
uint8_t wifiConnectionRetries = 0;

// SoftwareSerial for Aten connection
SoftwareSerial atenSerial(21, 22); // RX, TX

// RootTopic
char rootTopic[37];

// Set I2C addresses

/* DO NOT FORGET TO WIRE ACCORDINGLY, SDA GOES TO GPIO5, SCL TO GPIO4 (ON NODEMCU GPIO5 IS D1 AND GPIO4 IS D2) */
TwoWire triggerWire;
TwoWire relaisWire;

// Initialize a PCF8574 at I2C-address 0x20, using GPIO5, GPIO4 and testWire for the I2C-bus
int triggerAddress = 0x20;
int relaisAddress = 0x21;

PCF857x triggers(triggerAddress, &triggerWire, true);
PCF857x relais(relaisAddress, &relaisWire, true);

// Array to store the trigger states between interrupts
uint8_t triggerCache[16];

// Interrupt
volatile bool triggerInterruptFlag = false;

void triggerInterrupt() {
  triggerInterruptFlag = true;
}

void atenSendCommand(String command) {
  DEBUG_PRINT("Sending Aten serial command: ");
  DEBUG_PRINT(command)
  atenSerial.println(command);
}

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
      strcat(rootTopic, "/RetroKallaxControl/");
      strcat(rootTopic, clientMac.c_str());
      mqttClient.subscribe(rootTopic, 1);
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
  DEBUG_PRINT("Message arrived: Topic [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] | Data [");
  for (unsigned int i = 0; i < length; i++) {
    DEBUG_PRINT((char)payload[i]);
  }
  DEBUG_PRINTLN("]");

  if (strcmp(topic,"atenCommand")==0) {
    // send serial command to HDMI Switch
    atenSendCommand(String((char*)payload));
  }

  if (strcmp(topic,"relais")==0) {
    // switch relais on or off
    char channelChar[2];
    unsigned int counter = 0;
    for (unsigned int i = 0; i < length; i++) {
      if ((char)payload[i] == '/') {
        counter = 0;
        memset(channelChar, 0, sizeof channelChar);
      }
      if (counter < 2) {
        channelChar[counter] = (char)payload[i];
      }
      counter++;
    }

    if ((char)payload[0] == '1') {
      DEBUG_PRINT("Enabling relais ");
      DEBUG_PRINTLN(channelChar);
      relais.write( (int)channelChar, HIGH );
    } else {
      DEBUG_PRINT("Disabling relais ");
      DEBUG_PRINTLN(channelChar);
      relais.write( (int)channelChar, LOW );
    }
  }
}

void setup() {
  #ifdef DEBUG
  Serial.begin(SERIAL_BAUD); // initialize serial connection
  // delay for the serial monitor to start
  delay(3000);
  #endif

  // initialize Wire lib
  Wire.begin(5, 4);
  Wire.setClock(400000L);
  triggers.begin();
  relais.begin();

  // Most ready-made PCF8574-modules seem to lack an internal pullup-resistor, so you have to use the ESP8266-internal one.
  pinMode(14, INPUT_PULLUP);
  triggers.resetInterruptPin();
  attachInterrupt(digitalPinToInterrupt(14), triggerInterrupt, FALLING);

  // Setup serial port for the aten HDMI switch
  atenSerial.begin(19200);

  // Start the Pub/Sub client
  mqttClient.setServer(MQTT_SERVER, MQTT_SERVERPORT);
  mqttClient.setCallback(mqttCallback);

  // initial delay to let millis not be 0
  delay(1);

  // initial wifi connect
  wifiConnect();

  // read current trigger states and fill the cache
  for (unsigned int i = 0; i < 16; i++) {
    DEBUG_PRINTLN("Reading initial trigger states:");
    triggerCache[i] = triggers.read(i);
  }
  DEBUG_PRINTLN();
}

bool submitTrigger(uint8_t detectedTrigger) {
  if (initialPublish) {
    char triggerTopic[40];
    strcat(triggerTopic, rootTopic);
    strcat(triggerTopic, "/");
    strcat(triggerTopic, (char*)detectedTrigger);
    mqttClient.publish(triggerTopic, (char*)triggers.read(detectedTrigger), true);
    return true;
  } else {
    DEBUG_PRINTLN("Unable to send trigger to MQTT since no initial publish happend. Will retry on next interrupt.");
    return false;
  }
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
  if(triggerInterruptFlag){
    DEBUG_PRINT("Got an interrupt. Changed trigger(s):");
    for (unsigned int i = 0; i < 16; i++) {
      if (triggerCache[i] != triggers.read(i)) {
        DEBUG_PRINT(">   ");
        DEBUG_PRINT(i);
        if(triggers.read(i)==HIGH) DEBUG_PRINTLN(" is now HIGH");
        else DEBUG_PRINTLN(" is now LOW");
        if (submitTrigger(i)) triggerCache[i] = triggers.read(i);
      }

    // DO NOTE: When you write LOW to a pin on a PCF8574 it becomes an OUTPUT.
    // It wouldn't generate an interrupt if you were to connect a button to it that pulls it HIGH when you press the button.
    // Any pin you wish to use as input must be written HIGH and be pulled LOW to generate an interrupt.

    triggerInterruptFlag=false;
  }

  // calling loop at the end as proposed
  mqttClient.loop();
  }
}
