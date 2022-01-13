// https://www.mischianti.org/2020/06/22/wemos-d1-mini-esp8266-integrated-littlefs-filesystem-part-5/
// https://voltiq.ru/esp32-esp8266-web-server-physical-button/
// https://microcontrollerslab.com/esp8266-nodemcu-web-server-using-littlefs-flash-file-system/
// https://randomnerdtutorials.com/esp8266-web-server-spiffs-nodemcu/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#define trigPin 13
#define echoPin 12
#define moveDown 5
#define moveUp 4
String wifiSSID = "", wifiPSK = "";
const String espName = "TableLift";

float k = 0.1;  // коэффициент фильтрации, 0.0-1.0
float expRunningAverage(float newVal) { // бегущее среднее
  static float filVal = 0;
  filVal += (newVal - filVal) * k;
  return filVal;
}

byte needMove = false;
byte needReboot = false;
int8_t liftDirection = 0;
const unsigned int getDistancePeriod = 100;
unsigned int distance = 550, hOffset = 0, hMode1 = 550, hMode2 = 655, hMin = 500, hMax = 750, hNeed = 550;
unsigned long distanceLastTime, uptimeLastTime, snowInfoLastTime;

ADC_MODE(ADC_VCC);
AsyncWebServer httpServer(80);

unsigned int getDistance();
String getUptime();
void snowInfo();
float getVCC() {return (ESP.getVcc() * 0.001);}

void configRead();
void configWrite();

String processor(const String& var) {
  if (var == "WTIME") {return (getUptime());}
  if (var == "RSSI") {return (String(WiFi.RSSI()));}
  if (var == "VCC") {return (String(getVCC()));}
  if (var == "HEIGHT") {return (String(distance*0.1, 1));}
  if (var == "wifiSSID") {return (wifiSSID);}
  if (var == "wifiPSK") {return (wifiPSK);}
  if (var == "hMin") {return (String(hMin));}
  if (var == "hMax") {return (String(hMax));}
  if (var == "hMode1") {return (String(hMode1));}
  if (var == "hMode2") {return (String(hMode2));}
  if (var == "hOffset") {return (String(hOffset));}
  return String();
}

void moveUP () {digitalWrite(moveDown, LOW); digitalWrite(moveUp, HIGH);}
void moveDOWN () {digitalWrite(moveUp, LOW); digitalWrite(moveDown, HIGH);}
void moveSTOP () {digitalWrite(moveUp, LOW); digitalWrite(moveDown, LOW);}

void setup() {
  pinMode(moveUp, OUTPUT);
  pinMode(moveDown, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
  digitalWrite(moveDown, LOW);
  digitalWrite(moveUp, LOW);
  Serial.begin(115200);
  Serial.setTimeout(0);
  Serial.println();
  Serial.println(F("Table Lift Controller (ver.3.5) loaded"));
    Serial.print(F(" => Firmware builded: "));
    Serial.print(String(__DATE__)); 
    Serial.print(F(", "));
    Serial.println(String(__TIME__));

  if (LittleFS.begin()) {
    Serial.println(F("LittleFS.begin"));
    configRead();
  } else {
    Serial.println(F("fail."));
  }
  
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.softAP(espName);
  WiFi.hostname(espName);
  WiFi.begin("Evangelion", "97910101");
  Serial.print(F("Connecting..."));
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print(F("."));
      delay(250);
    }
  Serial.println();
  Serial.print(F("Connected to ")); Serial.println(WiFi.SSID());
  wifiSSID = WiFi.SSID(); wifiPSK = WiFi.psk();
  Serial.print(F("IP address: ")); Serial.println(WiFi.localIP());

  httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html", false, processor);
  });

  httpServer.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/config.html", "text/html", false, processor);
  });
  httpServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    needReboot = true;
  });
  httpServer.on("/reboot.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/reboot.html", "text/html", false, processor);
    needReboot = true;
  });

  httpServer.on("/uptime", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", getUptime().c_str());
    });
  httpServer.on("/rssi", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(WiFi.RSSI()).c_str());
    });
  httpServer.on("/vcc", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(getVCC()).c_str());
    });
  httpServer.on("/height", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(distance*0.1,1).c_str());
    });

  httpServer.on("/saveParams", HTTP_GET, [](AsyncWebServerRequest *request) {
    String param1 = request->getParam("wifiSSID")->value();
    String param2 = request->getParam("wifiPSK")->value();
    String param3 = request->getParam("hMin")->value();
    String param4 = request->getParam("hMax")->value();
    String param5 = request->getParam("hMode1")->value();
    String param6 = request->getParam("hMode2")->value();
    String param7 = request->getParam("hOffset")->value();
    if (param1 != "") {wifiSSID = param1;}
    if (param2 != "") {wifiPSK  = param2;}
    if (param3 != "") {hMin     = param3.toInt();}
    if (param4 != "") {hMax     = param4.toInt();}
    if (param5 != "") {hMode1   = param5.toInt();}
    if (param6 != "") {hMode2   = param6.toInt();}
    if (param7 != "") {hOffset  = param7.toInt();}
    configWrite();
    request->redirect("/reboot.html");
    needReboot = true;
  });

  httpServer.on("/moveLift", HTTP_POST, [](AsyncWebServerRequest *request) {
    String param1 = request->getParam("height")->value();
    liftDirection = param1.toInt(); needMove = false;
  });
  httpServer.on("/setLift", HTTP_GET, [](AsyncWebServerRequest *request) {
    String param1 = request->getParam("height")->value();
    hNeed = param1.toInt(); needMove = true;
    request->send(200, "text/plain", "OK");
  });
  httpServer.on("/setLiftMin", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMin; needMove = true;
    request->redirect("/");
  });
  httpServer.on("/setLiftMax", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMax; needMove = true;
    request->redirect("/");
  });
  httpServer.on("/setLiftMode1", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMode1; needMove = true;
    request->redirect("/");
  });
  httpServer.on("/setLiftMode2", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMode2; needMove = true;
    request->redirect("/");
  });

  httpServer.on("/json", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = String("height:") + String(distance) + String(";");
    json+= String("hMin:") + String(hMin) + String(";");
    json+= String("hMax:") + String(hMax) + String(";");
    json+= String("hMode1:") + String(hMode1) + String(";");
    json+= String("hMode2:") + String(hMode2) + String(";");
    json+= String("hOffset:") + String(hOffset) + String(";");
    json+= String("vcc:") + String(getVCC()) + String(";");
    request->send(200, "text/plain", json);
  });

  httpServer.serveStatic("/styles.css",   LittleFS, "/styles.css");
  httpServer.serveStatic("/favicon.ico",  LittleFS, "/favicon.ico");
  httpServer.serveStatic("/favico16.png", LittleFS, "/favico16.png");
  httpServer.serveStatic("/favico32.png", LittleFS, "/favico32.png");
  httpServer.serveStatic("/favic180.png", LittleFS, "/favic180.png");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  AsyncElegantOTA.begin(&httpServer);
  httpServer.begin();
}

void loop() {
  if (needReboot) {delay(500); ESP.reset();}
  if (needMove == false) {
  switch (liftDirection) {
    case 1:
      moveUP();
      break;
    case -1:
      moveDOWN();
      break;
    default:
      moveSTOP();
      break;
  }
  }
  if (needMove == true) {
    if (hNeed > distance) {
      moveUP();
    }
    if (hNeed < distance) {
      moveDOWN();
    }
    if (hNeed == distance) {
      needMove = false;
      moveSTOP();
    }
  }
  if (millis() - distanceLastTime >= getDistancePeriod) {
    getDistance();
  }
  if (millis() - snowInfoLastTime >= 500) {
    snowInfo();
  }
  if (millis() - uptimeLastTime >= 1000*60) {
    Serial.print(F("* Uptime: "));
    Serial.print(getUptime());
    Serial.print(F("\t| Connected to ")); Serial.print(WiFi.SSID());
    Serial.print(F("\t| IP address: ")); Serial.println(WiFi.localIP());
  }
}

void snowInfo() {
  Serial.print(F("Distance: "));
  Serial.print(distance*0.1, 1);
  Serial.print(F("\t| VCC: ")); Serial.print(getVCC()); Serial.print(F("V"));
  Serial.print(F("\t| RSSI: ")); Serial.print(String(WiFi.RSSI())); Serial.print(F(" dBi"));
  Serial.print(F("\t| FreeMem: ")); Serial.print(String(ESP.getFreeHeap())); Serial.println(F(" b"));
  snowInfoLastTime = millis();
}

unsigned int getDistance () {
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  unsigned int duration = pulseIn(echoPin, HIGH); // unsigned long
  duration = expRunningAverage(duration);
  bool useStandart = false;
  if (useStandart == true) {
    distance = duration / 58;
  } else {
    int8_t t = 29; // температура воздуха
    distance = duration * (t * 6 / 10 + 330) / 2000; // от AlexGyver: https://kit.alexgyver.ru/tutorials/hc-sr04/
  }
  distanceLastTime = millis();
  return (distance + hOffset);
}

String getUptime() {
  String uptime = ""; unsigned long time = millis()/1000;
  if (time/60/60<10) {uptime += "0";} uptime += String(time/60/60); uptime += ":";
  if (time/60%60<10) {uptime += "0";} uptime += String((time/60)%60); uptime += ":";
  if (time%60<10) {uptime += "0";} uptime += String(time%60);
  uptimeLastTime = millis();
  return uptime;
  }

void configRead() {
  bool needConfig = false;
  if (LittleFS.exists(F("/config.json"))) {
    Serial.println(F("Reading config:"));
    File configFile = LittleFS.open(F("/config.json"), "r");
    String config = configFile.readString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, config);
    configFile.close();
    if (doc == NULL) {
      needConfig = true;
    } else {
      wifiSSID = doc["ssid"].as<String>();
      wifiPSK = doc["key"].as<String>();
      hMin = doc["hMin"].as<unsigned int>();
      hMax = doc["hMax"].as<unsigned int>();
      hMode1 = doc["hMode1"].as<unsigned int>();
      hMode2 = doc["hMode2"].as<unsigned int>();
      hOffset = doc["hOffset"].as<unsigned int>();
      Serial.print(F("ssid:")); Serial.print(wifiSSID); Serial.print(F(";"));
      Serial.print(F("key:")); Serial.print(wifiPSK); Serial.print(F(";"));
      Serial.print(F("hMin:")); Serial.print(hMin); Serial.print(F(";"));
      Serial.print(F("hMax:")); Serial.print(hMax); Serial.print(F(";"));
      Serial.print(F("hMode1:")); Serial.print(hMode1); Serial.print(F(";"));
      Serial.print(F("hMode2:")); Serial.print(hMode2); Serial.print(F(";"));
      Serial.print(F("hOffset:")); Serial.print(hOffset); Serial.print(F(";"));
      Serial.println(); delay(200);
    }
  } else {
    Serial.println(F("LittleFS not exists"));
    Serial.println(F("Formating..."));
    delay(5000);
    LittleFS.format();
    needConfig = true;
    delay(2500);
    }
  if (needConfig == true) {
    Serial.println(F("Config file not found!"));
    Serial.println(F("Writing default settings..."));
    configWrite();
  }
}

void configWrite() {
  File configFile = LittleFS.open(F("/config.json"), "w");
  DynamicJsonDocument doc(1024);
  doc["ssid"] = wifiSSID;
  doc["key"] = wifiPSK;
  doc["hMin"] = hMin;
  doc["hMax"] = hMax;
  doc["hMode1"] = hMode1;
  doc["hMode2"] = hMode2;
  doc["hOffset"] = hOffset;
  Serial.println(F("Writing config: "));
  serializeJson(doc, configFile);
  configFile.print(configFile); //запись файла
  serializeJson(doc, Serial);
  configFile.close();
  Serial.println();
}