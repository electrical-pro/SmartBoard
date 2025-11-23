//Note: we use modified LiquidCrystal_I2C.h, (removed Wire.begin();) use the one provided in use_these_libs.zip
//Core 3.1.2 (updated and tested 23/11/2025)
#include <Arduino.h>
#include <ArduinoOTA.h> // for OTA
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <OneWire.h> // for DS18B20              
#include <DallasTemperature.h> // for DS18B20  3.9.0 > 4.0.5
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h> // [IMPORTANT] v2.0.0 modified lib, removed Wire.begin();, we call it from setup insted, with our pins Wire.begin(4, 0);
#include <WebSocketsServer.h>
#include <Hash.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <ArduinoJson.h> // json
#include <StreamString.h>
#include <PZEM004Tv30.h>
#include "littlefs_file_manager.h" // gziped file maneger

// url: 192.168.x.x:8089/
// file manager at 192.168.x.x:8089/littlefs content from data folder should be uploaded (format & upload)

const char* ssid = "myName";
const char* password = "myPassword";

//#define SPECIFY_MAC_ADDRESS //(enable if needed)
#ifdef SPECIFY_MAC_ADDRESS
  // MAC address of the specific AP (BSSID), if defined, it will only connect if MAC matches MAC of AP
  uint8_t bssid[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // mac
#endif

PZEM004Tv30 pzem(&Serial);

// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);

//=========ThingSpeak
const String writeAPIKey = "---APIKEY----"; // write API key for your ThingSpeak Channel
bool SendDataToServer = false;   //  set to false to NOT send data to ThingSpeak
const uint32_t ThingSpkPostInt = 60000; // set ms intervels between posts to ThingSpeak
uint32_t previousMillisThingSpeakUpdt = 0; // will store last time when was report to ThingSpeak server

ESP8266WebServer server(8089); //http
WebSocketsServer webSocketSRVR = WebSocketsServer(8092); //ws

#define BLUE_LED_PIN 2  // pin for indication
#define POWER_OFF_PIN 5 // relay, to power off the house
#define ONE_WIRE_BUS 14 //Pin to which is attached a temperature sensor
#define ONE_WIRE_MAX_DEV 10 //The maximum number of devices

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

#define IP_HISTORY_SIZE 11
String IP_History[IP_HISTORY_SIZE];
uint16_t IP_HistoryHits[IP_HISTORY_SIZE];

const uint32_t waitSenseInt = 8000; // my varible to set ms intervals to measure temperature,
uint32_t previousMillisSenseUp = 0; // will store last time when rempareture was updated

int rssi =0; // for signal level
bool need_auth = false;  // authorization
uint32_t loopsPerSec = 0;

bool displayBlink = false;  

//UptimeVaribles
uint32_t days = 0;
uint32_t hours = 0;
uint32_t mins = 0;
uint32_t secs = 0;
//UptimeVaribles

float PWR_Ctrl_tempOne = -127;
uint16_t tempOneError = 0;
uint32_t ReadingTotalOne = 0;
uint16_t DS_ErrorsLast = 0;

bool sentTempWarning = false;

float AC_Voltage = 0;
float Power = 0;
float AC_Current = 0;

uint32_t FreqExstLastUpdt = 0;
float FrequencyExst = 0;

float Frequency = 0;
float Energy = 0;
float PowerFactor = 0;

uint32_t nan_count_err = 0;

bool internetDown = false;

bool thingSpeakError = true;

// Sensor adreses (can be found at 192.168.x.x:8089/OneWireServer when connected)
DeviceAddress EXT_SENSOR1 = { 0x28, 0xb7, 0xcd, 0x99, 0x0d, 0x00, 0x00, 0x30 }; // temp sensor
// Sensor adreses


void setup(void) {
  pinMode (POWER_OFF_PIN, OUTPUT); 
  digitalWrite(POWER_OFF_PIN, LOW); 
  
  // preparing GPIOs
  pinMode (BLUE_LED_PIN, OUTPUT); // indication works as an output
  digitalWrite (BLUE_LED_PIN, LOW); // Indication pin set for low.
  
  Serial1.begin(115200); // using Serial1 for debug
  Wire.begin(4, 0); // change if needed
  Wire.setClock(400000);

  // initialize the LCD
  lcd.init();
  delay(200);
  lcd.clear();
  lcd.backlight();

  lcd_drive(0, "Starting...");

  ArduinoOTA.setHostname("PowerControlESP");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    Serial1.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial1.println("\nEnd");
    ESP.restart();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial1.printf("Progress: %u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial1.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial1.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial1.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial1.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial1.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial1.println("End Failed");
    delay(4000);
    ESP.restart();
  });
  ArduinoOTA.begin(); // for OTA

 
  // Mount LittleFS
  if (!LittleFS.begin()) {
    Serial1.println("LittleFS mount failed");
    delay(5000);
  }


  WiFi.hostname("PowerControlESP");
  delay(500);
  WiFi.mode(WIFI_STA); // STA mode.
  delay(500);
  Serial1.println("===============================");
  Serial1.println(F("WIFI_STA mode"));

  #ifdef SPECIFY_MAC_ADDRESS
    WiFi.begin(ssid, password, 0, bssid);
  #else
    WiFi.begin(ssid, password);
  #endif

  while (WiFi.status() != WL_CONNECTED && millis() < 120000) {
    delay(250);
    handleMeasurements();
    Serial1.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("\nFailed to connect. Restarting...");
    ESP.restart();
  }

  delay(500);

  // time
  configTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org");

  //Websocket
  webSocketSRVR.begin(); // WS
  webSocketSRVR.enableHeartbeat(30000, 3000, 3); // ping every 15s, timeout 3s, max 3 retries
  webSocketSRVR.onEvent(webSocketEvent);

  server.begin(); // HTTP
  Serial1.println(F("HTTP server started"));

  //=====temp sensors ======================================//
  DS18B20.begin();

  int numberOfDevices = DS18B20.getDeviceCount();
  Serial1.print(F( "Found DS18B20 Devices: " ));
  Serial1.println( numberOfDevices );

  // set resolution
  DS18B20.setResolution(EXT_SENSOR1, 12);
  // This line disables delay in the library DallasTemperature
  DS18B20.setWaitForConversion(false);
  //=====temp sensors ======================================//


  //===================ApList=============================//
  // Scans and shows a table with AP
  server.on("/listAP", []() {
    server.sendHeader("access-control-allow-origin", "*");

    String listAP_Str = "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta charset=\"utf-8\">"
    "<meta name=\"theme-color\" content=\"#042B3C\">"
    "<style>"
    "body{background-color:#000;font-family:Arial;color:#fff}"
    "table{font-family:arial,sans-serif;border-collapse:collapse;}"
    "th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}"
    "td{border:1px solid #004763;text-align:left;padding:8px}"
    "tr:nth-child(even){background-color:#0a0307}"
    "tr:nth-child(odd){background-color:#003e57}"
    "a,a:active,a:visited{color:#fff;text-decoration:none}"
    "a:hover{color:#ef0;text-decoration:none}"
    "#moreBtn{font-size:82%;border-radius:10px;text-align:center;outline:0;width:50px;color:#000;background:#08e18e;border:0}"
    ".wrapper{display:inline-block}"
    "hr{display:block;width:100%;height:0;border:0;border-top:1px solid #00a3a3;padding:0;margin:10px 0}"
    "</style></head><body>"
    "<div class=\"wrapper\">";

    int n = WiFi.scanNetworks();

    listAP_Str += "<font color=\"#ffff00\"> Networks found : ";
    listAP_Str += n;
    listAP_Str += "</font>";
    listAP_Str += "<hr>";

    listAP_Str += "<table>\n";
    listAP_Str += "<tr>\n";
    listAP_Str += "<th>SSID (NAME)</th>\n";
    listAP_Str += "<th>Signal</th>\n";
    listAP_Str += "<th>Security</th>\n";
    listAP_Str += "<th>BSSID (MAC)</th>\n";
    listAP_Str += "<th>Channel</th>\n";
    listAP_Str += "</tr>\n";
    int x = 0;
    while (x !=  n) {

      listAP_Str += "<tr>\n";

      listAP_Str += "<td>\n";
      listAP_Str += WiFi.SSID(x);
      listAP_Str += "</td>\n";

      listAP_Str += "<td>\n";
      listAP_Str += WiFi.RSSI(x);
      listAP_Str += " dBm";
      listAP_Str += "</td>\n";

      listAP_Str += "<td>\n";

      auto translateEncryptionType = [](uint8_t t) -> String {
        switch (t) {
          case ENC_TYPE_NONE: return "Open";
          case ENC_TYPE_WEP:  return "WEP";
          case ENC_TYPE_TKIP: return "WPA (TKIP)";
          case ENC_TYPE_CCMP: return "WPA2 (AES/CCMP)";
          case ENC_TYPE_AUTO: return "Auto";
          default: return "Unknown";
        }
      };

      listAP_Str += translateEncryptionType(WiFi.encryptionType(x));

      listAP_Str += "</td>\n";

      listAP_Str += "<td>\n";
      listAP_Str += WiFi.BSSIDstr(x);
      listAP_Str += "</td>\n";

      listAP_Str += "<td>\n";
      listAP_Str += WiFi.channel(x);
      listAP_Str += "</td>\n";

      listAP_Str += "</tr>\n";
      x++;
    }
    listAP_Str += "</table>";
    listAP_Str +="<hr><button id=moreBtn onclick='location.href=\"/\"'>HOME</button>";
    listAP_Str += "</div>";
    listAP_Str += "</body>\n";
    listAP_Str += "</html>";

    server.send(200, "text/html", listAP_Str);  // main page
  });
  //===================ApList===================================//


  //===================InfoList=============================//
  server.on("/info", []() {
    server.sendHeader("access-control-allow-origin", "*");
    
    String infoStr = "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta charset=\"utf-8\">"
    "<meta name=\"theme-color\" content=\"#042B3C\">"
    "<style>"
    "body{background-color:#000;font-family:Arial;color:#fff}"
    ".wrapper{display:inline-block}"
    "hr{display:block;width:100%;height:0;border:0;border-top:5px solid #570633;padding:0;margin:10px 0}"
    "table{font-family:arial,sans-serif;border-collapse:collapse;}"
    "th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}"
    "td{border:1px solid #004763;text-align:left;padding:8px}"
    "tr:nth-child(even){background-color:#0a0307}"
    "tr:nth-child(odd){background-color:#003e57}"
    "a,a:active,a:visited{color:#fff;text-decoration:none}"
    "a:hover{color:#ef0;text-decoration:none}"
    "#moreBtn{font-size:82%;border-radius:10px;text-align:center;outline:0;width:50px;color:#000;background:#08e18e;border:0}"
    "</style></head><body>"
    "<div class=\"wrapper\">"
    "<table>\n";

    
    // Helper lambda to add table rows
    auto addRow = [&](const char* label, String value) {
      infoStr += "<tr><td>" + String(label) + "</td><td>" + value + "</td></tr>\n";
    };
    
    addRow("The last reset reason:", ESP.getResetReason());
    addRow("SDK version:", ESP.getSdkVersion());
    addRow("Core version:", ESP.getCoreVersion());
    addRow("Frequency:", String(ESP.getCpuFreqMHz()) + " MHz");
    addRow("Sketch size:", String(ESP.getSketchSize()));
    addRow("Free sketch space:", String(ESP.getFreeSketchSpace()));
    addRow("Sketch MD5:", ESP.getSketchMD5());
    addRow("Flash chip size (as seen by the SDK):", String(ESP.getFlashChipSize()));
    addRow("Flash Chip Speed:", String(ESP.getFlashChipSpeed()));
    addRow("ESP.getFreeHeap():", String(ESP.getFreeHeap()));

    infoStr += "</table>";
    infoStr += "<hr><button id=moreBtn onclick='location.href=\"/\"'>HOME</button>";
    infoStr += "</body></html>";
    
    server.send(200, "text/html", infoStr);
  });
  //===================InfoList===================================//


  //===================DS_errors==============//
  server.on("/ErrorsDS", []() {
    server.sendHeader("access-control-allow-origin", "*");

    struct SensorData {
      const char* name;
      uint32_t errors;
      uint32_t readings;
    } sensors[] = {
      {"Inside / tempOne", tempOneError, ReadingTotalOne}
      // {"Outside / tempTwo", tempTwoError, ReadingTotalTwo},
      // {"Attic / tempThree", tempThreeError, ReadingTotalThree},
      // {"Basement / tempFour", tempFourError, ReadingTotalFour},
      // {"Heating / tempFive", tempFiveError, ReadingTotalFive}
    };

    uint32_t ReadingTotal = 0, ErrorTotal = 0;
    for (auto& s : sensors) {
      ReadingTotal += s.readings;
      ErrorTotal  += s.errors;
    }

    String page =
      "<!DOCTYPE html><html><head>"
      "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta name='theme-color' content='#042B3C'>"
      "<meta http-equiv='refresh' content='300'/>"
      "<style>"
      "body{background-color:#000;font-family:Arial;color:#fff}"
      "hr{border:0;background-color:#570633;height:5px;width:700px}"
      "table{font-family:arial,sans-serif;border-collapse:collapse;width:700px}"
      "th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}"
      "td{border:1px solid #004763;text-align:left;padding:8px}"
      "tr:nth-child(even){background-color:#0a0307}"
      "tr:nth-child(odd){background-color:#003e57}"
      "a,a:active,a:visited{color:#fff;text-decoration:none}"
      "a:hover{color:#ef0;text-decoration:none}"
      "</style></head><body>"
      "<table><tr><th>Sensor</th><th>Sensor Errors</th><th>Sensor Readings Total</th><th>Sensor Errors Percent</th></tr>";

    for (auto& s : sensors) {
      page += "<tr><td>" + String(s.name) + "</td><td>" + String(s.errors) + "</td><td>" + String(s.readings) + "</td><td>";
      if (s.readings != 0) {
        float percent = (float(s.errors) / float(s.readings)) * 100.0;
        page += String(percent, 2) + " %";
      } else {
        page += "0 %";
      }
      page += "</td></tr>";
    }

    page += "</table><hr align='left'>";
    page += "<li>Reading times Total: " + String(ReadingTotal) + "</li>";
    page += "<li>Errors Total: " + String(ErrorTotal) + "</li>";
    if (ReadingTotal != 0) {
      float percent = (float(ErrorTotal) / float(ReadingTotal)) * 100.0;
      page += "<li>Errors Percent Total: " + String(percent, 2) + " %</li>";
    }

    page += "<hr align='left'><li>Uptime: " + String(days) + " Days, " + String(hours) + " Hours, " + String(mins) + " Min, " + String(secs) + " Sec.</li>";
    page += "</body></html>";

    server.send(200, "text/html", page);
  });
  //===================DS_errors==============//
  

  //===================IP_HISTORY==============//
  server.on("/IP_History", []() {
    server.sendHeader("access-control-allow-origin", "*");
    String ipList = F("<style>body { background-color: #000000; font-family: Arial; Color: white;}</style>\n");
    ipList += F("<meta name=\"theme-color\" content=\"#042B3C\" />");
    ipList += F("<meta http-equiv='refresh' content='300'/>"); //auto apdate web page
    ipList += F("IP ACCESS LIST");
    ipList += F("<br/>");
    ipList += F("===============================================");
    ipList += F("<br/>");
    for (int x = 0; IP_History[x].length() > 3; x++) {
      ipList += IP_History[x];
      ipList += " | Hits: ";
      ipList += IP_HistoryHits[x];
      ipList += F("<br/>");
    }
    server.send(200, "text/html", ipList);
  });
  //===================IP_HISTORY==============//



  //===================PublicAccess==============//
  server.on("/PublicAccessTrue", []() {
    server.sendHeader("access-control-allow-origin", "*");
    server.send(200, "text/html", "<p>PublicAccess: enabled!</p>");
    need_auth = false;
  });
  server.on("/PublicAccessFalse", []() {
    server.sendHeader("access-control-allow-origin", "*");
    server.send(200, "text/html", "<p>PublicAccess: disabled!</p>");
    need_auth = true;
  });
  //===================PublicAccess==============//


  //===================SendDataToServer==============//
  server.on("/SendDataToServerTrue", []() {
    server.sendHeader("access-control-allow-origin", "*");
    server.send(200, "text/html", "<p>SendData: Allowed!</p>");
    SendDataToServer = true;
  });
  server.on("/SendDataToServerFalse", []() {
    server.sendHeader("access-control-allow-origin", "*");
    server.send(200, "text/html", "<p>SendData: Disallowed!</p>");
    SendDataToServer = false;
  });
  //===================SendDataToServer==============//

  //These 3 lines tell esp to collect User-Agent and Cookie in http header when request is made
  const char * headerkeys[] = {"User-Agent", "Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  server.collectHeaders(headerkeys, headerkeyssize );
  //These 3 lines tell esp to collect User-Agent and Cookie in http header when request is made

  //========================FileServer======================================================//
  server.onNotFound([]() {
    server.sendHeader("Access-Control-Allow-Origin", "*");

    String path = server.uri();
    if (path == "/") path = "/control_WS_css_V2_6.html"; // for root
    
    // Prefer gzipped version if available
    String gzPath = path + ".gz";
    if (LittleFS.exists(gzPath)) {
      path = gzPath;
    } else if (!LittleFS.exists(path)) {
      server.send(404, "text/plain", "404: Not Found");
      IP_addToList(server.client().remoteIP().toString() + " | " + server.uri() + " | Not Found !");
      return;
    }

    // Check authorization
    if(need_auth){
      String sessionToken = "111v6566v3c363498y6g3qz";
      if (!server.hasHeader("Cookie")) {
        server.send(401, "text/html", "File exists, but you are not authorized!");
        IP_addToList(server.client().remoteIP().toString() + " | " + server.uri() + " | Not authorized !");
        return;
      }
      
      String cookie = server.header("Cookie");
      if (cookie.indexOf(sessionToken) == -1) {
        server.send(403, "text/html", "File exists, but your cookie is not valid!");
        IP_addToList(server.client().remoteIP().toString() + " | " + server.uri() + " | Not valid cookie !");
        return;
      }
    }
    
    // Determine content type
    String uri = path;
    if (uri.endsWith(".gz")) uri = uri.substring(0, uri.length() - 3); //remove .gz so we give correct contentType
    
    String contentType = "text/plain";
    if (uri.endsWith(".htm") || uri.endsWith(".html")) contentType = "text/html";
    else if (uri.endsWith(".css")) contentType = "text/css";
    else if (uri.endsWith(".js")) contentType = "application/javascript";
    else if (uri.endsWith(".png")) contentType = "image/png";
    else if (uri.endsWith(".gif")) contentType = "image/gif";
    else if (uri.endsWith(".jpg")) contentType = "image/jpeg";
    else if (uri.endsWith(".ico")) contentType = "image/x-icon";
    else if (uri.endsWith(".xml")) contentType = "text/xml";
    else if (uri.endsWith(".pdf")) contentType = "application/x-pdf";
    else if (uri.endsWith(".zip")) contentType = "application/x-zip";
    
    File file = LittleFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
  });


  //========================FileManager======================================================//
  server.on("/littlefs", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)littleFS_html, sizeof(littleFS_html));
  });

  // List files endpoint
  server.on("/list", HTTP_GET, []() {
    Dir dir = LittleFS.openDir("/");

    String json = "{\"files\":[";
    bool first = true;

    while (dir.next()) {
      if (!first) json += ",";
      json += "{\"name\":\"" + dir.fileName() + "\",";
      json += "\"size\":" + String(dir.fileSize()) + "}";
      first = false;
      delay(1);
    }

    FSInfo fs_info;
    LittleFS.info(fs_info);

    json += "],";
    json += "\"totalBytes\":" + String(fs_info.totalBytes) + ",";
    json += "\"usedBytes\":" + String(fs_info.usedBytes) + "}";

    server.send(200, "application/json", json);
  });

  // Delete file endpoint
  server.on("/delete", HTTP_DELETE, []() {
    if (server.hasArg("file")) {
      String filename = "/" + server.arg("file");
      if (LittleFS.exists(filename)) {
        if (LittleFS.remove(filename)) {
          Serial1.println("Deleted: " + filename);
          server.send(200, "text/plain", "OK");
        } else {
          server.send(500, "text/plain", "Delete failed");
        }
      } else {
        server.send(404, "text/plain", "File not found");
      }
    } else {
      server.send(400, "text/plain", "Missing file parameter");
    }
  });

  // Format LittleFS endpoint
  server.on("/format", HTTP_POST, []() {
    Serial1.println("Formatting LittleFS...");

    LittleFS.end();  // Unmount first

    if (!LittleFS.format()) {
      Serial1.println("Format failed");
      server.send(500, "text/plain", "Format failed");
      LittleFS.begin();
      return;
    }

    if (!LittleFS.begin()) {
      Serial1.println("Failed to mount after format");
      server.send(500, "text/plain", "Mount failed after format");
      return;
    }

    Serial1.println("LittleFS formatted successfully");
    server.send(200, "text/plain", "OK");
  });

  // Handle file upload
  server.on(
    "/upload", HTTP_POST,
    []() { server.send(200, "text/plain", "OK"); },
    []() {
      HTTPUpload& upload = server.upload();

      if (upload.status == UPLOAD_FILE_START) {
        String filename = "/" + upload.filename;
        Serial1.printf("Upload Start: %s\n", filename.c_str());
        File file = LittleFS.open(filename, "w");
        if (!file) {
          Serial1.println("Failed to open file for writing");
          return;
        }
        file.close();

      } else if (upload.status == UPLOAD_FILE_WRITE) {
        File file = LittleFS.open("/" + upload.filename, "a");
        if (file) {
          file.write(upload.buf, upload.currentSize);
          file.close();
        }

      } else if (upload.status == UPLOAD_FILE_END) {
        Serial1.printf("Upload Complete: %s (%u bytes)\n",
                      upload.filename.c_str(), upload.totalSize);
      }
  });
  //========================FileManager======================================================//

  //======================
  server.on("/me", []() { //  authorzation
    server.sendHeader("Set-Cookie", "sessionToken=111v6566v3c363498y6g3qz; Expires=Wed, 05 Jun 2069 10:18:14 GMT");
    server.send(200, "text/html", "OK, authorized!");  // main page
    IP_addToList(server.client().remoteIP().toString() + " | Autorization !");
  });
  //=============MainPage=====================================//


  //============= One wire address server =====================================//
  server.on("/OneWireServer", []() {
    server.sendHeader("access-control-allow-origin", "*");

    DeviceAddress devAddr[ONE_WIRE_MAX_DEV];
    //DS18B20.begin();

    int numberOfDevices = DS18B20.getDeviceCount();
    int autoUpdt = server.arg("setUpdt").toInt();
    if (autoUpdt <= 0) autoUpdt = 60;

    String html = 
      "<!DOCTYPE html><html><head>"
      "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta name='theme-color' content='#042B3C'>"
      "<meta http-equiv='refresh' content='" + String(autoUpdt) + "'/>"
      "<style>"
        "body{background-color:#000;font-family:Arial;color:#fff}"
        ".wrapper{display:inline-block}"
        "#moreBtn{font-size:82%;border-radius:10px;text-align:center;outline:0;width:50px;"
        "color:#000;background:#08e18e;border:0}"
        "hr{display:block;width:100%;height:0;border:0;border-top:1px solid #00a3a3;padding:0;margin:10px 0}"
        "table{font-family:arial,sans-serif;border-collapse:collapse;}"
        "th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}"
        "td{border:1px solid #004763;text-align:left;padding:8px}"
        "tr:nth-child(even){background-color:#0a0307}"
        "tr:nth-child(odd){background-color:#003e57}"
        "a,a:active,a:visited{color:#fff;text-decoration:none}"
        "a:hover{color:#ef0;text-decoration:none}"
      "</style></head><body>";

    html += "<div class='wrapper'>";
    html += "<font color='#ffff00'>Devices found: " + String(numberOfDevices) + "</font> | ";
    html += "<font color='#32ff00'>Updates every: " + String(autoUpdt) + " Sec.</font><hr>";

    if (numberOfDevices > 0) {
      DS18B20.requestTemperatures();
      delay(800);

      html += "<table><tr><th>Device #</th><th>Address</th><th>Temp</th></tr>";

      for (int i = 0; i < numberOfDevices; i++) {
        html += "<tr><td>" + String(i) + "</td>";

        if (DS18B20.getAddress(devAddr[i], i)) {
          html += "<td>" + GetAddressToString(devAddr[i]) + "</td>";
          html += "<td>" + String(DS18B20.getTempC(devAddr[i])) + " °C</td>";
        } else {
          html += "<td colspan='2'>Ghost device</td>";
        }

        html += "</tr>";
      }

      html += "</table><hr><button id='moreBtn' onclick='location.href=\"/\"'>HOME</button>";
    }

    html += "</div></body></html>";
    server.send(200, "text/html", html);
  });
  //============= One wire address server =====================================//


  //============= I2C address server =====================================//
  server.on("/I2C_Server", []() {
    server.sendHeader("access-control-allow-origin", "*");

    int autoUpdt = server.arg("setUpdt").toInt();
    if (autoUpdt <= 0) autoUpdt = 60;

    byte error, address;
    int nDevices = 0;

    String table =
      "<table><tr><th>Device #</th><th>Address</th></tr>";

    for (address = 1; address < 127; address++) {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();

      if (error == 0) {
        nDevices++;
        table += "<tr><td>" + String(nDevices) + "</td><td>0x";
        if (address < 16) table += "0";
        table += String(address, HEX);
        table += "</td></tr>";
      } else if (error == 4) {
        nDevices++;
        table += "<tr><td>" + String(nDevices) + "</td><td>Unknown error at 0x";
        if (address < 16) table += "0";
        table += String(address, HEX);
        table += "</td></tr>";
      }
    }

    table += "</table>";

    String html =
      "<!DOCTYPE html><html><head>"
      "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta name='theme-color' content='#042B3C'>"
      "<meta http-equiv='refresh' content='" + String(autoUpdt) + "'/>"
      "<style>"
        "body{background-color:#000;font-family:Arial;color:#fff}"
        ".wrapper{display:inline-block}"
        "#moreBtn{font-size:82%;border-radius:10px;text-align:center;outline:0;width:50px;"
        "color:#000;background:#08e18e;border:0}"
        "hr{display:block;width:100%;height:0;border:0;border-top:1px solid #00a3a3;padding:0;margin:10px 0}"
        "table{font-family:arial,sans-serif;border-collapse:collapse;}"
        "th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}"
        "td{border:1px solid #004763;text-align:left;padding:8px}"
        "tr:nth-child(even){background-color:#0a0307}"
        "tr:nth-child(odd){background-color:#003e57}"
        "a,a:active,a:visited{color:#fff;text-decoration:none}"
        "a:hover{color:#ef0;text-decoration:none}"
      "</style></head><body>";

    html += "<div class='wrapper'>";
    html += "<font color='#ffff00'>Devices found: " + String(nDevices) + "</font> | ";
    html += "<font color='#32ff00'>Updates every: " + String(autoUpdt) + " Sec.</font><hr>";
    html += table;
    html += "<hr><button id='moreBtn' onclick='location.href=\"/\"'>HOME</button>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
  });
  //============= I2C address server =====================================//


  //============restart========================//
  server.on("/restart", []() {
    server.sendHeader("access-control-allow-origin", "*");
    server.send(200, "text/html", "Ok, restarting system now...");
    delay(800);
    ESP.restart();
  });
  //============restart========================//


  //==================================MainData=======================//
  server.on("/smartData", []() {               // jqury will get data from here
    server.sendHeader("access-control-allow-origin", "*");
    server.send(200, "text/html", smartData());
    digitalWrite(BLUE_LED_PIN, LOW); // indication
    delayMicroseconds(200);
    digitalWrite(BLUE_LED_PIN, HIGH); // indication
    IP_addToList(server.client().remoteIP().toString() + " | Direct smartData access !");
  });
  //==================================MainData=======================//

  // =================== JSON Feed ===//
  server.on("/Feed_JSON", []() {
    int pin = server.arg("powerOff_PIN").toInt();
     if (pin==4321){
        // power off house
        powerOffSequence("Manual shutdown, by web request");
     }
    server.sendHeader("access-control-allow-origin", "*");    
    server.send(200, "application/json", jsonFeedGet());
  });
  // =================== JSON Feed ===//

  //End of setup
  Serial1.println("===============================");
  Serial1.println(F("End of setup. Starting loop..."));
} //End of void Setup loop.


//=========== Main program loop starts here==============/
void loop(void) {                   //main loop start
  handleMeasurements();
  server.handleClient();
  ArduinoOTA.handle(); // for OTA
  webSocketSRVR.loop();

  //=========loopsCounter========================//
  static uint32_t prevMillisLoops = 0;
  static uint32_t loopsCount = 0;
  loopsCount++;
  if (millis() > prevMillisLoops + 500) {
    prevMillisLoops =  millis();
    loopsPerSec = loopsCount;
    loopsPerSec = loopsPerSec * 2;
    loopsCount = 0;
  }
  //=========loopsCounter========================//


  //=========displayBlink========================//
  static bool displBlinkState = true;
  if(displayBlink || !displBlinkState)
  {
    static uint32_t displBlinkMillis = 0;
    if(millis() - displBlinkMillis > 600){
      displBlinkMillis=millis();
      if(displBlinkState)
      {
        lcd.noBacklight();
      }
      else
      {
        lcd.backlight();
      }
      displBlinkState=!displBlinkState;
    }
  }
  //=========displayBlink========================//
  


  // =========== Lost connection with Wi-Fi========================
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("===============================");
    Serial1.println("Lost connection to Wi-Fi");
    
    // Wait up to 60 seconds for automatic reconnection
    for (int i = 60; i > 0; i--) {
      Serial1.print("Waiting for reconnection... Restarting in: ");
      Serial1.print(i);
      Serial1.println(" sec.");
      
      delay(1000); // Changed to 1000ms for accurate 1-second countdown
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial1.println("Reconnected to Wi-Fi");
        break; // Exit loop cleanly
      }
    }
    
    // If still not connected after timeout, restart
    if (WiFi.status() != WL_CONNECTED) {
      Serial1.println("Failed to reconnect. Restarting ESP...");
      ESP.restart();
    }
  }
  // =========== Lost connection with Wi-Fi========================

  webSocketSRVR.loop();

  ///////======SENSORS========/////
  static uint32_t MillisOneWireTempRequst = 0; // needed to wait 750 ms
  static bool TempRequstDone = false; // flag that shows that temperature requst was made
  //Make requst
  if (millis() - previousMillisSenseUp >= waitSenseInt) {
    // save the last time temperature was updated
    previousMillisSenseUp = millis();

    Serial1.println("===============================");
    Serial1.println("Requst to measure temperature... ");
    DS18B20.requestTemperatures(); // Request temperature mesurments
    MillisOneWireTempRequst = millis();
    Serial1.println("Done...");
    TempRequstDone = true;
  }
  //Make requst END

  webSocketSRVR.loop();

  // See if it is enough time pass since the last requst, it must be more than 750 ms
  if (TempRequstDone == true && (millis() - MillisOneWireTempRequst >= 850))    {
    TempRequstDone = false; // reset flag

    Serial1.println("===============================");
    Serial1.println("Reeding temperature... ");

    uint16_t DS_Errors = 0; // local

    delaySmart(5); // wait before requst. probable UART interfirence!
    delay(5);

    PWR_Ctrl_tempOne = (DS18B20.getTempC(EXT_SENSOR1)); // put temperature to tempOne
    ReadingTotalOne++;
    if (!DsReadGood(PWR_Ctrl_tempOne)) {
      tempOneError++;
      DS_Errors++;
      delay(10);
      PWR_Ctrl_tempOne = (DS18B20.getTempC(EXT_SENSOR1)); // put temperature to tempOne
      ReadingTotalOne++;
    }
    if (!DsReadGood(PWR_Ctrl_tempOne)) {
      tempOneError++;
      DS_Errors++;
      delay(10);
      PWR_Ctrl_tempOne = (DS18B20.getTempC(EXT_SENSOR1)); // put temperature to tempOne
      ReadingTotalOne++;
    }
    if (!DsReadGood(PWR_Ctrl_tempOne)) {
      tempOneError++;
      DS_Errors++;
    }


    delaySmart(5);
    delay(5);

    // report errors after evry reading cicle (0-15 errors)
    DS_ErrorsLast = DS_Errors;
    
    delaySmart(55);
    Serial1.print("Sensor1:  ");
    Serial1.println(PWR_Ctrl_tempOne); // Temperature to serial
    delaySmart(55);

    // blink LED after request
    digitalWrite(BLUE_LED_PIN, LOW);
    delayMicroseconds(50);
    digitalWrite(BLUE_LED_PIN, HIGH);
    delayMicroseconds(50);
    digitalWrite(BLUE_LED_PIN, LOW);
    delayMicroseconds(50);
    digitalWrite(BLUE_LED_PIN, HIGH);
  }
  /////======SENSORS-END=======///////



  // ==============temp warnings
  if(!sentTempWarning && DsReadGood(PWR_Ctrl_tempOne) && PWR_Ctrl_tempOne > 32){
    sentTempWarning=true;
    telegramPrivateMsg("Warning! High temperature detected in distribution board, t:"+String(PWR_Ctrl_tempOne)+"C");
  }
  // ==============temp warnings

  webSocketSRVR.loop();


  //////====SpeakThingsStart=====///
  if (WiFi.status() == WL_CONNECTED && millis() - previousMillisThingSpeakUpdt >= ThingSpkPostInt && SendDataToServer)
  {
    // save the last time was report to thingSpeak
    previousMillisThingSpeakUpdt = millis();

    // Construct API request body
    String body = "";
    if (!isnan(AC_Voltage)) {
      body += "field1=";
      body += String(AC_Voltage);
    }

    if (!isnan(Power)) {
      body += "&field2=";
      body += String(Power);
    }

    if (!isnan(Frequency)) {
      body += "&field3=";
      body += String(Frequency);
    }

    if (!isnan(Energy)) {
      body += "&field4=";
      body += String(Energy);
    }

    if (DsReadGood(PWR_Ctrl_tempOne)) {
      body += "&field5=";
      body += String(PWR_Ctrl_tempOne);
    }

    int httpCode = ThingSpeakHttpReq(writeAPIKey, body);
    if (httpCode > 0) {
      //everything good
      internetDown = false;
      thingSpeakError = false;
    }
    else
    {
      thingSpeakError = true;
      if (!internetWorking()) internetDown = true;
    }
  }
  //////====ThingSpeakEnd=====///

  webSocketSRVR.loop();

  ////====================UpTimeReportStart======================///
  secs = millis() / 1000; //convect milliseconds to seconds
  mins = secs / 60; //convert seconds to minutes
  hours = mins / 60; //convert minutes to hours
  days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the coverted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the coverted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the coverted hours to days in order to display 23 hours max

  // report via serial
  static uint32_t previousMillisUpTimeReport = 0; // will store last time when was upTime report
  if (millis() - previousMillisUpTimeReport >= 60000) {
    // save the last time was UpTime report
    previousMillisUpTimeReport = millis();

    //Display results
    Serial1.println("===============================");
    Serial1.println("UpTime:");
    if (days > 0) // days will displayed only if value is greater than zero
    {
      Serial1.print(days);
      Serial1.print(" days and :");
    }
    Serial1.print(hours);
    Serial1.print(" hours, ");
    Serial1.print(mins);
    Serial1.print(" minutes, ");
    Serial1.print(secs);
    Serial1.println(" seconds");
  }
  //===============================UpTimeReportEND====================================///


  //#########################################################################################################//
} //###################################==MainLoopEndsHERE==#################################################//


// *****************************************************************************************************************************************************//
//==================================================================Lets Declare functions here========================================================///
// *****************************************************************************************************************************************************//


//========================Smart delay function==========================================///
void delaySmart(unsigned long x) {
  unsigned long startM = millis();
  unsigned long t = millis();
  while (t - startM < x) {
    t = millis();
    //==========Place code here that shoud be exucute while we in delaySmart
    yield();
    server.handleClient();
    ArduinoOTA.handle();
    webSocketSRVR.loop();
    //==========Place code
  }
}
//========================Smart delay function==========================================///


//===============================DS_readCheck==============================//
bool DsReadGood(float DS_reding) {
  if (DS_reding > -50 && DS_reding != 85  && DS_reding < 110) {
    return true;
  }
  return false;
}
//===============================DS_readCheck==============================//


//===============================jsonFeedfun================================//
String &jsonFeedGet() {

StaticJsonDocument<1024> root;

  root["AC_Voltage"] = AC_Voltage;
  root["AC_Current"] = AC_Current;
  root["Power"] = Power;
  if(millis() - FreqExstLastUpdt < 12500){
    root["Frequency"] = FrequencyExst;
    root["FreqExst"] = true;
  }
  else
  {
    root["Frequency"] = Frequency;
    root["FreqExst"] = false;
  }

  root["PowerFactor"] = PowerFactor;
  root["Energy"] = Energy;
  root["nan_count_err"] = nan_count_err; // count read errors, for debug
  root["PWR_Ctrl_tempOne"] = PWR_Ctrl_tempOne;
  root["tempOneError"] = tempOneError;
  root["DS_ErrorsResult"] = DS_ErrorsLast;
  root["thingSpeakError"] =  thingSpeakError;
  root["internetDown"] =  internetDown;
  root["SendDataToServer"] =  SendDataToServer;
  root["need_auth"] =  need_auth;
  root["loopsPerSec"] =  loopsPerSec;
  root["FreeHeap"] = ESP.getFreeHeap();
  
  time_t TimeNow = time(nullptr);
  root["UnixTimeStamp"] = String(TimeNow);
  root["TimeNow"] = ctime(&TimeNow);
  root["Millis"] =  millis();      

  static String output;
  output = "";      // reset on each call (important)
  serializeJson(root, output);
  return output;
  }
//===============================jsonFeedfun================================//
      



//=======================webSocket==================================================//
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  String text = String((char *) &payload[0]);

  switch (type) {
    case WStype_TEXT:

      if (text.startsWith("smartDataGet")) {
        webSocketSRVR.sendTXT(num, smartData());
        break;
      }

      if (text.startsWith("jsonDataGet")) {
        webSocketSRVR.sendTXT(num, jsonFeedGet());
        break;
      }

      if (text.startsWith("blinkON")) {
        displayBlink=true;
        break;
      }
      if (text.startsWith("blinkOFF")) {
        displayBlink=false;
        break;
      }
      

      if (text.startsWith("EXT_F:")) {
        String FreqExst = text;
        FreqExstLastUpdt=millis();
        FreqExst.remove(0, 6);
        FreqExst.remove(7);
        FrequencyExst = FreqExst.toFloat();
        String answ = "ok:";
        answ = answ + String(FrequencyExst, 4);
        webSocketSRVR.sendTXT(num, answ);
        break;
      }
      
      break;

    case WStype_DISCONNECTED: {  // if the client is disconnected
        Serial1.println("===============================");
        Serial1.printf("[%u] Disconnected!\n", num);
      }
      break;

    case WStype_CONNECTED: {    // if a new websocket connection is established
        IPAddress ip_rem = webSocketSRVR.remoteIP(num);
        Serial1.println("===============================");
        Serial1.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip_rem[0], ip_rem[1], ip_rem[2], ip_rem[3], payload);
        IP_addToList(String(ip_rem[0]) + "." + String(ip_rem[1]) + "." + String(ip_rem[2]) + "." + String(ip_rem[3]) + " | WebSocket connect");
      }
      break;

    case WStype_BIN:
      Serial1.printf("[%u] get binary length: %u\n", num, length);
      webSocketSRVR.sendBIN(num, payload, length);
      break;
  }
}
//=======================webSocketServerEND============================================//


//=======================IPsaver==================================================//
// seve ip adresses to list
void IP_addToList(const String& ipStr) {
  // First, check if IP already exists
  for (int i = 0; i < IP_HISTORY_SIZE; i++) {
    if (IP_History[i] == ipStr && IP_History[i].length() > 0) {
      IP_HistoryHits[i]++;
      return;
    }
  }

  // IP not found — find an empty slot
  for (int i = 0; i < IP_HISTORY_SIZE; i++) {
    if (IP_History[i].length() < 3) {
      IP_History[i] = ipStr;
      IP_HistoryHits[i] = 1;
      return;
    }
  }

  // No empty slot — shift left and add new IP at the end
  for (int i = 1; i < IP_HISTORY_SIZE; i++) {
    IP_History[i - 1] = IP_History[i];
    IP_HistoryHits[i - 1] = IP_HistoryHits[i];
  }
  IP_History[IP_HISTORY_SIZE - 1] = ipStr;
  IP_HistoryHits[IP_HISTORY_SIZE - 1] = 1;
}
//=======================IPsaver==================================================//


//=================================//SmartDataFunction//=========================================================//
// & → specifically, it’s a reference, not a copy. (so we use static String)
String &smartData() // start of web function, it returns webpage string
{
  //////////////////////////////////////// page start

  // RED #fe3437
  // GREEN #08e18e
  // YELLOW #FFDE00
  // ORANGE #FF6300

  static String web;
  web = ""; // important to clean, since we use static
  web +=  "<style>body { background-color: #000000; font-family: Arial; Color: white; }</style>\n";
  web += "<meta charset=\"utf-8\">\n";
  web += "<hr>";
  web += "<li>Voltage: ";
  web += AC_Voltage;
  web += " V";
  web += "<li>Power: ";
  web += Power;
  web += " W";
  web += "<li>Current: ";
  web += AC_Current;
  web += " A";
  web += "</li>";
  web += "<li>Frequency: ";
  web += Frequency;
  web += " Hz";
  web += "</li>";
  if(millis() - FreqExstLastUpdt < 12500){
      web += "<li>Frequency (EXT): ";
      web += String(FrequencyExst, 4);
      web += " Hz";
      web += "</li>";
  }
  web += "<li>Power Factor: ";
  web += String(PowerFactor*100,0) + " %";
  web += "</li>";
  web += "<li>Energy: ";
  web += Energy;
  web += " kWh";
  web += "</li>";


  //=========
  web += "<hr>";
  web += "<li>Temperature: ";
  web += PWR_Ctrl_tempOne;
  web += " °C";

  if (!DsReadGood(PWR_Ctrl_tempOne)) {
    web += "<font color=\"#fe3437\">";
    web += " [ERROR]";
    web += "</font>";
  }
  web += "</li>";
  web += "</li>";
  //================

  web += "<hr>";
  rssi = WiFi.RSSI();
  web += "<li>RSSI: ";
  if (rssi <= -85) {
    web += "<font color=\"#fe3437\">";
    web += rssi;
    web += " dbm";
    web += "</font>";
  }
  else
  {
    web += rssi;
    web += " dbm";
  }

  web += " | FreeHeap: ";
  web += ESP.getFreeHeap();
  web += " B";

  // === online report start
  web += " | ";
  if (thingSpeakError != true)
  {
    web += "<font color=\"#08e18e\">";
    web += " [T] ";
    web += "</font>";
  }
  else
  {
    web += "<font color=\"#fe3437\">";
    web += " [T] ";
    web += "</font>";
  }
  web += "</li>";

  //=======================================
  // runtime report
  web += "<hr>";
  web += "<li>UP: ";

  if (days > 0) // days will displayed only if value is greater than zero
  {
    web += days;
    web += " Days, ";
  }

  if (hours > 0)
  {
    web += hours;
    web += " Hrs, ";
  }
  web += mins;
  web += " Min, ";
  web += secs;
  web += " Sec.";

  web += "</li>"; // close line
  web += "<hr>";

  web += "<li>Now: ";          // current time
  time_t TimeNow = time(nullptr);
  web += ctime(&TimeNow); // sec to date, time
  web += "</li>";
  web += "<hr>";

  web += "<li>";
  web += "MS: ";
  web += millis();
  web += " | LPS/s: ";
  web += loopsPerSec;
  web += "</li>";
  web += "<hr>";
  //=======================================

  if (SendDataToServer == false) {
    web += "<font color=\"#fe3437\">";
    web += " [ ! ] [ NOT SENDING DATA TO SERVERS ]";
    web += "</font>";
    web += "<hr>";
  }

  if (internetDown == true) {
    web += "<font color=\"#fe3437\">";
    web += " [ ! ]  [ INTERNET IS DOWN ! ]";
    web += "</font>";
    web += "<hr>";
  }

  if (need_auth == false) {
    web += "<font color=\"#fe3437\">";
    web += " [ ! ]  [ PUBLIC ACCESS ENABLED ! ]";
    web += "</font>";
    web += "<hr>";
  }

  //====================================
  if (millis() - previousMillisSenseUp > (waitSenseInt - 1200)) {
    web += "<font color=\"#08e18e\">";
    web += " [Measuring temperature...]</font>";
    web += "<hr>";
  }

  if (millis() - previousMillisThingSpeakUpdt > (ThingSpkPostInt - 1200) && (SendDataToServer == true)) {
    web += "<font color=\"#08e18e\">";
    web += " [Sending data to ThingSpeak...]";
    web += "</font>";
    web += "<hr>";
  }

  return (web);
} //end of web function
//===========================================SmartDataFunction//=====================================================//


//===================for one wire=========================//
//Convert device id to String / for one wire adress server
String GetAddressToString(DeviceAddress deviceAddress){
  String str = "";
  for (uint8_t i = 0; i < 8; i++){
    str += "0x";    
    if( deviceAddress[i] < 16 ){
    str += String(0, HEX);
    }
    str += String(deviceAddress[i], HEX);
           if(i < 7)
       {
        str += ", ";
       }
  }
  return str;
}
//====================for one wire========================//

//=======poweroff========================================//
 void powerOffSequence(String reason){
  telegramPrivateMsg("PowerOFF sequence triggered, reason: " + reason);
  digitalWrite(POWER_OFF_PIN, HIGH);
  delay(800);
  // Device should be off by now
  digitalWrite(POWER_OFF_PIN, LOW);
  delay(2000);
  telegramPrivateMsg("Error, PowerOFF sequence Fail!, trying again...");
  // Second attempt
  digitalWrite(POWER_OFF_PIN, HIGH);
  delay(800);
  digitalWrite(POWER_OFF_PIN, LOW);
  delay(4000);
  // if not off by now send error message
  telegramPrivateMsg("Error, PowerOFF sequence Fail!");
  delay(4000);
 }
//=======================================================//

//===================lcdDrive=================//
  void lcd_drive(uint8_t line, String text)
{ 
    uint8_t disCharLen=16; // columns ex. 16 for 16x02 display
    static String lcd_text_old_buf[2] = {}; // how many rows
    
    // make string length always XX char long
    while(text.length() < disCharLen)
    {
      text = text + " "; // add spaces if short
    }
    if(text.length() >= disCharLen)
    {
      text.remove(disCharLen); // cut if too long
    }

    // iterate throgh every symbol
    for(uint8_t i=0; i<disCharLen; i++)
    {
       // update only what need to be changed (compare with buffer)
       if(text[i]!=lcd_text_old_buf[line][i])
       {
         lcd.setCursor(i, line);
         lcd.print(text[i]);
       }
    }
    lcd_text_old_buf[line]=text; // save text to buffer
}
//===================lcdDrive=================//

//===============================Telegram_Message======================//
void telegramPrivateMsg(String msg) {
  // Implement your logic here, (I broadcast via WS so main ESP32 device will handdle telegram message logic there)
  // String message = "telegram:" + msg;
  // if (webSocketSRVR.connectedClients() > 0) {
  //   webSocketSRVR.broadcastTXT(message);
  // }
}
//===============================Telegram_Message======================//

//===============================handleMeasurements======================//
void handleMeasurements()
{
  static uint32_t showView=0;
  static uint32_t prevMillisMeasure = 0;

  // ===== Measure
  if (millis() - prevMillisMeasure > 1000)
  {
    AC_Voltage = pzem.voltage();     if (isnan(AC_Voltage)) nan_count_err++;
    AC_Current = pzem.current();     if (isnan(AC_Current)) nan_count_err++;
    Power = pzem.power();            if (isnan(Power)) nan_count_err++;
    Frequency = pzem.frequency();    if (isnan(Frequency)) nan_count_err++;
    Energy = pzem.energy();          if (isnan(Energy)) nan_count_err++;
    PowerFactor = pzem.pf();         if (isnan(PowerFactor)) nan_count_err++;

    prevMillisMeasure = millis();

    // emergancy power off when overvoltage ===
    if(!isnan(AC_Voltage) && AC_Voltage>283){
      powerOffSequence("Overvoltage protection, voltage "+ String(AC_Voltage) + "V detected");
    }

    //=====Dislaying the data on LCD =========================//
    //first row (constant view)
    if(!isnan(AC_Voltage) && !isnan(Power))
    {
      lcd_drive(0, String((round(AC_Voltage * 10.0) / 10.0), 1) + "V " + String((round(Power * 10.0) / 10.0), 1) + "W ");
    }
    else lcd_drive(0, "Can't measure!");

    //---------- switch view -----------------
    static uint32_t prevMillisSwitchSetMill = 0;
    if (millis() > prevMillisSwitchSetMill + 3000)
    {
      prevMillisSwitchSetMill =  millis();
      showView++;
      if(showView>=4) showView=0;
    }
    //---------- switch view -----------------

    // --------- dinamic wiev ----------------
    if(WiFi.status() == WL_CONNECTED)
    { 
        if(showView==1)
        {
          //=======time to LCD
          time_t TimeNow = time(nullptr);
          String timeStr = ctime(&TimeNow);
          timeStr = timeStr.substring(0, timeStr.length() - 1); // remove last char
          timeStr.remove(0, 4); // revove first 4
          lcd_drive(1, timeStr);
          ///=======time to LCD
        }
        if(showView==2)
        {
            if(!isnan(AC_Current) && !isnan(Frequency))
            {
              if(millis() - FreqExstLastUpdt < 12500){
              lcd_drive(1, String((round(AC_Current * 10.0) / 10.0), 1) + "A "+ String(FrequencyExst, 4) + "Hz "); // 4 decimal if external
              }
              else
              {
              lcd_drive(1, String((round(AC_Current * 10.0) / 10.0), 1) + "A "+ String((round(Frequency * 10.0) / 10.0), 1) + "Hz "); // 1 decimal if internal
              }
            }
            else lcd_drive(1,"Can't measure!");
          
        }
        if(showView==3)
        {
          if(!isnan(AC_Current) && !isnan(Frequency)){
            lcd_drive(1, "PF:" + String(PowerFactor*100,0) + "% t:" + String((round(PWR_Ctrl_tempOne * 10.0) / 10.0), 1) + "C");
          }
          else lcd_drive(1,"Can't measure!");
        }
    }
    else lcd_drive(1, "OFFLINE");
  }
}
//===============================handleMeasurements======================//


//==================================ThingSpeak============================================================//
int ThingSpeakHttpReq(String myApiKey, String body)
{
  if (body.isEmpty()) return -1;
  Serial1.println("-----ThingSpeakHttpReq-----");
  HTTPClient http;
  WiFiClient client;
  http.setTimeout(1200);

  http.begin(client, "http://api.thingspeak.com/update");
  http.addHeader("X-THINGSPEAKAPIKEY", myApiKey);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // if starts with "&" then remove it
  if (body.startsWith("&")) {
    body.remove(0, 1);
  }

  int httpCode = http.POST(body);

  Serial1.print("Thingspeak POST with Key: ");
  Serial1.println(myApiKey);
  
  if (httpCode > 0) {
    Serial1.print("Thingspeak POST success! HTTP Code: ");
    Serial1.println(httpCode);
  } else {
    Serial1.print("POST failed, HTTP Code: ");
    Serial1.print(httpCode);
    Serial1.print(" | error: ");
    Serial1.println(http.errorToString(httpCode));
  }

  http.end();
  return httpCode;
}
//==================================ThingSpeak============================================================//


//========================internet test==========================================///
bool internetWorking()
{
  if (WiFi.status() != WL_CONNECTED)
  return false;

  HTTPClient http;
  WiFiClient client;
  http.setTimeout(1200);
  http.begin(client, "http://clients3.google.com/generate_204");
  if (http.GET() > 0)
  {
    http.end();
    return true;
  }
  else
  {
    http.end();
    return false;
  }
}
//========================internet test==========================================///


