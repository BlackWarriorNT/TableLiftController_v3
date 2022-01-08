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

String HTML_main = "";
String HTML_config = "";
const String HTML_header = "<html lang='ru'><meta http-equiv='content-type' content='text/html; charset=utf-8'>";

const String HTML_css = "<style type='text/css'>"
"html, body {margin: 0px; padding: 0px; min-height: 100%; border: 0; color: #fff;}"
"body {background: -webkit-linear-gradient(45deg, #292929 25%, transparent 25%, transparent 75%, #292929 75%), -webkit-linear-gradient(45deg, #292929 25%, transparent 25%, transparent 75%, #292929 75%) 0.1875em 0.1875em, -webkit-radial-gradient(at 50% 0, #484847, #090909);"
  "background: linear-gradient(45deg, #292929 25%, transparent 25%, transparent 75%, #292929 75%), linear-gradient(45deg, #292929 25%, transparent 25%, transparent 75%, #292929 75%) 0.1875em 0.1875em, radial-gradient(at 50% 0, #484847, #090909);"
  "background-size: 0.375em 0.375em, 0.375em 0.375em, 100% 100%;}"
".tableMain {border: 2px solid black; background-color: rgba(0, 0, 0, 0.5); margin: auto;width: 640; border-radius: 10px; margin:0 auto; padding: 0px; border-spacing: 10px;}"
".tableH {text-align: left; padding-left: 20px;}"
"th {font-weight:bold; color: rgb(253,194,91); font-size:140%;}"
"td {text-align: center; vertical-align: middle; border-radius: 10px;}"
"@keyframes glowing {0% {color: rgb(0, 249, 254);} 50% {color: rgb(68, 203, 206);} 100% {color: rgb(0, 249, 254)}}"
"button:hover, input:hover {box-shadow: 0 8px 16px 0 rgba(105,28,21,0.2), 0 6px 20px 0 rgba(105,28,21,0.19);}"
"button:active, input:active {background-color: rgba(200, 40, 40, 0.76); box-shadow: 0 5px rgba(105,28,21,0.2); transform: translateY(4px);}"
".btnArrow {width: 100px; font-weight:bold; color: rgb(253,194,91); background-color: black; font-size:400%; border-radius: 50px; border: 2px solid black;}"
".btnSelect {width: 140px; font-weight:bold; color: rgb(253,194,91); background-color: black; font-size:125%; border-radius: 50px; border: 2px solid black;}"
".heightInfo {animation: glowing 1300ms infinite; border: 1px solid black; padding: 10px; margin: 10px; color: rgb(0, 249, 254); background-color: rgba(0, 122, 125, 0.1); font-size: 750%;}"
".left {padding-left: 60px; color: rgb(0, 249, 254); font-size: 120%; text-align: left; width: 200px;}"
"input {font-weight: 700; color: rgb(0, 249, 254); background-color: rgb(9, 7, 21); border: 1px solid black}"
"</style>";

const String HTML_reboot = HTML_header + HTML_css + "<body><center><meta http-equiv=\"refresh\" content=\"30;URL='/'\"><br>Перезапуск устройства<br>Ждите<br></center></body></html>";

void handleRoot();
void handleConfig();
void setLift();
void setLiftMin();
void setLiftMax();
void setLiftMode1();
void setLiftMode2();
void handleReboot() {
  //httpServer.send(200, "text/html", HTML_reboot);
  delay(500); ESP.reset();
  }
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

void curUptime() {
  //httpServer.send(200, "text/plane", getUptime());
  }
void curVCC() {
  //httpServer.send(200, "text/plane", String(getVCC()));
  }
void curHeight() {
  //httpServer.send(200, "text/plane", String(distance*0.1, 1));
  }
void curRSSI() {
  //httpServer.send(200, "text/plane", String(WiFi.RSSI()));
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
  Serial.println(F("Table Lift Controller (ver.3) loaded"));
    Serial.print(F("\tFirmware builded: "));
    Serial.print(String(__DATE__)); 
    Serial.print(F(", "));
    Serial.println(String(__TIME__));

  if (LittleFS.begin()) {
    Serial.println(F("LittleFS.begin")); delay(250);
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

    // Route for root / web page
  httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  // Route to load style.css file
  httpServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });
  /*
  httpServer.on("/", handleRoot);
  httpServer.on("/config", handleConfig);
  httpServer.on("/reboot", handleReboot);
  httpServer.on("/json", handleJSON);
  httpServer.on("/saveParams", handleGenericArgs); //Associate the handler function to the path
    httpServer.on("/moveLift", moveLift);
    httpServer.on("/setLift", setLift);
    httpServer.on("/setLiftMin", setLiftMin);
    httpServer.on("/setLiftMax", setLiftMax);
    httpServer.on("/setLiftMode1", setLiftMode1);
    httpServer.on("/setLiftMode2", setLiftMode2);
    httpServer.on("/uptime", curUptime);
    httpServer.on("/vccread", curVCC);
    httpServer.on("/height", curHeight);
    httpServer.on("/rssi", curRSSI);
  */
  httpServer.begin();
  //httpUpdater.setup(&httpServer);
  handleJSON();
}

void loop() {
  //httpServer.handleClient();
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
void setLiftMin() {
  hNeed = hMin;
  ////httpServer.send(200, "text/html", HTML_main);
  needMove = true;
}
void setLiftMax() {
  hNeed = hMax;
  ////httpServer.send(200, "text/html", HTML_main);
  needMove = true;
}
void setLiftMode1() {
  hNeed = hMode1;
  ////httpServer.send(200, "text/html", HTML_main);
  needMove = true;
}
void setLiftMode2() {
  hNeed = hMode2;
  ////httpServer.send(200, "text/html", HTML_main);
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
  HTML_main = HTML_header + HTML_css + "<body>"
  "<table style='height: 100%; width: 100%;'><tr><td>"
      "<table class='tableMain'>"
        "<tr><th colspan='2'>Умный стол <span style='float:right; vertical-align: top; font-size: 50%;'>RSSI: <span style='float:right;' id='rssi'>RSSI</span><br>VCC: <span id='vcc'>3.00</span>V</span></th></tr>"
        "<tr>"
          "<td rowspan='2' class='heightInfo'><span id='height'>" + String(distance*0.1, 1) + "</span>см</td>"
          "<td><button class='btnArrow' onmousedown = 'moveLift(1)' onmouseup='moveLift(0)'>&#129145;</button></td>"
        "</tr>"
        "<tr>"
          "<td><button class='btnArrow' onmousedown = 'moveLift(-1)' onmouseup='moveLift(0)'>&#129147;</button></td>"
        "</tr>"
        "<tr>"
          "<td><a href='/setLift?height=" + String(hMin) + "'><button class='btnSelect' style='width: 40px;'>&#8615;</button></a>&nbsp;<a href='/setLift?height=" + String(hMode1) + "'><button class='btnSelect'>Режим&nbsp;1</button></a>&nbsp;<a href='/setLift?height=" + String(hMode2) + "'><button class='btnSelect'>Режим&nbsp;2</button></a>&nbsp;<a href='/setLift?height=" + String(hMax) + "'><button class='btnSelect' style='width: 40px;'>&#8613;</button></a></td>"
          "<td><a href='./config'><button class='btnSelect'>Настройки</button></a></td>"
        "</tr>"
      "</table>"
  "</td></tr></table>"
"</body>"
"<script>"
"setInterval(function() {getVCC();}, 2000);"
"setInterval(function() {getRSSI();}, 2500);"
"setInterval(function() {getHeight();}, 1000);"
"function moveLift(val) {"
  "var xhr = new XMLHttpRequest();"
  "xhr.open('POST', '/moveLift?height='+val, true);"
  "xhr.send();"
"}"
"function getHeight() {"
  "var xhttp = new XMLHttpRequest();"
  "xhttp.onreadystatechange = function() {"
    "if (this.readyState == 4 && this.status == 200) {"
      "document.getElementById('height').innerHTML ="
      "this.responseText;"
    "}"
  "};"
  "xhttp.open('GET', 'height', true);"
  "xhttp.send();"
"}"
"function getRSSI() {"
  "var xhttp = new XMLHttpRequest();"
  "xhttp.onreadystatechange = function() {"
    "if (this.readyState == 4 && this.status == 200) {"
      "document.getElementById('rssi').innerHTML ="
      "this.responseText;"
    "}"
  "};"
  "xhttp.open('GET', 'rssi', true);"
  "xhttp.send();"
"}"
"function getVCC() {"
  "var xhttp = new XMLHttpRequest();"
  "xhttp.onreadystatechange = function() {"
    "if (this.readyState == 4 && this.status == 200) {"
      "document.getElementById('vcc').innerHTML ="
      "this.responseText;"
    "}"
  "};"
  "xhttp.open('GET', 'vccread', true);"
  "xhttp.send();"
"}"
"</script>"
"</html>";
  //httpServer.send(200, "text/html", HTML_main);
  }

void handleConfig() {
  HTML_config = HTML_header + HTML_css + "<title>настройки</title><body>"
  "<table style='height: 100%; width: 100%;'><tr><td>"
    "<form method='POST' action='/saveParams' class='tableMain' style='border-radius: 10px 10px 5px 5px;'>"
      "<table style='margin: 10px auto 0px;'>"
          "<tr><th class='tableH'>Настройки Wi-Fi</th></tr>"
          "<tr>"
            "<td class='left'>SSID</td>"
            "<td><input type='text' name='wifiSSID' maxlength='32' size='32' placeholder='" + wifiSSID + "'></td>"
          "</tr>"
          "<tr>"
            "<td class='left'>key</td>"
            "<td><input type='text' name='wifiPSK' maxlength='32' size='32' placeholder='" + wifiPSK + "'></td>"
          "</tr>"
          "<tr><th class='tableH'>Настройки высоты</th></tr>"
          "<tr>"
            "<td class='left'>Минимальная</td>"
            "<td>"
              "<input type='text' name='hMin' maxlength='5' size='32' min='1' max='2000' step='1' placeholder='" + String(hMin) + "'>"
            "</td>"
          "</tr>"
          "<tr>"
            "<td class='left'>Максимальная</td>"
            "<td>"
              "<input type='text' name='hMax' maxlength='5' size='32' min='1' max='2000' step='1' placeholder='" + String(hMax) + "'>"
            "</td>"
          "</tr>"
          "<tr>"
            "<td class='left'>"
              "Режим №1"
            "</td>"
            "<td>"
              "<input type='text' name='hMode1' maxlength='5' size='32' min='1' max='2000' step='1' placeholder='" + String(hMode1) + "'>"
            "</td>"
          "</tr>"
          "<tr>"
            "<td class='left'>"
              "Режим №2"
            "</td>"
            "<td>"
              "<input type='text' name='hMode2' maxlength='5' size='32' min='1' max='2000' step='1' placeholder='" + String(hMode2) + "'>"
            "</td>"
          "</tr>"
          "<tr>"
            "<td class='left'>"
              "Поправка на высоту"
            "</td>"
            "<td>"
              "<input type='text' name='hOffset' maxlength='5' size='32' min='1' max='2000' step='1' placeholder='" + String(hOffset) + "'>"
            "</td>"
          "</tr>"
          "<tr>"
            "<td></td>"
            "<td><input type='submit' name='action' value='Запомнить настройки' class='btnSelect' style='width: 250px; margin: 10px 0px 10px 10px;'></td>"
          "</tr>"
      "</table>"
      "</form>"
      "<table class='tableMain' style='border-radius: 0px 0px 10px 10px; margin-top: -2px;'><tr><td><a href='/'><button class='btnSelect' style='width: 250px; margin: 10px 0px 10px 10px;'>Главная страница</button></a> <a href='./update'><button class='btnSelect' style='width: 250px; margin: 10px 0px 10px 10px;'>Обновить прошивку</button></a></td></tr></table>"
  "</td></tr></table>"
"</body>"
"</html>";
  //httpServer.send(200, "text/html", HTML_config);
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
