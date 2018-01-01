#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>   // Include the WebServer library
#include <PubSubClient.h>       // For MQTT
#include <Dns.h>
#include <EEPROM.h>             // For persisiting values in EEPROM
#include <BME280I2C.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "ESP8266Ping.h"        // For ping, of course
#include "Filter.h"


// Indulge me!
#define U8  uint8_t
#define S8  int8_t
#define S16 int16_t
#define U16 uint16_t
#define S32 int32_t 
#define U32 uint32_t            // unsigned long
#define F32 float
#define F64 double


// Interval definitions
#define SECONDS 1000
#define MINUTES 60 * SECONDS
#define HOURS 60 * MINUTES
#define DAYS 24 * HOURS
#define MILLIS_TO_MICROS 1000

#define TEMPERATURE_UNIT BME280::TempUnit_Celsius
#define PRESSURE_UNIT    BME280::PresUnit_hPa

// Define this to disable MQTT
// #define DISABLE_MQTT

//////////////////////
// WiFi Definitions //
//////////////////////

#define WEB_PORT 80
ESP8266WebServer server(WEB_PORT);


// Create a new exponential filter with a weight of 5 and an initial value of 0. 
// Bigger number hew truer to the unfiltered data
ExponentialFilter<F32> TempFilter1(30, 0);
ExponentialFilter<F32> TempFilter2(20, 0);
ExponentialFilter<F32> TempFilter3(15, 0);


// Verified
// #define D0    16
// #define D1      5   // I2C Bus SCL (clock)
// #define D2      4   // I2C Bus SDA (data)
// #define D3      0
// #define D4      2   // Also blinks on-board blue LED, but with inverted logic
// #define D5    14   // SPI Bus SCK (clock)
// #define D6    12   // SPI Bus MISO 
// #define D7    13   // SPI Bus MOSI
// #define D8    15   // SPI Bus SS (CS)
// #define D9      3   // RX0 (Serial console)
// #define D10    1   // TX0 (Serial console)

// Pin layout
// Temp sensor
#define BME_SCL D1    // Green wire, SPI (Serial Clock)  5
#define BME_SDA D2    // Blue wire,  SDA (Serial Data)   4

// Shinyei sensor
#define SHINYEI_SENSOR_DIGITAL_PIN_PM10 D5 //14 // D5
#define SHINYEI_SENSOR_DIGITAL_PIN_PM25 D6 //12 // D6
#define SHINYEI_LEVEL_PIN A0 //12 // D6
#define SHARP_LED_POWER   D8 //15  // D8

// Output LEDs
#define LED_BUILTIN D4
#define LED_RED D4
#define LED_YELLOW D3
#define LED_GREEN D0

// D7 & D8 are disconnected

enum Leds {
  NONE = 0,
  RED = 1,
  YELLOW = 2,
  GREEN = 4,
};


#define SAMPLE_PERIOD_DURATION  5 * SECONDS * MILLIS_TO_MICROS//30 * SECONDS

///// 
// For persisting values in EEPROM
const int SSID_LENGTH       = 32;
const int PASSWORD_LENGTH   = 63;
const int DEVICE_KEY_LENGTH = 20;
const int URL_LENGTH        = 64;

// Our vars to hold the EEPROM values
char localSsid[SSID_LENGTH + 1];
char localPassword[PASSWORD_LENGTH + 1];
char wifiSsid[SSID_LENGTH + 1];
char wifiPassword[PASSWORD_LENGTH + 1];
char deviceToken[DEVICE_KEY_LENGTH + 1];
char mqttUrl[URL_LENGTH + 1];
U16 mqttPort;
U16 wifiChannel = 11;   // TODO: EEPROM, 0 = default, 1-13 are valid values

const int LOCAL_SSID_ADDRESS     = 0;
const int LOCAL_PASSWORD_ADDRESS = LOCAL_SSID_ADDRESS     + sizeof(localSsid);
const int WIFI_SSID_ADDRESS      = LOCAL_PASSWORD_ADDRESS + sizeof(localPassword);
const int WIFI_PASSWORD_ADDRESS  = WIFI_SSID_ADDRESS      + sizeof(wifiSsid);
const int DEVICE_KEY_ADDRESS     = WIFI_PASSWORD_ADDRESS  + sizeof(wifiPassword);
const int MQTT_URL_ADDRESS       = DEVICE_KEY_ADDRESS     + sizeof(deviceToken);
const int PUB_SUB_PORT_ADDRESS   = MQTT_URL_ADDRESS       + sizeof(mqttUrl);
const int NEXT_ADDRESS           = PUB_SUB_PORT_ADDRESS   + sizeof(mqttPort);

const int EEPROM_SIZE = NEXT_ADDRESS;

// U8 macAddress[WL_MAC_ADDR_LENGTH];
// WiFi.softAPmacAddress(macAddress);

U32 millisOveflows = 0;

const U32 WIFI_CONNECT_TIMEOUT = 20 * SECONDS;

const char *localAccessPointAddress = "192.168.1.1";    // Url a user connected by wifi would use to access the device server
const char *localGatewayAddress = "192.168.1.2";

void connectToWiFi(const char*, const char*, bool); // Forward declare
void activateLed(U32 ledMask);

StaticJsonBuffer<1024> jsonBuffer;    // Reusable buffer for sending/receiving json



void message_received_from_mothership(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Message arrived [%s]\n", topic);

  // See https://github.com/bblanchon/ArduinoJson for usage
  JsonObject &root = jsonBuffer.parseObject(payload);

  const char *color = root["LED"];

  if(strcmp(color, "GREEN") == 0)
    activateLed(GREEN);
  else if(strcmp(color, "YELLOW") == 0)
    activateLed(YELLOW);
  else if(strcmp(color, "RED") == 0)
    activateLed(RED);
  else
    activateLed(NONE);
}


WiFiClient wfclient;
PubSubClient pubSubClient(wfclient);
U32 lastPubSubConnectAttempt = 0;
bool mqttServerConfigured = false;
bool mqttServerLookupError = false;

void setupPubSubClient() {
  if(mqttServerConfigured || mqttServerLookupError )
    return;

  if(WiFi.status() != WL_CONNECTED)   // No point in doing anything here if we don't have internet access
    return;

  IPAddress serverIp;

  Serial.printf("\nLooking up IP for %s\n", mqttUrl);
  if(WiFi.hostByName(mqttUrl, serverIp)) {
    mqttSetServer(serverIp, mqttPort);
    mqttServerConfigured = true;
  } else {
    Serial.printf("Could not get IP address for MQTT server %s\n", mqttUrl);
    mqttServerLookupError = true;
  }
}


U32 pubSubConnectFailures = 0;
U32 now_micros, now_millis;

void loopPubSub() {
  setupPubSubClient();

  if(!mqttServerConfigured)
    return;

  // Ensure constant contact with the mother ship
  if(!mqttConnected()) {
    if (now_millis - lastPubSubConnectAttempt > 5 * SECONDS) {
      reconnectToPubSubServer();      // Attempt to reconnect
      lastPubSubConnectAttempt = now_millis;
      return;
    }
  }

  mqttLoop();
}


// Gets run when we're not connected to the PubSub client
void reconnectToPubSubServer() {
  if(!mqttServerConfigured)
    return;

  if(WiFi.status() != WL_CONNECTED)   // No point in doing anything here if we don't have internet access
    return;

  bool verboseMode = pubSubConnectFailures == 0;

  if(verboseMode)
    Serial.println("Attempting MQTT connection...");

  // Attempt to connect
  if (mqttConnect("Birdhouse", deviceToken, "")) {   // ClientID, username, password
    onConnectedToPubSubServer();
  } else {    // Connection failed
    if(verboseMode) {
      Serial.printf("MQTT connection failed: %s\n", getSubPubStatusName(mqttState()));

      // We know we're connected to the wifi, so let's see if we can ping the MQTT host
      bool reachable = Ping.ping(mqttUrl, 1) || Ping.ping(mqttUrl, 1) || Ping.ping(mqttUrl, 1);   // Try up to 3 pings

      if(reachable) {
        if(mqttState() == MQTT_CONNECT_UNAUTHORIZED) {
          Serial.printf("MQTT host: \"%s\" is online.\nLooks like the device token (%s) is wrong?\n", mqttUrl, deviceToken);
        } else {
          Serial.printf("MQTT host: \"%s\" is online.  Perhaps the port (%d) is wrong?\n", mqttUrl, mqttPort);
        }
      } else {
        Serial.printf("MQTT host: %s is not responding to ping.  Perhaps you've got the wrong address, or the machine is down?\n", mqttUrl);
      }
    }
    pubSubConnectFailures++;
  }
}



void mqttSetServer(IPAddress &ip, uint16_t port) {
# ifdef DISABLE_MQTT
    Serial.printf("MQTT SET SERVER: %s | %d\n", ip.toString().c_str(), port);
    return;
# else
    pubSubClient.setServer(ip, port);
# endif
}


bool mqttConnect(const char *id, const char *user, const char *pass) {
# ifdef DISABLE_MQTT
    Serial.printf("MQTT CON: %s | %s | %s\n", id, user, pass);
    return true;
# else
    return pubSubClient.connect(id, user, pass);
# endif
}


bool mqttConnected() {
# ifdef DISABLE_MQTT
    return true;
# else
    return pubSubClient.connected();
# endif
}


void mqttDisconnect() {
# ifdef DISABLE_MQTT
    Serial.printf("MQTT DIS\n");
# else
    pubSubClient.disconnect();
# endif
}


bool mqttLoop() {
# ifdef DISABLE_MQTT
    return true;
# else
    return pubSubClient.loop();
# endif
}


int mqttState() {
# ifdef DISABLE_MQTT
    return MQTT_CONNECTED;
# else
    return pubSubClient.state();
# endif
}


void mqttSetCallback(MQTT_CALLBACK_SIGNATURE) {
# ifdef DISABLE_MQTT
    Serial.printf("MQTT SETTING CALLBACK\n");
# else
    pubSubClient.setCallback(callback);
# endif
}


bool mqttPublishAttribute(const char* payload) {
  return mqttPublish("v1/devices/me/attributes", payload);
}


bool mqttPublish(const char* topic, const char* payload) {
# ifdef DISABLE_MQTT
    Serial.printf("MQTT MSG: %s | %s\n", topic, payload);
    return true;
# else
    return pubSubClient.publish_P(topic, payload, false);
# endif
}


bool mqttSubscribe(const char* topic) {
# ifdef DISABLE_MQTT
    Serial.printf("MQTT SUB: %s\n", topic);
    return true;
# else
    return pubSubClient.subscribe(topic);
# endif
}


void onConnectedToPubSubServer() {
  Serial.println("Connected to MQTT server");                          // Even if we're not in verbose mode... Victories are to be celebrated!
  mqttPublishAttribute("{'status':'Connected'}");                      // Once connected, publish an announcement...
  mqttSubscribe("v1/devices/me/attributes");                           // ... and subscribe to any shared attribute changes

  // Send some info about ourselves to the server
  sendLocalCredentialsToServer();
  sendTempSensorToServer();

  pubSubConnectFailures = 0;
}


const char *defaultPingTargetHostName = "www.google.com";


const U32 MAX_COMMAND_LENGTH = 128;
String command;     // The command the user is composing during command mode

bool changedWifiCredentials = false;    // Track if we've changed wifi connection params during command mode

void setup()
{
  Serial.begin(115200);

  Serial.println("");
  Serial.println("");
  Serial.println("   _____                            __          __ ");
  Serial.println("  / ___/___  ____  _________  _____/ /_  ____  / /_");
  Serial.println("  \\__ \\/ _ \\/ __ \\/ ___/ __ \\/ ___/ __ \\/ __ \\/ __/");
  Serial.println(" ___/ /  __/ / / (__  ) /_/ / /  / /_/ / /_/ / /_  ");
  Serial.println("/____/\\___/_/ /_/____/\\____/_/  /_.___/\\____/\\__/  ");
  Serial.println("");

# ifdef DISABLE_MQTT
    Serial.println("\n\n*** MQTT FUNCTIONALITY DISABLED IN THIS BUILD! ***\n");
# endif

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);


  activateLed(RED);
  delay(500);
  activateLed(YELLOW);
  delay(500);
  activateLed(GREEN);
  delay(500);
  activateLed(NONE);

  EEPROM.begin(EEPROM_SIZE);

  // void readStringFromEeprom(int addr, int length, char container[]);  // Forward declare

  readStringFromEeprom(LOCAL_SSID_ADDRESS,     sizeof(localSsid)     - 1, localSsid);
  readStringFromEeprom(LOCAL_PASSWORD_ADDRESS, sizeof(localPassword) - 1, localPassword);
  readStringFromEeprom(WIFI_SSID_ADDRESS,      sizeof(wifiSsid)      - 1, wifiSsid);
  readStringFromEeprom(WIFI_PASSWORD_ADDRESS,  sizeof(wifiPassword)  - 1, wifiPassword);
  readStringFromEeprom(DEVICE_KEY_ADDRESS,     sizeof(deviceToken)   - 1, deviceToken);
  readStringFromEeprom(MQTT_URL_ADDRESS,       sizeof(mqttUrl)       - 1, mqttUrl);
  mqttPort = EepromReadU16(PUB_SUB_PORT_ADDRESS);

  
  Serial.printf("Local SSID: %s\n", localSsid);
  Serial.printf("Local Password: %s\n", localPassword);
  Serial.printf("WiFi SSID: %s\n", wifiSsid);
  Serial.printf("WiFi Password: %s\n", wifiPassword);
  Serial.printf("MQTT Url: %s\n", mqttUrl);
  Serial.printf("MQTT Port: %d\n", mqttPort);

  Serial.printf("DeviceToken: %s\n", deviceToken);

  WiFi.mode(WIFI_AP_STA);  

  // We'll handle these ourselves
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);


  Serial.setDebugOutput(true);

  // WiFi.wifi_station_set_reconnect_policy(false);
  // WiFi.wifi_station_set_auto_connect(false);


  setupPubSubClient();
  mqttSetCallback(message_received_from_mothership);

  setupSensors();
 
  setupWebHandlers();
  
  command.reserve(MAX_COMMAND_LENGTH);

  setupLocalAccessPoint(localSsid, localPassword);
  connectToWiFi(wifiSsid, wifiPassword, changedWifiCredentials);

  Serial.println("Done setting up.");
}


void sendLocalCredentialsToServer() {
  JsonObject &root = jsonBuffer.createObject();
  root["localPassword"] = localPassword;
  root["localSsid"] = localSsid;

  String json;
  root.printTo(json);

  mqttPublishAttribute(json.c_str());
}


void sendTempSensorToServer() {
  JsonObject &root = jsonBuffer.createObject();
  root["temperatureSensor"] = getTemperatureSensorName();

  String json;
  root.printTo(json);

  mqttPublishAttribute(json.c_str());
}


void activateLed(U32 ledMask) {
  Serial.printf("LED Mask: %d %d %d %d\n", ledMask, (ledMask & RED) ? HIGH : LOW, (ledMask & YELLOW) ? HIGH : LOW, (ledMask & GREEN) ? HIGH : LOW);
  digitalWrite(LED_RED, (ledMask & RED) ? HIGH : LOW);
  digitalWrite(LED_YELLOW, (ledMask & YELLOW) ? HIGH : LOW);
  digitalWrite(LED_GREEN, (ledMask & GREEN) ? HIGH : LOW);
}

bool needToReconnectToWifi = false;
bool needToConnect = false;
String visitUrl = "www.yahoo.com";




bool isConnectingToWifi = false;    // True while a connection is in process

U32 connectingToWifiDotTimer;
U32 wifiConnectStartTime;
U32 lastMillis = 0;

bool needToReportScanResults = false;


U32 lastScanTime = 0;


void loop() {
  now_micros = micros();
  now_millis = millis();

  if(now_millis < lastMillis)
    millisOveflows++;
  
  lastMillis = now_millis;

  loopPubSub();
  loopSensors();

  server.handleClient();
  checkForNewInputFromSerialPort();

  if(isConnectingToWifi) {
    connectingToWifi();
  }

  if(now_millis - lastScanTime > 30 * MINUTES) {
    startScanning();
  }

  if(needToReconnectToWifi) {
    connectToWiFi(wifiSsid, wifiPassword, changedWifiCredentials);
    needToReconnectToWifi = false;
  }

  if(needToReportScanResults && WiFi.scanComplete() >= 0) {
    printScanResult();
    needToReportScanResults = false;    
  }
}


U32 lowpulseoccupancy25 = 0;
U32 lowpulseoccupancy10 = 0;




bool BME_ok = false;
bool toggle = true;


// BME280I2C::Settings settings(
//    BME280::OSR_X8,    // Temp   --> default was OSR_1X
//    BME280::OSR_X8,    // Humid  --> default was OSR_1X
//    BME280::OSR_X1,    // Press
//    BME280::Mode_Forced,   // Power mode
//    BME280::StandbyTime_1000ms, 
//    BME280::Filter_Off,    // Pressure filter
//    BME280::SpiEnable_False,
//    0x76 // I2C address. I2C specific.
// );

// Temperature sensor
BME280I2C bme;


// Orange: VCC, yellow: GND, Green: SCL, d1, Blue: SDA, d2
void setupSensors() {
  // Serial.println(getResetReason());


  // Temperature, humidity, and barometric pressure
  Wire.begin(BME_SDA, BME_SCL);
  BME_ok = bme.begin();

  if(BME_ok) {
    F32 t = bme.temp();

    TempFilter1.SetCurrent(t);
    TempFilter2.SetCurrent(t);
    TempFilter3.SetCurrent(t);

    Serial.printf("Temperature sensor: %s\n", getTemperatureSensorName());



    char str_temp[6];
    dtostrf(t, 4, 2, str_temp);

    Serial.printf("Initializing temperature filter %s\n", str_temp);
  }


  pinMode(SHINYEI_SENSOR_DIGITAL_PIN_PM10, INPUT_PULLUP);
  pinMode(SHINYEI_SENSOR_DIGITAL_PIN_PM25, INPUT_PULLUP);
  pinMode(SHINYEI_LEVEL_PIN, OUTPUT);

  analogWrite(SHINYEI_LEVEL_PIN, 1.0);  //????

  pinMode(SHARP_LED_POWER, OUTPUT);

  initializeShinyeiTimers();
}


U32 samplingPeriodStartTime = micros();

const char *getTemperatureSensorName() {
  if(!BME_ok)
    return "Error initializing temperature sensor.  No temperature or humidity data available";

  switch(bme.chipModel()) {
    case BME280::ChipModel_BME280:
      return "BME280 (temperature and humidity)";
    case BME280::ChipModel_BMP280:
      return "BMP280 (temperature, no humidity)";
    default:
      return "No temperature or humidity sensor found";
  }
}


bool triggerP1, triggerP2;
U32 durationP1, durationP2;
U32 triggerOnP1, triggerOnP2;



// Read sensors each loop tick
void loopSensors() {

  // Collect Shinyei data every loop
  bool valP1 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM10);
  bool valP2 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM25);

  if(valP1 == LOW || valP2 == LOW)  // Collecting
    Serial.print(".");


  bool doneSampling = (now_micros - samplingPeriodStartTime) > SAMPLE_PERIOD_DURATION;

  if(doneSampling) {
    // If we overshot our sampling period slightly, compute a correction
    U32 overage = (now_micros - samplingPeriodStartTime) - SAMPLE_PERIOD_DURATION;

    if(valP1 == LOW) {
      // TODO: Verify and simplify
      durationP1 += now_micros - triggerOnP1 - overage;
    }
    if(valP2 == LOW) {
      durationP2 += now_micros - triggerOnP2 - overage;
    }
  }
  else {

    // Track the duration each pin is LOW  
    // Going low... start timer
    if(valP1 == LOW && triggerP1 == LOW) {
      triggerP1 = HIGH;
      triggerOnP1 = now_micros;
      Serial.printf("P1: ON\n");
    }
    
    // Going high... stop timer
    else if (valP1 == HIGH && triggerP1 == HIGH) {
      U32 pulseLengthP1 = now_micros - triggerOnP1;
      durationP1 += pulseLengthP1;
      Serial.printf("P1: OFF, %d\n", pulseLengthP1);
      triggerP1 = LOW;
    }
    
    // Going low... start timer
    if(valP2 == LOW && triggerP2 == LOW) {
      triggerP2 = HIGH;
      triggerOnP2 = now_micros;
      Serial.printf("P2: ON\n");
    }
    
    // Going high... stop timer
    else if (valP2 == HIGH && triggerP2 == HIGH) {
      U32 pulseLengthP2 = now_micros - triggerOnP2;
      durationP2 += pulseLengthP2;
      triggerP2 = LOW;
      Serial.printf("P2: OFF, %d\n", pulseLengthP2);
    }
  }


  // https://www.elecrow.com/wiki/index.php?title=Dust_Sensor-_GP2Y1010AU0F
  // Sparp PM sensor
  // digitalWrite(SHARP_LED_POWER, LOW); // power on the LED
  // delayMicroseconds(samplingTime);
  // voMeasured = analogRead(measurePin); // read the dust value
  // delayMicroseconds(deltaTime);
  // digitalWrite(SHARP_LED_POWER, HIGH); // turn the LED off
  // delayMicroseconds(sleepTime);
  // // 0 - 3.3V mapped to 0 - 1023 integer values
  // // recover voltage
  // calcVoltage = voMeasured * (3.3 / 1024);
  // // linear eqaution taken from http://www.howmuchsnow.com/arduino/airquality/
  // // Chris Nafis (c) 2012
  // dustDensity = 0.17 * calcVoltage - 0.1;
  // Serial.print("Raw Signal Value (0-1023): ");
  // Serial.print(voMeasured);
  // Serial.print(" - Voltage: ");
  // Serial.print(calcVoltage);
  // Serial.print(" - Dust Density: ");
  // Serial.println(dustDensity);
  // delay(1000);






  if (doneSampling) {
    reportMeasurements();

    // Start the cycle anew
    samplingPeriodStartTime = micros();
  }


}


// Take any measurements we only do once per reporting period, and send all our data to the mothership
void reportMeasurements() {

    // Function creates particle count and mass concentration
    // from PPD-42 low pulse occupancy (LPO).
      
      // Generates PM10 and PM2.5 count from LPO.
      // Derived from code created by Chris Nafis
      // http://www.howmuchsnow.com/arduino/airquality/grovedust/

  Serial.printf("Durations (10/2.5): %dµs / %dµs   %s\n", durationP1, durationP2, (durationP1 > SAMPLE_PERIOD_DURATION || 
                                                                               durationP2 > SAMPLE_PERIOD_DURATION) ? "ERROR" : "");
      
      //               microseconds            milliseconds               1000      
      F32 ratioP1 = (F32)durationP1 / ((F32)SAMPLE_PERIOD_DURATION) * 100;    // Generate a percentage expressed as an integer between 0 and 100
      F32 ratioP2 = (F32)durationP2 / ((F32)SAMPLE_PERIOD_DURATION) * 100;

Serial.printf("10/2.5 ratios: %s% / %s%\n", String(ratioP1).c_str(), String(ratioP2).c_str());

      F32 countP1 = 1.1 * pow(ratioP1, 3) - 3.8 * pow(ratioP1, 2) + 520 * ratioP1 + 0.62;
      F32 countP2 = 1.1 * pow(ratioP2, 3) - 3.8 * pow(ratioP2, 2) + 520 * ratioP2 + 0.62;
      F32 PM10count = countP1;
      F32 PM25count = countP2 - countP1;
      
      // Assumes density, shape, and size of dust
      // to estimate mass concentration from particle
      // count. This method was described in a 2009
      // paper by Uva, M., Falcone, R., McClellan, A., and Ostapowicz, E.
      // http://wireless.ece.drexel.edu/research/sd_air_quality.pdf
      
      const F64 pi = 3.14159;
      const F64 K = 3531.5;
      const F64 density = 1.65 * pow(10, 12);

      // begins PM10 mass concentration algorithm
      const F64 r10 = 2.6 * pow(10, -6);
      const F64 vol10 = (4.0f / 3.0f) * pi * pow(r10, 3);
      const F64 mass10 = density * vol10;
      F32 PM10conc = PM10count * K * mass10;
      
      // next, PM2.5 mass concentration algorithm
      const F64 r25 = 0.44 * pow(10, -6);
      const F64 vol25 = (4.0 / 3.0) * pi * pow(r25, 3);
      const F64 mass25 = density * vol25;
      F32 PM25conc = PM25count * K * mass25;
      

      // TODO: Convert to arduinoJson
      String json = "{\"shinyeiPM10conc\":" + String(PM10conc) + ",\"shinyeiPM10count\":" + String(PM10count) + 
                    ",\"shinyeiPM25conc\":" + String(PM25conc) + ",\"shinyeiPM25count\":" + String(PM25count) + "}";

      bool ok = mqttPublish("v1/devices/me/telemetry", json.c_str());
      if(!ok) {
        Serial.printf("Could not publish Shinyei PM data: %s\n", json.c_str());
        Serial.printf("MQTT Status: %s\n", String(getSubPubStatusName(mqttState())).c_str());
      }

#     ifndef DISABLE_MQTT
        Serial.println(json);
#     endif


  if(BME_ok) {

    F32 pres, temp, hum;

    BME280I2C::Settings settings(
           BME280::OSR_X16,    // Temp   --> default was OSR_1X
           BME280::OSR_X16,    // Humid  --> default was OSR_1X
           BME280::OSR_X1,    // Press
           BME280::Mode_Forced,   // Power mode
           BME280::StandbyTime_1000ms, 
           BME280::Filter_Off,    // Pressure filter
           BME280::SpiEnable_False,
           0x76 // I2C address. I2C specific.
        );

    // Temperature sensor
    bme.setSettings(settings);


    bme.read(pres, temp, hum, TEMPERATURE_UNIT, PRESSURE_UNIT);

    Serial.print("Temp: ");
    Serial.print(temp);
    Serial.print("°"+ String(TEMPERATURE_UNIT == BME280::TempUnit_Celsius ? 'C' :'F'));
    Serial.print("\t\tHumidity: ");
    Serial.print(hum);
    Serial.print("% RH");
    Serial.print("\t\tPressure: ");
    Serial.print(pres);
    Serial.println(" Pa");


    // TODO: Convert to arduinoJson
    json = "{\"temperature\":" + String(temp) + ",\"humidity\":" + String(hum) + ",\"pressure\":" + String(pres) + "}";

    ok = mqttPublish("v1/devices/me/telemetry", json.c_str());
    if(!ok) {
      Serial.printf("Could not publish environmental data: %s\n", json.c_str());
    }


    BME280I2C::Settings settings2(
           BME280::OSR_X1,    // Temp   --> default was OSR_1X
           BME280::OSR_X1,    // Humid  --> default was OSR_1X
           BME280::OSR_X1,    // Press
           BME280::Mode_Forced,   // Power mode
           BME280::StandbyTime_1000ms, 
           BME280::Filter_Off,    // Pressure filter
           BME280::SpiEnable_False,
           0x76 // I2C address. I2C specific.
        );

    // Temperature sensor
    bme.setSettings(settings2);


    bme.read(pres, temp, hum, TEMPERATURE_UNIT, PRESSURE_UNIT);

    TempFilter1.Filter(temp);
    TempFilter2.Filter(temp);
    TempFilter3.Filter(temp);
   

    // TODO: Convert to arduinoJson
    json = "{\"atemperature\":" + String(TempFilter1.Current()) + ",\"btemperature\":" + String(TempFilter2.Current()) + ",\"ctemperature\":" + String(TempFilter3.Current()) + "}";
    ok = mqttPublish("v1/devices/me/telemetry", json.c_str());
    if(!ok) {
      Serial.printf("Could not publish cumulative environmental data: %s\n", json.c_str());
    }
  }

  initializeShinyeiTimers();
}


void initializeShinyeiTimers() {
  bool valP1 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM10);
  bool valP2 = digitalRead(SHINYEI_SENSOR_DIGITAL_PIN_PM25);

  durationP1 = 0;
  durationP2 = 0;
  triggerOnP1 = now_micros;
  triggerOnP2 = now_micros;
    
  triggerP1 = !valP1;
  triggerP2 = !valP2;

 Serial.printf("P1 / P2 starting %s / %s\n", valP1 ? "HIGH" : "LOW",valP2 ? "HIGH" : "LOW");
}





void copy(char *dest, const char *source, U32 destSize) {
  strncpy(dest, source, destSize);
  dest[destSize] = '\0';
}



void processConfigCommand(const String &command) {
  if(command == "uptime") {
    Serial.print("Uptime: ");
    if(millisOveflows > 0) {
      Serial.printf("%d*2^32 + ", millisOveflows);
    }
    Serial.printf("%d seconds\n", millis() / SECONDS);
  }
  else if(command.startsWith("set wifi pw")) {
    updateWifiPassword(&command.c_str()[12]);

    Serial.printf("Saved wifi pw: %s\n", wifiPassword, 123);
  }
  else if(command.startsWith("set local pw")) {
    if(strlen(&command.c_str()[13]) < 8 || strlen(&command.c_str()[13]) > sizeof(localPassword) - 1) {
      Serial.printf("Password must be between at least 8 and %d characters long; not saving.\n", sizeof(localPassword) - 1);
      return;
    }

    copy(localPassword, &command.c_str()[13], sizeof(localPassword) - 1);
    writeStringToEeprom(LOCAL_PASSWORD_ADDRESS, sizeof(localPassword) - 1, localPassword);
    pubSubConnectFailures = 0;

    sendLocalCredentialsToServer();

    Serial.printf("Saved local pw: %s\n", localPassword);
  }
  else if(command.startsWith("set wifi ssid")) {
    updateWifiSsid(&command.c_str()[14]);
  }

  #define COMMAND "use"
  else if(command.startsWith(COMMAND)) {
    int index = atoi(&command.c_str()[sizeof(COMMAND)]);
    setWifiSsidFromScanResults(index);
  }
  else if(command.startsWith("set local ssid")) {
    copy(localSsid, &command.c_str()[15], sizeof(localSsid) - 1);
    writeStringToEeprom(LOCAL_SSID_ADDRESS, sizeof(localSsid) - 1, localSsid);
    
    sendLocalCredentialsToServer();

    Serial.printf("Saved local ssid: %s\n", localSsid);
  }
  else if(command.startsWith("set device token")) {
    copy(deviceToken, &command.c_str()[17], sizeof(deviceToken) - 1);
    writeStringToEeprom(DEVICE_KEY_ADDRESS, sizeof(deviceToken) - 1, deviceToken);
    pubSubConnectFailures = 0;

    Serial.printf("Set device token: %s\n", deviceToken);
  }  
  else if(command.startsWith("set mqtt url")) {
    copy(mqttUrl, &command.c_str()[13], sizeof(mqttUrl) - 1);
    writeStringToEeprom(MQTT_URL_ADDRESS, sizeof(mqttUrl) - 1, mqttUrl);
    pubSubConnectFailures = 0;
    mqttDisconnect();
    mqttServerConfigured = false;
    mqttServerLookupError = false;

    Serial.printf("Saved mqtt URL: %s\n", mqttUrl);
    setupPubSubClient();

    // Let's immediately connect our PubSub client
    reconnectToPubSubServer();
  }
  else if(command.startsWith("set mqtt port")) {
    mqttPort = atoi(&command.c_str()[14]);
    EepromWriteU16(PUB_SUB_PORT_ADDRESS, mqttPort);
    pubSubConnectFailures = 0;
    mqttDisconnect();
    mqttServerConfigured = false;
    mqttServerLookupError = false;

    Serial.printf("Saved mqtt port: %d\n", mqttPort);
    setupPubSubClient();

    // Let's immediately connect our PubSub client
    reconnectToPubSubServer();

  }
  else if(command.startsWith("con")) {
    connectToWiFi(wifiSsid, wifiPassword, true);
  }
  else if(command.startsWith("cancel")) {
    if(isConnectingToWifi) {
      Serial.println("\nCanceled connection attempt");
      isConnectingToWifi = false;
    }
    else
      Serial.println("No connection attempt in progress");

  }
  else if(command.startsWith("stat") || command.startsWith("show")) {
    Serial.println("\n====================================");
    Serial.println("Wifi Diagnostics:");
    WiFi.printDiag(Serial); 
    Serial.printf("Wifi status: %s\n",         getWifiStatusName(WiFi.status()));
    Serial.printf("MQTT status: %s\n", getSubPubStatusName(mqttState()));
    Serial.println("====================================");
    Serial.printf("localSsid: %s\n", localSsid);
    Serial.printf("localPassword: %s\n", localPassword);
    Serial.printf("MQTT Url: %s\n", mqttUrl);
    Serial.printf("MQTT port: %d\n", mqttPort);
    Serial.printf("Device Token: %s\n", deviceToken);
    Serial.println("====================================");
    Serial.printf("Temperature sensor %s\n", BME_ok ? "OK" : "Not found");
  }
  else if(command.startsWith("scan")) {
    startScanning();
  }
  else if(command.startsWith("ping")) {
    const int commandPrefixLen = strlen("PING ");
    ping((command.length() > commandPrefixLen) ? &command.c_str()[commandPrefixLen] : defaultPingTargetHostName);
  }
  else {
    Serial.printf("Unknown command: %s\n", command.c_str());
  }
}


void startScanning() {
  if(needToReportScanResults)
    return;
  Serial.println("Scanning available networks...");
  needToReportScanResults = true;
  WiFi.scanNetworks(false, true);    // Include hidden access points
  lastScanTime = millis();
}


void updateWifiSsid(const char *ssid) {
  copy(wifiSsid, ssid, sizeof(wifiSsid) - 1);
  writeStringToEeprom(WIFI_SSID_ADDRESS, sizeof(wifiSsid) - 1, wifiSsid);
  changedWifiCredentials = true;
  initiateConnectionToWifi();

  Serial.printf("Saved wifi ssid: %s\n", wifiSsid);
}

void updateWifiPassword(const char *password) {
  copy(wifiPassword, password, sizeof(wifiPassword) - 1);
  writeStringToEeprom(WIFI_PASSWORD_ADDRESS, sizeof(wifiPassword) - 1, wifiPassword);
  changedWifiCredentials = true;
  pubSubConnectFailures = 0;
  initiateConnectionToWifi();

  Serial.printf("Saved wifi password: %s\n", wifiPassword);
}

void setWifiSsidFromScanResults(int index) {
  if(WiFi.scanComplete() == -1) {
    Serial.println("Scan running... please wait for it to complete and try again.");
    return;
  }

  if(WiFi.scanComplete() == -2) {
    Serial.println("You must run \"scan\" first!");
    return;
  }

  // Networks are 0-indexed, but user will be selecting a network based on 1-indexed display
  if(index < 1 || index > WiFi.scanComplete()) {
    Serial.printf("Invalid index: %s\n", index);
    return;
  }
  
  updateWifiSsid(WiFi.SSID(index - 1).c_str());
}


const char *getWifiStatusName(wl_status_t status) {
  return
    status == WL_NO_SHIELD        ? "NO_SHIELD" :
    status == WL_IDLE_STATUS      ? "IDLE_STATUS" :
    status == WL_NO_SSID_AVAIL    ? "NO_SSID_AVAIL" :
    status == WL_SCAN_COMPLETED   ? "SCAN_COMPLETED" :
    status == WL_CONNECTED        ? "CONNECTED" :
    status == WL_CONNECT_FAILED   ? "CONNECT_FAILED" :
    status == WL_CONNECTION_LOST  ? "CONNECTION_LOST" :
    status == WL_DISCONNECTED     ? "DISCONNECTED" :
                                    "UNKNOWN";
}


const char *getSubPubStatusName(int status) {
  return
    status == MQTT_CONNECTION_TIMEOUT      ? "CONNECTION_TIMEOUT" :       // The server didn't respond within the keepalive time
    status == MQTT_CONNECTION_LOST         ? "CONNECTION_LOST" :          // The network connection was broken
    status == MQTT_CONNECT_FAILED          ? "CONNECT_FAILED" :           // The network connection failed
    status == MQTT_DISCONNECTED            ? "DISCONNECTED" :             // The client is disconnected cleanly
    status == MQTT_CONNECTED               ? "CONNECTED" :                // The cient is connected
    status == MQTT_CONNECT_BAD_PROTOCOL    ? "CONNECT_BAD_PROTOCOL" :     // The server doesn't support the requested version of MQTT
    status == MQTT_CONNECT_BAD_CLIENT_ID   ? "CONNECT_BAD_CLIENT_ID" :    // The server rejected the client identifier
    status == MQTT_CONNECT_UNAVAILABLE     ? "CONNECT_UNAVAILABLE" :      // The server was unable to accept the connection
    status == MQTT_CONNECT_BAD_CREDENTIALS ? "CONNECT_BAD_CREDENTIALS" :  // The username/password were rejected
    status == MQTT_CONNECT_UNAUTHORIZED    ? "CONNECT_UNAUTHORIZED" :     // The client was not authorized to connect
                                             "UNKNOWN";
}


// SerialEvent occurs whenever a new data comes in the hardware serial RX. This
// routine is run between each time loop() runs, so using delay inside loop can
// delay response. Multiple bytes of data may be available.
void checkForNewInputFromSerialPort() {
  while (Serial.available()) {
    // get the new byte:
    char incomingChar = (char)Serial.read();
    // Add it to the command.
    // if the incoming character is a newline, or we're just getting too long (which should never happen) 
    // start processing the command
    if (incomingChar == '\n' || command.length() == MAX_COMMAND_LENGTH) {
      processConfigCommand(command);
      command = "";
    }
    else
      command += incomingChar;
  }
}


// Get a list of wifi hotspots the device can see
void printScanResult() {
  int networksFound = WiFi.scanComplete();
  if(networksFound < 0) {
    Serial.println("NEGATIVE RESULT!!");    // Should never happen
    return;
  }
  if (networksFound == 0) {
    Serial.println("Scan completed: No networks found");
    return;
  }

  Serial.printf("%d network(s) found\n", networksFound);

  for (int i = 0; i < networksFound; i++) {
    Serial.printf("%d: %s <<%s>>, Ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i) == "" ? "[Hidden network]" : WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
  }

  // This is the format the Google Location services uses.  We'll create a list of these packets here so they can be passed through by the microservice
  // {
  //   "macAddress": "00:25:9c:cf:1c:ac",
  //   "signalStrength": -43,
  //   "age": 0,
  //   "channel": 11,
  //   "signalToNoiseRatio": 0
  // }

// TODO: Convert to arduinoJson
  String json = "{\"visibleHotspots\":\"["; 

  for (int i = 0; i < networksFound; i++) {
    json += "{\\\"macAddress\\\":\\\"" + WiFi.BSSIDstr(i) + "\\\",\\\"signalStrength\\\":" + String(WiFi.RSSI(i)) + 
                ", \\\"age\\\": 0, \\\"channel\\\":" + String(WiFi.channel(i)) + ",\\\"signalToNoiseRatio\\\": 0 }";
    if(i < networksFound - 1)
      json += ",";
  }
  json += "]\"}";

  bool ok = mqttPublishAttribute(json.c_str());

  if(!ok) {
    Serial.printf("Could not publish message: %s\n", json.c_str());
  }
}


void ping(const char *target) {
  connectToWiFi(wifiSsid, wifiPassword, false);

  Serial.print("Pinging ");   Serial.println(target);
  U8 pingCount = 5;
  while(pingCount > 0)
  {
    if(Ping.ping(target, 1)) {
      Serial.printf("Response time: %d ms\n", Ping.averageTime());
      pingCount--;
      if(pingCount == 0)
        Serial.println("Ping complete");
    } else {
      Serial.printf("Failure pinging  %s\n", target);
      pingCount = 0;    // Cancel ping if it's not working
    }
  }
}


void contactServer(const String &url) {

  IPAddress google;
  
  if(!WiFi.hostByName(url.c_str(), google)) {
    Serial.println("Could not resolve hostname -- retrying in 5 seconds");
    delay(5000);
    return;
  }

  //  wfclient2.setTimeout(20 * 1000);
  
  WiFiClient wfclient2;
  if(wfclient2.connect(google, 80)) {
    Serial.print("connected to ");
    Serial.println(url);
    wfclient2.println("GET / HTTP/1.0");
    wfclient2.println("");
    needToConnect = false;
  }
  else {
    Serial.println("connection failed");
  }
  
//print results from google

  unsigned long timeout = millis();
  while (wfclient2.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      wfclient2.stop();
      return;
    }
  }

  while(wfclient2.available()){
    String line = wfclient2.readStringUntil('\\n');
    Serial.println(line);
  }
  for(int i = 0; i < 5; i++) {
    Serial.println("x");
  }
}


// function prototypes for HTTP handlers
void handleRoot();              
void handleLogin();
void handleNotFound();


void setupWebHandlers() {
  server.on("/", HTTP_GET, handleRoot);        // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/login", HTTP_POST, handleLogin); // Call the 'handleLogin' function when a POST request is made to URI "/login"
  server.on("/status", HTTP_GET, handleStatusRequest);
  server.onNotFound(handleNotFound);           // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"

  server.begin(); 
}

void handleStatusRequest() {
 server.send(200, "text/html","<table>"
                  "<tr><td>MQTT Url</td><td>" + String(mqttUrl) + "</td></tr>"
                  "<tr><td>MQTT Port</td><td>" + String(mqttPort) + "</td></tr>"
                  "<tr><td>WiFi status</td><td>" + String(getWifiStatusName(WiFi.status())) + "</td></tr>"
                  "<tr><td>MQTT status</td><td>" + String(getSubPubStatusName(mqttState())) + "</td></tr>"
                  "<tr><td>WiFi SSID</td><td>" + String(wifiSsid) + "</td></tr>"
                  "<tr><td>WiFi PW</td><td>" + String(wifiPassword) + "</td></tr>"
                  "<tr><td>Device Token</td><td>" + String(deviceToken) + "</td></tr>"
                  "<tr><td>Temperature Sensor</td><td>" + (BME_ok ? "OK" : "Not found") + " || " + String(BME_SCL) + " | " + String(BME_SDA) + "</tr>"
                  "</table>"
  );
}

void handleRoot() {
//  server.send(200, "text/plain", "Hello world!");

//  server.send(200, "text/html", "<form action=\\"/login\\" method=\\"POST\\"><input type=\\"text\\" name=\\"LED\\" placeholder=\\"Username\\"><input type='submit' value='Submit'></form>");
  server.send(200, "text/html", "<form action='/login' method='POST'>"
  "<input type='radio' name='LED' value = 'red'>"
  "<input type='radio' name='LED' value = 'yellow'>"
  "<input type='radio' name='LED' value = 'green'>"
  "<br>url to connect:"
  "<input type='text' name='url' value = '' size='30'>"
  "<br>wifi ssid to connect:"
  "<input type='text' name='wifiSsid' value = 'optional' size='30'>"
  "<br>wifi password to connect:"
  "<input type='text' name='wifiPassword' value = 'optional' size='30'>"
  "<input type='submit' value='Submit'></form>"); 
//  client.print(getHeader());
//  client.print("howdy");
//  client.print(getFooter());
//  delay(1);
//  Serial.println("Client disonnected");
//
//  // The client will actually be disconnected
//  // when the function returns and 'client' object is detroyed
}





void handleLogin() {
  if( ! server.hasArg("LED") || server.arg("LED") == NULL) {
    server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }

  if(server.hasArg("wifiSsid") && server.arg("wifiSsid") != "") {

    updateWifiSsid(server.arg("wifiSsid").c_str());
    needToReconnectToWifi = true; 

  }

  if(server.hasArg("wifiPassword") && server.arg("wifiPassword") != "") {

    updateWifiPassword(server.arg("wifiPassword").c_str());
  }
  

  visitUrl = server.arg("url");
  needToConnect = true;
  // activateLed(server.arg("LED"));

  server.sendHeader("Location","/");        // Add a header to respond with a new location for the browser to go to the home page again
  server.send(303);                         // Send it back to the browser with an HTTP status 303 (See Other) to redirect
  
}

void handleNotFound(){
  server.send(200, "text/plain", "Not found!! Try /");
  Serial.println("handled!");
//  client.print(getHeader());
//  client.print("see ya");
//  client.print(getFooter());
//  delay(1);
//  Serial.println("Client disonnected");
}

  // Read the first line of the request
//  String req = client.readStringUntil('\\r');
//  Serial.println(req);
//  client.flush();



//const char* getHeader() {
//  return
//    "HTTP/1.1 200 OK\\r\\n"
//    "Content-Type: text/html\\r\\n\\r\\n"
//    "<!DOCTYPE HTML>\\r\\n<html>\\r\\n";
//}
//
//const char* getFooter() {
//  return 
//    "</html>\\n";       
//}




// Called from setup
void setupLocalAccessPoint(const char *ssid, const char *password)
{
  IPAddress ip, gateway;
  WiFi.hostByName(localAccessPointAddress, ip);
  WiFi.hostByName(localGatewayAddress, gateway);

  IPAddress subnetMask(255,255,255,0);
 
  bool ok;

  ok = WiFi.softAPConfig(ip, gateway, subnetMask);
  Serial.printf("AP Config: %s\n", ok ? "OK" : "Error");
  ok = WiFi.softAP(ssid, password); //, wifiChannel, false);  // name, pw, channel, hidden
  Serial.printf("AP %s Init: %s\n", ssid, ok ? "OK" : "Error");


  // Serial.printf("AP SSID: %s\n", Wifi.soft)
  Serial.printf("AP IP Address: %s\n", WiFi.softAPIP().toString().c_str());

  const char *dnsName = "birdhouse";      // Connect with birdhouse.local

  if (MDNS.begin(dnsName)) {              // Start the mDNS responder for birdhouse.local
    Serial.printf("mDNS responder started: %s.local\n", dnsName);
  }
  else {
    Serial.println("Error setting up MDNS responder!");
  }
}


void connectToWiFi(const char *ssid, const char *password, bool disconnectIfConnected = false) {

  mqttServerLookupError = false;
  changedWifiCredentials = false;

  if(WiFi.status() == WL_CONNECTED) {   // Already connected
    if(!disconnectIfConnected)          // Don't disconnect, so nothing to do
      return;

    Serial.println("Disconnecting..."); // Otherwise... disconnect!
    WiFi.disconnect();
  }

  initiateConnectionToWifi();
}


void initiateConnectionToWifi()
{
  Serial.printf("Starting connection to %s...\n", wifiSsid);

  // set passphrase
  int status = WiFi.begin(wifiSsid, wifiPassword);
  Serial.printf("Begin status = %d, %s\n", status, getWifiStatusName((wl_status_t)status));

  if (status == WL_CONNECT_FAILED) {    // Something went wrong
    Serial.println("Failed to set ssid & pw.  Connect attempt aborted.");
  } else {                              // Status is probably WL_DISCONNECTED
    isConnectingToWifi = true;
    connectingToWifiDotTimer = millis();
    wifiConnectStartTime = millis();    
  }
}



// Will be run every loop cycle if isConnectingToWifi is true
void connectingToWifi()
{
  if(WiFi.status() == WL_CONNECTED) {   // We just connected!  :-)

    isConnectingToWifi = false;
  
    Serial.println("");
    Serial.print((millis() - wifiConnectStartTime) / SECONDS);
    Serial.println(" seconds");
    Serial.print("Connected to WiFi; Address on the LAN: "); 
    Serial.println(WiFi.localIP());         // Send the IP address of the ESP8266 to the computer
    return;
  }

  // Still not connected  :-(

  if(millis() - connectingToWifiDotTimer > 500) {
    Serial.print(".");
    connectingToWifiDotTimer = millis();
  }

  if(millis() - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT) {
    Serial.println("");
    Serial.println("Unable to connect to WiFi!");
    isConnectingToWifi = false;
  }
}




void writeStringToEeprom(int addr, int length, const char *value)
{
  for (int i = 0; i < length; i++)
    EEPROM.write(addr + i, value[i]);
        
  EEPROM.write(addr + length, '\0');
  EEPROM.commit();
}


void readStringFromEeprom(int addr, int length, char container[])
{
  for (int i = 0; i < length; i++)
    container[i] = EEPROM.read(addr + i);

  container[length] = '\0';   // Better safe than sorry!
}


// This function will write a 2 byte integer to the eeprom at the specified address and address + 1
void EepromWriteU16(int addr, U16 value)
{
  byte lowByte  = ((value >> 0) & 0xFF);
  byte highByte = ((value >> 8) & 0xFF);

  EEPROM.write(addr, lowByte);
  EEPROM.write(addr + 1, highByte);
  EEPROM.commit();
}


// This function will read a 2 byte integer from the eeprom at the specified address and address + 1
U16 EepromReadU16(int addr)
{
  byte lowByte  = EEPROM.read(addr);
  byte highByte = EEPROM.read(addr + 1);

  return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
}
