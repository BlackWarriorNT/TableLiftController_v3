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
int8_t liftDirection;
unsigned int distance, getDistancePeriod = 100, hOffset = 0, hMode1 = 550, hMode2 = 655, hMin = 500, hMax = 750, hNeed;
unsigned long distanceLastTime, uptimeLastTime, snowInfoLastTime;

ADC_MODE(ADC_VCC);
AsyncWebServer httpServer(80);
//ESP8266HTTPUpdateServer httpUpdater;

unsigned int getDistance();
String getUptime();
void snowInfo();
float getVCC() {return (ESP.getVcc() * 0.001);}
void moveLift();

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

void handleRoot();
void handleConfig();
void setLift();
/*
void setLiftMin();
void setLiftMax();
void setLiftMode1();
void setLiftMode2();
*/
/*
void handleReboot() {
  //httpServer.send(200, "text/html", HTML_reboot);
  delay(500); ESP.reset();
  }
*/
void handleGenericArgs();

void handleJSON() {
  String json = String("height:") + String(distance) + String(";");
  json+= String("hMin:") + String(hMin) + String(";");
  json+= String("hMax:") + String(hMax) + String(";");
  json+= String("hMode1:") + String(hMode1) + String(";");
  json+= String("hMode2:") + String(hMode2) + String(";");
  json+= String("hOffset:") + String(hOffset) + String(";");
  json+= String("vcc:") + String(getVCC()) + String(";");
  Serial.println(json);
  //httpServer.send(200, "text/plain", json);
  }

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
    Serial.print(F("\tFirmware builded: "));
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
    ESP.reset();
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

  httpServer.on("/setLiftMin", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMin; needMove = true;
  });
  httpServer.on("/setLiftMax", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMax; needMove = true;
  });
  httpServer.on("/setLiftMode1", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMode1; needMove = true;
  });
  httpServer.on("/setLiftMode2", HTTP_GET, [](AsyncWebServerRequest *request) {
    hNeed = hMode2; needMove = true;
  });


  /*
  httpServer.on("/reboot.html", HTTP_GET, []() {
    request->send(LittleFS, "/reboot.html", "text/html");
    delay(500);
    ESP.reset();
  });
  */
  httpServer.serveStatic("/styles.css",   LittleFS, "/styles.css");
  httpServer.serveStatic("/favicon.ico",  LittleFS, "/favicon.ico");
  httpServer.serveStatic("/favico16.png", LittleFS, "/favico16.png");
  httpServer.serveStatic("/favico32.png", LittleFS, "/favico32.png");
  httpServer.serveStatic("/favic180.png", LittleFS, "/favic180.png");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  httpServer.begin();
}

void loop() {
  if (needMove == true) {
    if (hNeed > distance) {
      Serial.print(F("Moving up to: ")); Serial.print(String(hNeed)); Serial.print(F(", now: ")); Serial.println(String(distance));
      digitalWrite(moveDown, LOW);
      digitalWrite(moveUp, HIGH);
    }
    if (hNeed < distance) {
      Serial.print(F("Moving down to: ")); Serial.print(String(hNeed)); Serial.print(F(", now: ")); Serial.println(String(distance));
      digitalWrite(moveUp, LOW);
      digitalWrite(moveDown, HIGH);
    }
    if (hNeed == distance) {
      Serial.println(F("Moving stoped"));
      needMove = false;
      digitalWrite(moveDown, LOW);
      digitalWrite(moveUp, LOW);
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


void setLift() {
  //hNeed = httpServer.arg("height").toInt();
  //httpServer.send(200, "text/html", HTML_main);
  needMove = true;
}

void moveLift() {
  int val = 5;
  //int val = httpServer.arg("height").toInt();
  if (val > 0) {
    digitalWrite(moveDown, LOW);
    digitalWrite(moveUp, HIGH);
  } else if (val < 0) {
    digitalWrite(moveUp, LOW);
    digitalWrite(moveDown, HIGH);
  } else {
    digitalWrite(moveUp, LOW);
    digitalWrite(moveDown, LOW);
  }
}

String getUptime() {
  String uptime = ""; unsigned long time = millis()/1000;
  if (time/60/60<10) {uptime += "0";} uptime += String(time/60/60); uptime += ":";
  if (time/60%60<10) {uptime += "0";} uptime += String((time/60)%60); uptime += ":";
  if (time%60<10) {uptime += "0";} uptime += String(time%60);
  uptimeLastTime = millis();
  return uptime;
  }

void handleRoot() {
  }

void handleConfig() {
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
    //LittleFS.format();
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

void handleGenericArgs() {
  /*
  for (byte i = 0;i < httpServer.args()-1;i++) {
    String stringTemp;
    if (httpServer.argName(i) == "wifiSSID" && httpServer.arg(i) != "") {
      wifiSSID = httpServer.arg(i);
    }
    if (httpServer.argName(i) == "wifiPSK" && httpServer.arg(i) != "") {
      wifiPSK = httpServer.arg(i);
    }
    if (httpServer.argName(i) == "hMin") {
      stringTemp = httpServer.arg(i); hMin = stringTemp.toInt();
    }
    if (httpServer.argName(i) == "hMax") {
      stringTemp = httpServer.arg(i); hMax = stringTemp.toInt();
    }
    if (httpServer.argName(i) == "hMode1") {
      stringTemp = httpServer.arg(i); hMode1 = stringTemp.toInt();
    }
    if (httpServer.argName(i) == "hMode2") {
      stringTemp = httpServer.arg(i); hMode2 = stringTemp.toInt();
    }
    if (httpServer.argName(i) == "hOffset") {
      stringTemp = httpServer.arg(i); hOffset = stringTemp.toInt();
    }
  }
  configWrite();
  delay(250);
  handleReboot();
  */
}
