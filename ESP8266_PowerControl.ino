#include <Arduino.h>
#include <ArduinoOTA.h> // for OTA
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <OneWire.h> // for DS18B20              
#include <DallasTemperature.h> // for DS18B20  
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h> // lib moded
#include <WebSocketsServer.h>
#include <Hash.h>
#include <FS.h>   // Include the SPIFFS library
#include <time.h>
#include <ArduinoJson.h> // json
#include <StreamString.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <PZEM004Tv30.h>
WiFiManager wm;


/* Hardware Serial3 is only available on certain boards.
 * For example the Arduino MEGA 2560
*/
PZEM004Tv30 pzem(&Serial);


// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);

//WebsocketSesver
WebSocketsServer webSocketSRVR = WebSocketsServer(8092);

// For wifi beacons
extern "C" {
#include "user_interface.h"
}

//MDNSResponder mdns;


WiFiClient client;
//=========ThingSpeak
const String writeAPIKey = "XXXXXXXXXXXXXX"; // write API key for your ThingSpeak Channel

ESP8266WebServer server(8089);

String IP_History [11] {};  //store IP addreses, and comments store 10 (so [11], +1 IMPORTANT)
uint16_t IP_HistoryHits [11] {};


#define ONE_WIRE_BUS 14 //Pin to which is attached a temperature sensor
#define ONE_WIRE_MAX_DEV 10 //The maximum number of devices

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

uint8_t numberOfDevices; //Number of temperature devices found

const uint32_t waitSenseInt = 8000; // my varible to set ms intervals to measure temperature,
uint32_t previousMillisSenseUp = 0; // will store last time when rempareture was updated

bool SendDataToServer = true;   //  ThingSpeak

int rssi; // for signal level

bool WifiOnlineStateFlag = false; //  false - offline, true - online



// set it to false, and then get auth cookie by going to 192.168.x.x:8089/me
bool PublicAccess = true;  // authorization 


const uint32_t ThingSpkPostInt = 60000; // set ms intervels between posts to ThingSpeak
uint32_t previousMillisThingSpeakUpdt = 0; // will store last time when was report to ThingSpeak server

const uint32_t UpTimeReportInt = 60000; // Set ms intervals to report upTime via Serial
uint32_t previousMillisUpTimeReport = 0; // will store last time when was upTime report


uint32_t wifiApCheckMillLast = 0; // for wifi in offline mode


uint32_t loopsPerSec = 0;
uint32_t loopsCount = 0;
uint32_t prevMillisLoops = 0;


//UptimeVaribles_START
uint32_t days = 0;
uint32_t hours = 0;
uint32_t mins = 0;
uint32_t secs = 0;
//UptimeVaribles_END

uint32_t MillisOneWireTempRequst = 0; // needed to wait 750 ms
bool TempRequstDone = false; // flag that shows that temperature requst was made
bool ReadingDone = false; // flag that shows that temperature redeng was performed
float PWR_Ctrl_tempOne = -127;
uint16_t tempOneError = 0;
uint32_t ReadingTotalOne = 0;
uint16_t DS_ErrorsResult = 0;

bool sentTempWarning = false;


float AC_Voltage = 0;
float Power = 0;
float AC_Current = 0;


float Frequency = 0;
float Energy = 0;
float PowerFactor = 0;

uint32_t showInfoSet=0;

uint32_t prevMillisBeacon = 0;


uint32_t prevMillisMeasure = 0;

uint32_t prevMillisSwitchSetMill = 0;



const uint8_t indicPinBLUE = 2;  // pin for indication

const uint8_t PowerOff_pin = 5;



bool internetDown = false;

bool thingSpeakError = true;


// Sensor adreses
DeviceAddress EXT_SENSOR1 = { 0x28, 0x91, 0xcf, 0xfc, 0x08, 0x00, 0x00, 0x93 }; // inside sensor
// Sensor adreses end



void setup(void) {
  pinMode (PowerOff_pin, OUTPUT); 
  digitalWrite(PowerOff_pin, LOW); 
  
  // preparing GPIOs
  pinMode (indicPinBLUE, OUTPUT); // indication works as an output
  digitalWrite (indicPinBLUE, LOW); // Indication pin set for low.

  
  Serial1.begin(115200); // using Serial1 for debug
  Wire.begin(4, 0);
  Wire.setClock(400000);

  // initialize the LCD
  lcd.begin();
  delay(200);
  lcd.clear();


  lcd_drive(0, "Starting...");


 
  ArduinoOTA.onEnd([]() { // do a fancy thing with our board led at end
    for (int i = 0; i < 30; i++)
    {
      analogWrite(indicPinBLUE, (i * 100) % 1001);
      delay(25);
    }

    // restart after ota
    ESP.restart();
  });

 

  if (!SPIFFS.begin()) {
    Serial1.println(F("SPIFFS: Failed to mount file system!"));
  }
  else
  {
    Serial1.println(F("SPIFFS: Ok, File system mouted!"));
  }


  // SPIFFS.format(); // format spiffs


  WiFi.hostname("PowerControlESP");
  delay(500);
  WiFi.mode(WIFI_STA); // STA mode.
  delay(500);
  Serial1.println("===============================");
  Serial1.println(F("WIFI_STA mode"));

  while(millis() < 30000){ // delay to wait for the router, do measurments while waiting
  handleMeasurements();
  }

  wm.setConfigPortalBlocking(false);


    //automatically connect using saved credentials if they exist
    //If connection fails it starts an access point with the specified name
    if(wm.autoConnect("PowerControlESP | Offline", "231019787511")){
        Serial1.println("connected...yeey :)");
        lcd_drive(0, "Wi-Fi Connected");
        WifiOnlineStateFlag = true; // wifi in online state

          // Indicate the start of connectrion to AP
          digitalWrite(indicPinBLUE, HIGH);
          delay(600);
          digitalWrite(indicPinBLUE, LOW);
          delay(175);
          digitalWrite(indicPinBLUE, HIGH);
          delay(175);
          digitalWrite(indicPinBLUE, LOW);
          delay(175);
          digitalWrite(indicPinBLUE, HIGH);
          //
    }
    else {
        Serial1.println("Configportal running");
        lcd_drive(0, "AP Started");
        WifiOnlineStateFlag = false;
        Serial1.println("===============================");
        Serial1.println(F("OFFLINE MODE !!!"));
    }


  ArduinoOTA.setHostname("PowerControlESP");
  ArduinoOTA.begin(); // for OTA

  delay(500);

  
//  if (mdns.begin("PowerControlESP")) {
//    Serial1.println(F("MDNS responder started!"));
//    mdns.addService("http", "tcp", 8089);
//  } else {
//    Serial1.println(F("Error setting up MDNS responder!"));
//  }


  // time
  // EET-2EEST,M3.5.0/3,M10.5.0/4 this is for Ukraine, find for your country
  configTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org");


  //Websocket
  webSocketSRVR.begin();
  webSocketSRVR.onEvent(webSocketEvent);


  
  //These 3 lines tell esp to collect User-Agent and Cookie in http header when request is made
  const char * headerkeys[] = {"User-Agent", "Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
  server.collectHeaders(headerkeys, headerkeyssize );
  //These 3 lines tell esp to collect User-Agent and Cookie in http header when request is made



  //===================ApList=============================//
  // Scans and shows a table with AP
  server.on("/listAP", []() {
    server.sendHeader("access-control-allow-origin", "*");
    String listAP_Str = F("<!DOCTYPE html><html><head><style>body{background-color:#000;font-family:Arial;Color:#fff}</style><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta charset=\"utf-8\"><meta name=\"theme-color\" content=\"#042B3C\">");
    listAP_Str += F("<style>hr{border:0;background-color:#570633;height:5px}table{font-family:arial,sans-serif;border-collapse:collapse;width:100%}th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}td{border:1px solid #004763;text-align:left;padding:8px}tr:nth-child(even){background-color:#0a0307}tr:nth-child(odd){background-color:#003e57}a,a:active,a:visited{color:#fff;text-decoration:none}a:hover{color:#ef0;text-decoration:none}</style></head><body>");

    int n = WiFi.scanNetworks();

    listAP_Str += F("<font color=\"#ffff00\"> Networks found : ");
    listAP_Str += n;
    listAP_Str += F("</font>");
    listAP_Str += F("<hr>");

    listAP_Str += F("<table>\n");
    listAP_Str += F("<tr>\n");
    listAP_Str += F("<th>SSID (NAME)</th>\n");
    listAP_Str += F("<th>Signal</th>\n");
    listAP_Str += F("<th>Security</th>\n");
    listAP_Str += F("<th>BSSID (MAC)</th>\n");
    listAP_Str += F("<th>Channel</th>\n");
    listAP_Str += F("</tr>\n");
    int x = 0;
    while (x !=  n) {

      listAP_Str += F("<tr>\n");

      listAP_Str += F("<td>\n");
      listAP_Str += WiFi.SSID(x);
      listAP_Str += F("</td>\n");

      listAP_Str += F("<td>\n");
      listAP_Str += WiFi.RSSI(x);
      listAP_Str += F(" dBm");
      listAP_Str += F("</td>\n");

      listAP_Str += F("<td>\n");
      if (WiFi.encryptionType(x) == 2) {
        listAP_Str += F("TKIP (WPA)");
      }
      else if (WiFi.encryptionType(x) == 4) {
        listAP_Str += F("CCMP (WPA)");
      }
      else if (WiFi.encryptionType(x) == 5) {
        listAP_Str += F("WEP");
      }
      else if (WiFi.encryptionType(x) == 7) {
        listAP_Str += F("NONE (OPEN)");
      }
      else if (WiFi.encryptionType(x) == 8) {
        listAP_Str += F("AUTO");
      }
      else {
        listAP_Str += F("UNKNOWN/ERROR");
      }
      listAP_Str += F("</td>\n");

      listAP_Str += F("<td>\n");
      listAP_Str += WiFi.BSSIDstr(x);
      listAP_Str += F("</td>\n");

      listAP_Str += F("<td>\n");
      listAP_Str += WiFi.channel(x);
      listAP_Str += F("</td>\n");

      listAP_Str += F("</tr>\n");
      x++;
    }
    listAP_Str += F("</table>");
    listAP_Str += F("</body>\n");
    listAP_Str += F("</html>");

    server.send(200, "text/html", listAP_Str);  // main page
  });
  //===================ApList===================================//


  //===================InfoList=============================//
  server.on("/info", []() {
    server.sendHeader("access-control-allow-origin", "*");
    String info = F("<!DOCTYPE html><html><head><style>body{background-color:#000;font-family:Arial;Color:#fff}</style><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta charset=\"utf-8\">");
    info += F("<meta name=\"theme-color\" content=\"#042B3C\"><style>hr{border:0;background-color:#570633;height:5px}table{font-family:arial,sans-serif;border-collapse:collapse;width:100%}th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}td{border:1px solid #004763;text-align:left;padding:8px}tr:nth-child(even){background-color:#0a0307}tr:nth-child(odd){background-color:#003e57}a,a:active,a:visited{color:#fff;text-decoration:none}a:hover{color:#ef0;text-decoration:none}</style></head><body>");

    info += F("<table>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("The last reset reason:");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getResetReason();
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("Core version:");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getCoreVersion();
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("SDK version:");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getSdkVersion();
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("Frequency:");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getCpuFreqMHz();
    info += F(" MHz");
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("Sketch size:");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getSketchSize();
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("Free sketch space:");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getFreeSketchSpace();
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("Flash chip size (as seen by the SDK):");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getFlashChipSize();
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("Flash chip size (based on the flash chip ID):");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getFlashChipRealSize();
    info += F("</td>\n");
    info += F("</tr>\n");

    info += F("<tr>\n");
    info += F("<td>\n");
    info += F("Free Heap Size:");
    info += F("</td>\n");
    info += F("<td>\n");
    info += ESP.getFreeHeap();
    info += F("</td>\n");
    info += F("</tr>\n");


    info += F("</table>");
    info += F("</body>\n");
    info += F("</html>");

    server.send(200, "text/html", info);  // main page
  });
  //===================InfoList===================================//


//===================DS_errors==============//
  server.on("/ErrorsDS", []() {
    server.sendHeader("access-control-allow-origin", "*");
    String ErroresWebReport = F("<!DOCTYPE html><html><head><style>body{background-color:#000;font-family:Arial;Color:#fff}</style><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta charset=\"utf-8\">");
    ErroresWebReport += F("<meta name=\"theme-color\" content=\"#042B3C\"><style>hr{border:0;background-color:#570633;height:5px;width:700px}table{font-family:arial,sans-serif;border-collapse:collapse;width:700px}th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}td{border:1px solid #004763;text-align:left;padding:8px}tr:nth-child(even){background-color:#0a0307}tr:nth-child(odd){background-color:#003e57}a,a:active,a:visited{color:#fff;text-decoration:none}a:hover{color:#ef0;text-decoration:none}</style></head><body>");
    ErroresWebReport += F("<meta http-equiv='refresh' content='300'/>"); //auto apdate web page
    
    ErroresWebReport += F("<table>\n");

    uint32_t ReadingTotal = ReadingTotalOne; ///add more if needed
    uint32_t ErrorTotal = tempOneError; ///add more if needed

    ErroresWebReport += F("<tr>");
    ErroresWebReport += F("<th>Sensor</th>");
    ErroresWebReport += F("<th>Sensor Errors</th>");
    ErroresWebReport += F("<th>Sensor Readings Total</th>");
    ErroresWebReport += F("<th>Sensor Errors Percent</th>");
    ErroresWebReport += F("</tr>");

    
// sensor one
    ErroresWebReport += F("<tr>");
    ErroresWebReport += F("<td>PowerControl / PWR_Ctrl_tempOne");
    ErroresWebReport += F("</td>");
    ErroresWebReport += F("<td>");
    ErroresWebReport += tempOneError;
    ErroresWebReport += F("</td>");
    ErroresWebReport += F("<td>");
    ErroresWebReport += ReadingTotalOne;
    ErroresWebReport += F("</td>");
    ErroresWebReport += F("<td>");
    if (ReadingTotalOne != 0) {
      ErroresWebReport += ((float(tempOneError) / float(ReadingTotalOne)) * 100.00);
      ErroresWebReport += F(" %");
    }
    else{
    ErroresWebReport += F("0 %");
    }
    ErroresWebReport += "";
    ErroresWebReport += F("</td>");
    ErroresWebReport += F("</tr>");

    ErroresWebReport += F("</table>");

    
    ErroresWebReport += F("<hr align=\"left\">");

    ErroresWebReport += F("<li>Reading times Total: ");
    ErroresWebReport += ReadingTotal;
    ErroresWebReport += F("</li>");

    ErroresWebReport += F("<li>Errors Total: ");
    ErroresWebReport += ErrorTotal;
    ErroresWebReport += F("</li>");


    if (ReadingTotal != 0) {
      ErroresWebReport += F("<li>Errors Persent Total: ");
      ErroresWebReport += ((float(ErrorTotal) / float(ReadingTotal)) * 100.00);
      ErroresWebReport += F(" % </li>");
    }

    ErroresWebReport += F("<hr align=\"left\">");

    ErroresWebReport += F("<li>Uptime: ");
    ErroresWebReport += days;
    ErroresWebReport += F(" Days, ");
    ErroresWebReport += hours;
    ErroresWebReport += F(" Hours, ");
    ErroresWebReport += mins;
    ErroresWebReport += F(" Min, ");
    ErroresWebReport += secs;
    ErroresWebReport += F(" Sec.");
    ErroresWebReport += F("</li>");

    ErroresWebReport += F("</body>\n");
    ErroresWebReport += F("</html>");
    server.send(200, "text/html", ErroresWebReport);
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
    PublicAccess = true;
  });
  server.on("/PublicAccessFalse", []() {
    server.sendHeader("access-control-allow-origin", "*");
    server.send(200, "text/html", "<p>PublicAccess: disabled!</p>");
    PublicAccess = false;
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


  //=============MainPage=====================================//
  server.on("/", []() {
    server.sendHeader("access-control-allow-origin", "*");

    if (server.hasHeader("Cookie") || PublicAccess == true) {
      String cookie = server.header("Cookie"), sessionToken = "111v6566v3c363498y6g3qz";
      if (cookie.indexOf(sessionToken) != -1 || PublicAccess == true) {


        //--------------Read_file_and_send-----------//
        if (SPIFFS.exists("/cntrl.html")) {              // If the file exists
          server.sendHeader("Content-Encoding", "gzip");         // gziped header
          File file = SPIFFS.open("/cntrl.html", "r");     // Open it
          server.streamFile(file, "text/html");    // And send it to the client
          file.close();                                          // Then close the file again
        }
        else {
          server.send(200, "text/html", "SPIFFS: Error! can't open file, no such file");  // error
        }
        //-------------Read_file_and_send_finished-------//

      } else {
        server.send(200, "text/html", "Oh, Crap! Your cookie is not valid!");  // main page
        IP_addToList(server.client().remoteIP().toString() + " | Not valid cookie !");
      }
    } else {
      server.send(200, "text/html", "Oh, Crap! You not supposed to be here!");  // main page
      IP_addToList(server.client().remoteIP().toString() + " | Not authorized !");
    }
  });
  //======================
  server.on("/me", []() { //  authorzation
    server.sendHeader("Set-Cookie", "sessionToken=111v6566v3c363498y6g3qz; Expires=Wed, 05 Jun 2069 10:18:14 GMT");
    server.send(200, "text/html", "OK, authorized!");  // main page
    IP_addToList(server.client().remoteIP().toString() + " | Autorization !");
  });
  //=============MainPage=====================================//


   //============= One wire address server =====================================//
server.on("/OneWireServer", []() { //  authorzation
  server.sendHeader("access-control-allow-origin", "*");

  DeviceAddress devAddr[ONE_WIRE_MAX_DEV];
  
  DS18B20.begin();
  int numberOfDevices = DS18B20.getDeviceCount();
 
       int autoUpdt = 60;

       if(server.arg("setUpdt").toInt()!=0)
       {
        autoUpdt = server.arg("setUpdt").toInt();
       }
       
    String message = F("<!DOCTYPE html><html><head><style>body{background-color:#000;font-family:Arial;Color:#fff}</style><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta charset=\"utf-8\"><meta name=\"theme-color\" content=\"#042B3C\">");
    message += F("<meta http-equiv='refresh' content='"); //auto apdate web page
    message += String(autoUpdt);
    message += F("'/>"); 
    message += F("<style>hr{border:0;background-color:#570633;height:5px}table{font-family:arial,sans-serif;border-collapse:collapse;width:100%}th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}td{border:1px solid #004763;text-align:left;padding:8px}tr:nth-child(even){background-color:#0a0307}tr:nth-child(odd){background-color:#003e57}a,a:active,a:visited{color:#fff;text-decoration:none}a:hover{color:#ef0;text-decoration:none}</style></head><body>");
    message += F("<font color=\"#ffff00\"> Devices found: ");
    message += numberOfDevices;
    message += F("</font>");
    message += F(" | ");
    message += F("<font color=\"#32ff00\"> Updates every: ");
    message += String(autoUpdt);
    message += " Sec.";
    message += F("</font>");
    message += F("<hr>");

if (numberOfDevices>0)
{
  

    message += F("<table>\n");
    message += F("<tr>\n");
    message += F("<th>Device #</th>\n");
    message += F("<th>Address</th>\n");
    message += F("<th>Temp</th>\n");
    message += F("</tr>\n");


      DS18B20.requestTemperatures(); //Initiate the temperature measurement
      delay(800);
  

       // Loop through each device
    for(int i=0;i<numberOfDevices; i++)
    {
      // Search the wire for address
      if( DS18B20.getAddress(devAddr[i], i) )
      {

      message += F("<tr>\n");
      message += F("<td>\n");
      message += String(i, DEC);
      message += F("</td>\n");

        
      message += F("<td>\n");
      message += GetAddressToString(devAddr[i]);
      message += F("</td>\n");


      message += F("<td>\n");
      message += String(DS18B20.getTempC(devAddr[i])); //Measuring temperature in Celsius
      message += F(" °C");
      message += F("</td>\n");

     
      }
      else
      {
      message += F("<tr>\n");
      message += F("<td>\n");
      message += String(i, DEC);
      message += F("</td>\n");

        
      message += F("<td>\n");
      message += "Ghost device";
      message += F("</td>\n");


      message += F("<td>\n");
      message += "Ghost device";
      message += F("</td>\n");
      }
   }


    message += F("</tr>\n");
    message += F("</table>");
    message += F("</body>\n");
    message += F("</html>");

   
}
 server.send(200, "text/html", message );
});
//============= One wire address server =====================================//


// ============== I2C address server =====================================//
server.on("/I2C_Server", []() { //  authorzation
server.sendHeader("access-control-allow-origin", "*");

       int autoUpdt = 60;

       if(server.arg("setUpdt").toInt()!=0)
       {
        autoUpdt = server.arg("setUpdt").toInt();
       }
   
  byte error, address;
  int nDevices;
  nDevices = 0;

      String table = F("<table>\n");
      table += F("<tr>\n");
      table += F("<th>Device #</th>\n");
      table += F("<th>Address</th>\n");
      table += F("</tr>\n");
    
   for (address = 1; address < 127; address++ )  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0){
      nDevices++;

      table += F("<tr>\n");
      table += F("<td>\n");
      table += String(nDevices, DEC);
      table += F("</td>\n");

      table += F("<td>\n");
      table+= "0x"; 
      if (address < 16){
      table +="0";
      }
      table += String(address, HEX);
      table += F("</td>\n");
      
    } else if (error == 4) {
      table += F("<tr>\n");
      table += F("<td>\n");
      table += String(nDevices, DEC);
      table += F("</td>\n");

      table += F("<td>\n");
      table += "Unknow error at ";
      table+= "0x"; 
      if (address < 16){
      table +="0";
      }
      table += String(address, HEX);
      table += F("</td>\n");
    }
  } //for loop

      table += F("</tr>\n");
      table += F("</table>");
      table += F("</body>\n");
      table += F("</html>");


    String message = F("<!DOCTYPE html><html><head><style>body{background-color:#000;font-family:Arial;Color:#fff}</style><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta charset=\"utf-8\"><meta name=\"theme-color\" content=\"#042B3C\">");
    message += F("<meta http-equiv='refresh' content='"); //auto apdate web page
    message += String(autoUpdt);
    message += F("'/>"); 
    message += F("<style>hr{border:0;background-color:#570633;height:5px}table{font-family:arial,sans-serif;border-collapse:collapse;width:100%}th{background-color:#00473d;border:1px solid #006356;text-align:left;padding:8px}td{border:1px solid #004763;text-align:left;padding:8px}tr:nth-child(even){background-color:#0a0307}tr:nth-child(odd){background-color:#003e57}a,a:active,a:visited{color:#fff;text-decoration:none}a:hover{color:#ef0;text-decoration:none}</style></head><body>");
    message += F("<font color=\"#ffff00\"> Devices found: ");
    message += nDevices;
    message += F("</font>");
    message += F(" | ");
    message += F("<font color=\"#32ff00\"> Updates every: ");
    message += String(autoUpdt);
    message += " Sec.";
    message += F("</font>");
    message += F("<hr>");
    message += table;

  
 server.send(200, "text/html", message );
});
//============= I2C address server =====================================//



  //========================FileServer======================================================//
  server.onNotFound([]() {                              // If the client requests any URI
    server.sendHeader("access-control-allow-origin", "*");
    if (SPIFFS.exists(server.uri()))
    {
      // security check
      if (server.hasHeader("Cookie") ) {
        String cookie = server.header("Cookie"), sessionToken = "111v6566v3c363498y6g3qz";
        if (cookie.indexOf(sessionToken) != -1 ) {


          String ContentType = "text/plain";
          if (server.uri().endsWith(".htm")) {
            ContentType = "text/html";
            server.sendHeader("Content-Encoding", "gzip");  // gziped header if html
          }
          else if (server.uri().endsWith(".html")) {
            ContentType = "text/html";
            server.sendHeader("Content-Encoding", "gzip");  // gziped header if html
          }
          else if (server.uri().endsWith(".css"))  ContentType = "text/css";
          else if (server.uri().endsWith(".js"))   ContentType = "application/javascript";
          else if (server.uri().endsWith(".png"))  ContentType = "image/png";
          else if (server.uri().endsWith(".gif"))  ContentType = "image/gif";
          else if (server.uri().endsWith(".jpg"))  ContentType = "image/jpeg";
          else if (server.uri().endsWith(".ico"))  ContentType = "image/x-icon";
          else if (server.uri().endsWith(".xml"))  ContentType = "text/xml";
          else if (server.uri().endsWith(".pdf"))  ContentType = "application/x-pdf";
          else if (server.uri().endsWith(".zip"))  ContentType = "application/x-zip";
          else if (server.uri().endsWith(".gz"))   ContentType = "application/x-gzip";


          File file = SPIFFS.open(server.uri(), "r");     // Open it
          server.streamFile(file, ContentType);    // And send it to the client
          file.close();                                          // Then close the file again

          //IP_addToList(server.client().remoteIP().toString() + server.uri()+ " | " + millis());


        } else {
          server.send(200, "text/html", "File exist, but your cookie is not valid!");  // main page
          IP_addToList(server.client().remoteIP().toString() + " | " +  server.uri() + " | Not valid cookie !");
        }
      } else {
        server.send(200, "text/html", "File exist, but you are not authorized!");  // main page
        IP_addToList(server.client().remoteIP().toString() + " | " +  server.uri() + " | Not authorized !");
      }

    }
    else
    {
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
      IP_addToList(server.client().remoteIP().toString() + " | " +  server.uri() + " | Not Found !");
    }

  });
  //========================FileServer======================================================//


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
    digitalWrite(indicPinBLUE, LOW); // indication
    delayMicroseconds(200);
    digitalWrite(indicPinBLUE, HIGH); // indication
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


  server.begin();
  Serial1.println(F("HTTP server started"));


    //=====Count Sensors========///
  DS18B20.begin();
  numberOfDevices = DS18B20.getDeviceCount();
  Serial1.print(F( "Found DS18B20 Devices: " ));
  Serial1.println( numberOfDevices );
  //========CountEnd==========///

  // set resolution
  DS18B20.setResolution(EXT_SENSOR1, 12);
  // This line disables delay in the library DallasTemperature
  DS18B20.setWaitForConversion(false);



  //End of setup
  Serial1.println("===============================");
  Serial1.println(F("End of setup. Starting loop..."));
} //End of void Setup loop.


//=========== Main program loop starts here==============/
void loop(void) {                   //main loop start
  handleMeasurements();
  wm.process();
  server.handleClient();
  ArduinoOTA.handle(); // for OTA
  webSocketSRVR.loop();
  

  //=========loopsCounter========================//
  loopsCount++;
  if (millis() > prevMillisLoops + 500) {
    prevMillisLoops =  millis();
    loopsPerSec = loopsCount;
    loopsPerSec = loopsPerSec * 2;
    loopsCount = 0;
  }
  //=========loopsCounter========================//


  // in offline mode check for AP peridiodicly, if available then reboot.
  if(WifiOnlineStateFlag == false && millis() - wifiApCheckMillLast > 120000)
  {
      wifiApCheckMillLast = millis();
      Serial1.println("===============================");
      Serial1.print("Checking if "); // if AP available
      Serial1.print(WiFi.SSID());
      Serial1.println(" available");
  
      int n = WiFi.scanNetworks(); // n of networks
      bool foundCorrectSSID = false;
      Serial1.println("Found " + String(n) + " networks");
      for(int x=0; x!=n; x++)
      {
         Serial1.println(WiFi.SSID(x));
         if(WiFi.SSID(x)==WiFi.SSID())
         {
          foundCorrectSSID = true;
         }
      }
     Serial1.println("----");
     if(foundCorrectSSID)
     {
      Serial1.println("Found " + String(WiFi.SSID()));
      Serial1.println("Restarting ESP!!!");
      delay(1000);
      ESP.restart();
     }
     else
     {
      Serial1.println("Not Found " + String(WiFi.SSID()));
     }
  }


  // Lost connection with wi-fi
  if ((WiFi.status() != WL_CONNECTED) && (WifiOnlineStateFlag == true)) {
    Serial1.println("===============================");
    Serial1.println(F("Lost connection to wi-fi"));
    // if wi-fi is not reconnected within 60 second then reboot ESP
    for (int i = 60; i >= 0; i--) {

      Serial1.print("Restarting in: ");
      Serial1.print(i);
      Serial1.println(" sec.");

      delaySmart(750);
      

      if (WiFi.status() == WL_CONNECTED) {
        i = -1;
        Serial1.println(F("Reconnected to wi-fi"));
      }
    }
    if ((WiFi.status() != WL_CONNECTED) && (WifiOnlineStateFlag == true)) {
      ESP.restart();
    }
  }
  ///==========AP State==========//

  
  webSocketSRVR.loop();

  ///////======SENSORS========/////

  //Make requst
  if (millis() - previousMillisSenseUp >= waitSenseInt) {
    // save the last time temperature was updated
    previousMillisSenseUp = millis();


    ///Meausure temperature

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

    
    //==== done

    // report errors after evry reading cicle (0-15 errors)
    DS_ErrorsResult = DS_Errors;
    
    ReadingDone = true;

    delaySmart(5);

    // blink LED after request
    digitalWrite(indicPinBLUE, LOW);
    delayMicroseconds(50);
    digitalWrite(indicPinBLUE, HIGH);
    delayMicroseconds(50);
    digitalWrite(indicPinBLUE, LOW);
    delayMicroseconds(50);
    digitalWrite(indicPinBLUE, HIGH);
  }



  // ==
  if (ReadingDone == true ) { ///
    ReadingDone = false; // reset flag

 
    delaySmart(55);
    Serial1.print("Sensor1:  ");
    Serial1.println(PWR_Ctrl_tempOne); // Temperature to serial
    delaySmart(55);


    digitalWrite(indicPinBLUE, LOW);
    delayMicroseconds(50);
    digitalWrite(indicPinBLUE, HIGH);
    delayMicroseconds(50);
    digitalWrite(indicPinBLUE, LOW);
    delayMicroseconds(50);
    digitalWrite(indicPinBLUE, HIGH);
  }////


  /////======SENSORS-END=======///////



// ==============temp warnings
  if(!sentTempWarning && DsReadGood(PWR_Ctrl_tempOne) && PWR_Ctrl_tempOne > 32){
    sentTempWarning=true;
    telegramPrivateMsg("Warning! High temperature detected in distribution board, t:"+String(PWR_Ctrl_tempOne)+"C");
  }
// ==============temp warnings

  webSocketSRVR.loop();


  //////====SpeakThingsStart=====///
  if (WifiOnlineStateFlag == false){ // not run in offline
  previousMillisThingSpeakUpdt = millis();
  }
  
  if (millis() - previousMillisThingSpeakUpdt >= ThingSpkPostInt && SendDataToServer == true) {
    // save the last time was report to thingSpeak
    previousMillisThingSpeakUpdt = millis();



    // thingSpeak
    HTTPClient http;

    http.begin(client, "http://api.thingspeak.com/update");
    http.addHeader("X-THINGSPEAKAPIKEY", writeAPIKey);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");


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

    // if starts with "&" then remove it
    if (body.startsWith("&")) {
      body.remove(0, 1);
    }


    int httpCode = http.POST(body);
    http.end();

    delaySmart(5);

    Serial1.print("HTTP Code: ");
    Serial1.println(httpCode);
    if (httpCode > 0) {
      //everything good
      internetDown = false;
      thingSpeakError = false;
    }
    else
    {
      // Error
      thingSpeakError = true;
      // do some testing here
      Serial1.println("Can't send data to ThingSpeak");
      delaySmart(200);
      Serial1.println("Let check if Google accessible...");

      HTTPClient http;
      http.begin(client, "http://google.com");
      if (WiFi.status() == WL_CONNECTED && http.GET() > 0)
      {
        Serial1.println("Result: Google still accessible");
        internetDown = false;
      }
      else
      {
        Serial1.println("Result: Google not accessible also");
        internetDown = true;

        Serial1.println("Cheking wi-fi...");
        if (WiFi.status() == WL_CONNECTED)
        {
          Serial1.println("Result: Wi-Fi still connected.");
        }
        else
        {
          Serial1.println("Result: Error! Wi-Fi not connected.");
        }
      }
      http.end();
    }

  }
  //////====ThingSpeakEnd=====///

  webSocketSRVR.loop();



  ////====================UpTimeReportStart======================///
  // calculate

  secs = millis() / 1000; //convect milliseconds to seconds
  mins = secs / 60; //convert seconds to minutes
  hours = mins / 60; //convert minutes to hours
  days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the coverted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the coverted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the coverted hours to days in order to display 23 hours max



  // report via serial
  if (millis() - previousMillisUpTimeReport >= UpTimeReportInt) {
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



  
  webSocketSRVR.loop();



// //====================================wifi=beacon============================ */
//  if (millis() > prevMillisBeacon + 100 && DsReadGood(PWR_Ctrl_tempOne)) {
//    // getting data for SSID
//    String dataSend = "PWR_Ctrl: ";
//    dataSend += String((round(PWR_Ctrl_tempOne * 10.0) / 10.0), 1);
//    dataSend += " 'C ";
//    wifiBeaconSend(dataSend);
//    prevMillisBeacon = millis();
//  }
// //====================================wifi=beacon============================ */
 


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


//===============================WebLog================================//
void webLog(String dataLog) {
// if (internetDown == false){
//      HTTPClient http;
//      http.begin(client, "http://maker.ifttt.com/trigger/WebLog/with/key/dmVrPNOuov_XEoVIsX8FPu");
//      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
//      http.POST("value1=" + dataLog);
//      Serial1.println("===============================");
//      Serial1.println("WebLog");
//      Serial1.println("Response from server:");
//      http.writeToStream(&Serial1);
//      Serial1.println();
//      http.end();
// }
}
//===============================WebLog================================//


//===============================jsonFeedfun================================//
String jsonFeedGet() {
  
StaticJsonDocument<1024> root;

  root["AC_Voltage"] = AC_Voltage;
  root["AC_Current"] = AC_Current;
  root["Power"] = Power;
  root["Frequency"] = Frequency;
  root["PowerFactor"] = PowerFactor;
  root["Energy"] = Energy;
  root["PWR_Ctrl_tempOne"] = PWR_Ctrl_tempOne;
  root["DS_ErrorsResult"] = DS_ErrorsResult;
  root["thingSpeakError"] =  thingSpeakError;
  root["internetDown"] =  internetDown;
  root["SendDataToServer"] =  SendDataToServer;
  root["PublicAccess"] =  PublicAccess;
  root["loopsPerSec"] =  loopsPerSec;
  root["FreeHeap"] = ESP.getFreeHeap();
  
      time_t TimeNow = time(nullptr);

  root["UnixTimeStamp"] = TimeNow;
  root["TimeNow"] = ctime(&TimeNow);
  root["Millis"] =  millis();      

  String output;
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
        String readyData = smartData();
        webSocketSRVR.sendTXT(num, readyData);
      }

      if (text.startsWith("jsonDataGet")) {
        String readyData = jsonFeedGet();
        webSocketSRVR.sendTXT(num, readyData);
      }
      
      break;

    //===

    case WStype_DISCONNECTED: {  // if the client is disconnected
        Serial1.println("===============================");
        Serial1.printf("[%u] Disconnected!\n", num);
      }
      break;

    //===

    case WStype_CONNECTED: {    // if a new websocket connection is established
        IPAddress ip_rem = webSocketSRVR.remoteIP(num);
        Serial1.println("===============================");
        Serial1.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip_rem[0], ip_rem[1], ip_rem[2], ip_rem[3], payload);
        IP_addToList(String(ip_rem[0]) + "." + String(ip_rem[1]) + "." + String(ip_rem[2]) + "." + String(ip_rem[3]) + " | WebSocket connect");
      }
      break;

    //===

    case WStype_BIN:
      Serial1.printf("[%u] get binary length: %u\n", num, length);
      webSocketSRVR.sendBIN(num, payload, length);
      break;

      //===

  }
}
//=======================webSocketServerEND============================================//




//=======================IPsaver==================================================//
// seve ip adresses to list
void IP_addToList(String ipStr) {
  bool emSp = false;
  for (int x = 0; emSp == false; x++) {
    if (IP_History[x].length() < 3) { // found empty place
      emSp = true; // found empty space, ready to write, no need to check more
      bool newIp = true; // lets assume that ip is new
      for (int r = x; r >= 0 ; r--) { // go down and chech if it is new
        if (IP_History[r] == ipStr) {
          newIp = false; // IP is not new, dont write it.
          IP_HistoryHits[r]++;
          
            // == weblog if hit multiple times
            if((IP_HistoryHits[r]>=10 && IP_HistoryHits[r]<11) || (IP_HistoryHits[r]>=100 && IP_HistoryHits[r]<101) || (IP_HistoryHits[r]>=200 && IP_HistoryHits[r]<201)){
               // (if not local)
               if (!ipStr.startsWith("192.168.4.", 0)) {
               webLog("PowerControl IP " + ipStr + " | Hits: " + IP_HistoryHits[r]);
               }
              }///==

          
        }
      }
      if (newIp == true) { // check if IP is new
        IP_History[x] = ipStr; // write it to array
        IP_HistoryHits[x]++;
        

         // weblog as well
         // (if not local)
         if (!ipStr.startsWith("192.168.4.", 0)) {
         webLog("PowerControl IP " + ipStr );
         }
         

        if (IP_History[10].length() > 3) { // now, check if array is full
          for (int i = 0; i != 10; i++) { // if so, move and free some space
            IP_History[i] = IP_History[i + 1];
            IP_HistoryHits[i]=IP_HistoryHits[i+1];
          }
          IP_History[10] = ""; /// clear the last
          IP_HistoryHits[10] = 0;
        }

      }
    }
  }

}
//=======================IPsaver==================================================//





//=================================//SmartDataFunction//=========================================================//
String smartData() // start of web function, it returns webpage string
{
  //////////////////////////////////////// page start

  // RED #fe3437
  // GREEN #08e18e
  // YELLOW #FFDE00
  // ORANGE #FF6300

  String  web =  "<style>body { background-color: #000000; font-family: Arial; Color: white; }</style>\n";
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
  //Wifi signal
  rssi = WiFi.RSSI();
  web += "<li>RSSI: ";
  if (rssi <= -85) {
    web += "<font color=\"#fe3437\">";
 web += rssi;
 web += " dbm";
    web += "</font>";
  }
  else{
  web += rssi;
  web += " dbm";
    }

  web += " | FreeHeap: ";
  web += ESP.getFreeHeap();
  web += " B";

  // === online report start
  // === report if one minute passed
  if (WifiOnlineStateFlag == true) {
    //
    web += " | ";

    //

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
    //
  }  // === online report end

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

  //
  web += "<li>Now: ";          // current time
  time_t TimeNow = time(nullptr);
  web += ctime(&TimeNow); // sec to date, time
  web += "</li>";
  web += "<hr>";

  //

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

  if (WifiOnlineStateFlag == false) {

    web += "<font color=\"#fe3437\">";
    web += " [ ! ] [ OFFLINE CONTROL MODE! ]";
    web += "</font>";
    web += "<hr>";
  }

  if (internetDown == true) {
    web += "<font color=\"#fe3437\">";
    web += " [ ! ]  [ INTERNET IS DOWN ! ]";
    web += "</font>";
    web += "<hr>";
  }

  if (PublicAccess == true) {
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

////====================================wifi=beaconFun============================ */
//void wifiBeaconSend(String SSID_data){
//    uint16_t packetLength = 0;
//    uint8_t BeaconPacket[128] = {};
//    
//    //fixed part
//    uint8_t BeaconStartPart[56] = { 0x80, 0x00, 0x00, 0x00,
//                /*4*/   0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
//                /*MAC 10*/  0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
//                /*MAC 16*/  0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
//                /*sequance number 22*/  0xc0, 0x6c,
//                /*timestamp 24*/  0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
//                /*beacon interval 32*/  0x64, 0x00,
//                /*Capabilities 34*/  0x01, 0x04,
//                /*36*/  0x00
//              };
//
//    //varied part (example)
//    // /*SSID LENGHTH */    0x08,
//    //       /* SSID */     0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
//
//    //fixed part
//    uint8_t BeaconEndPart[16] = {
//      /*rates*/    0x01, 0x08,
//      /*rates*/    0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
//      0x03, 0x01,
//      /*channel*/    0x06
//    };
//
//    //put starting part in packet
//    for (uint8_t x = 0; x < 37; x++) {
//      BeaconPacket[0 + x] = BeaconStartPart[x];
//    }
//    packetLength = packetLength + 37;
//
//    // put lenghth in packet and varible
//    BeaconPacket[37] = SSID_data.length();
//    packetLength = packetLength + 1;
//
//    // put SSID_data in packet
//    for (uint8_t x = 0; x != SSID_data.length(); x++) {
//      BeaconPacket[38 + x] = SSID_data[x];
//    }
//    packetLength = packetLength + SSID_data.length();
//
//    //put ending part in packet
//    for (uint8_t x = 0; x < 13; x++) {
//      BeaconPacket[packetLength + x] = BeaconEndPart[x];
//    }
//    packetLength = packetLength + 13;
//
//    wifi_send_pkt_freedom(BeaconPacket, packetLength, 0);
//    
//}
//  //====================================wifi=beaconFun============================ */


//=======poweroff
 void powerOffSequence(String reason){
  telegramPrivateMsg("PowerOFF sequence triggered, reason: " + reason);
  digitalWrite(PowerOff_pin, HIGH);
  delay(800);
  // Device should be off by now
  digitalWrite(PowerOff_pin, LOW);
  delay(2000);
  telegramPrivateMsg("Error, PowerOFF sequence Fail!, trying again...");
  // Second attempt
  digitalWrite(PowerOff_pin, HIGH);
  delay(800);
  digitalWrite(PowerOff_pin, LOW);
  delay(4000);
  // if not off by now send error message
  telegramPrivateMsg("Error, PowerOFF sequence Fail!");
  delay(4000);
 }
//=========



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
// I send warnings to telegram via IFTTT, change to what you like here.
void telegramPrivateMsg(String msg) {
 if (internetDown == false && WiFi.status() == WL_CONNECTED){
      HTTPClient http;
      http.setTimeout(2000);
      http.begin(client, F("http://maker.ifttt.com/trigger/telegramPrivateMsg/with/key/xxxxxxYourApiKeyxxxxxxxxx"));
      http.addHeader(F("Content-Type"), F("application/x-www-form-urlencoded"));
      http.POST("value1=" + msg);
      Serial1.println("===============================");
      Serial1.println(F("Private Telegram Message"));
      Serial1.println(F("Response from server:"));
      http.writeToStream(&Serial1);
      Serial1.println();
      http.end();
 }
}
//===============================Telegram_Message======================//

//===============================handleMeasurements======================//
void handleMeasurements()
{
    // ===== Measure
  if (millis() > prevMillisMeasure + 800) {
    AC_Voltage = pzem.voltage();
    AC_Current = pzem.current();
    Power = pzem.power();
    Frequency = pzem.frequency();
    Energy = pzem.energy();
    PowerFactor = pzem.pf();
    prevMillisMeasure =  millis();
}// ===== Measure



// emergancy power off when overvoltage ===
    if(!isnan(AC_Voltage) && AC_Voltage>283){
      powerOffSequence("Overvoltage protection, voltage "+ String(AC_Voltage) + "V detected");
    }
// emergancy power off when overvoltage ===





//=====Dislaying the data on LCD =========================//
  if(!isnan(AC_Voltage) && !isnan(Power)){
      lcd_drive(0, String((round(AC_Voltage * 10.0) / 10.0), 1) + "V " + String((round(Power * 10.0) / 10.0), 1) + "W ");
  } else {
      lcd_drive(0, "Can't measure!");
  }

  if(WifiOnlineStateFlag == true && WiFi.status() == WL_CONNECTED)
  { 
       if(showInfoSet==1)
       {
        //=======time to LCD
        time_t TimeNow = time(nullptr);
        String timeStr = ctime(&TimeNow);
        timeStr = timeStr.substring(0, timeStr.length() - 1); // remove last char
        timeStr.remove(0, 4); // revove first 4
        lcd_drive(1, timeStr);
        ///=======time to LCD
       }
       if(showInfoSet==2)
       {
          if(!isnan(AC_Current) && !isnan(Frequency)){
            lcd_drive(1, String((round(AC_Current * 10.0) / 10.0), 1) + "A "+ String((round(Frequency * 10.0) / 10.0), 1) + "Hz ");
          }
          else 
          {
            lcd_drive(1,"Can't measure!");
          }
       }
       if(showInfoSet==3)
       {
         if(!isnan(AC_Current) && !isnan(Frequency)){
           lcd_drive(1, "PF:" + String(PowerFactor*100,0) + "% t:" + String((round(PWR_Ctrl_tempOne * 10.0) / 10.0), 1) + "C");
         }
         else 
         {
           lcd_drive(1,"Can't measure!");
         }
       }
  }
  else
  {
  lcd_drive(1, "OFFLINE");
  }
//=====Dislaying the data on LCD end =========================//





//========= switch showset
if (millis() > prevMillisSwitchSetMill + 6000) {
   prevMillisSwitchSetMill =  millis();
   showInfoSet++;
    if(showInfoSet>=4)
    {
      showInfoSet=0;
    }
}
//========


}
//===============================handleMeasurements======================//
