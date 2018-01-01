/************************************************************************************/
/*                                                                                  */
/*     IR_Blaster_360 2.7beta                                                           */
/*                                                                                  */
/*  https://github.com/phili76/IRBlaster360                                         */
/*                                                                                  */
/*  https://github.com/mdhiggins/ESP8266-HTTP-IR-Blaster                            */
/*  Stand: 31.12.2017                                                               */
/*                                                                                  */
/*  Bibliotheken:                                                                   */
/*    ArduinoJson                                                                   */
/*    NTPClient                                                                     */
/*    IRremoteESP8266                                                               */
/*    WiFiManager                                                                   */
/*    NTPClient                                                                     */
/*    Time                                                                          */
/************************************************************************************/

/**************************************************************************
   Includes
**************************************************************************/
#include <FS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
#include <Dns.h>
#include <NTPClient.h>
#include <TimeLib.h>


/**************************************************************************
   Defines
**************************************************************************/
#define DEBUG
#define IR_SEND_PIN     D1
#define IR_RECEIVE_PIN  D4
#define CONFIG_PIN      D7
#define LED_PIN         D2

const String FIRMWARE_NAME = "IR Blaster 360";
const String VERSION       = "v2.7beta";

/**************************************************************************
   Debug
**************************************************************************/
#ifdef DEBUG
#define DEBUG_PRINT(x)  Serial.println (x)
#else
#define DEBUG_PRINT(x)
#endif

/**************************************************************************
   Variables
**************************************************************************/
char passcode[40] = "";
char host_name[40] = "";
char port_str[20] = "80";

class Code 
{
  public:
    char encoding[14] = "";
    char address[20] = "";
    char command[40] = "";
    char data[40] = "";
    String raw = "";
    int bits = 0;
    char timestamp[40] = "";
    bool valid = false;
};

Code last_recv;
Code last_recv_2;
Code last_recv_3;
Code last_recv_4;
Code last_recv_5;
Code last_send;
Code last_send_2;
Code last_send_3;
Code last_send_4;
Code last_send_5;


// led
Ticker ticker;

// wlan
const char *wifi_config_name = "IRBlaster Configuration";
int port = 80;
ESP8266WebServer server(port);
bool shouldSaveConfig = false;                                // Flag for saving data

// ir
IRrecv irrecv(IR_RECEIVE_PIN);
IRsend irsend(IR_SEND_PIN);

// multicast
WiFiUDP WiFiUdp;
IPAddress ipMulti(239, 0, 0, 57);
unsigned int portMulti = 12345;      // local port to listen on
String deviceID = "";

//NTP
bool getTime = true;                                    // Set to false to disable querying for the time
const int timeOffset = 3600;                                  // Timezone offset in seconds
WiFiUDP ntpUDP;
const char* poolServerName = "europe.pool.ntp.org";
char boottime[40] = "";                                       // 
NTPClient timeClient(ntpUDP, poolServerName, timeOffset, 1800000);

time_t timedate = 0;

String javaScript;
String htmlHeader;
String htmlFooter;

/**************************************************************************
   Callback notifying us of the need to save config
**************************************************************************/
void saveConfigCallback ()
{
  DEBUG_PRINT("Should save config");
  shouldSaveConfig = true;
}

/**************************************************************************
   toggle LED
**************************************************************************/
void tick()
{
  int state = digitalRead(LED_PIN);   // get the current state of LED pin
  digitalWrite(LED_PIN, !state);       // set pin to the opposite state
}

/**************************************************************************
   turn of LED after timeout
**************************************************************************/
void disableLed()
{
  DEBUG_PRINT("Turning off the LED to save power.");
  digitalWrite(LED_PIN, HIGH);                          // Shut down the LED
  ticker.detach();                                      // Stopping the ticker
}

/**************************************************************************
   Gets called when WiFiManager enters configuration mode
**************************************************************************/
void configModeCallback (WiFiManager *myWiFiManager)
{
  DEBUG_PRINT("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  DEBUG_PRINT(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

/**************************************************************************
   Setup WiFi connection
**************************************************************************/
bool setupWifi(bool resetConf)
{
  // set led pin as output
  pinMode(LED_PIN, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // reset settings - for testing
  if (resetConf) wifiManager.resetSettings();

  // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (SPIFFS.begin()) 
  {
    DEBUG_PRINT("mounted file system");
    if (SPIFFS.exists("/config.json")) 
    {
      //file exists, reading and loading
      DEBUG_PRINT("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_PRINT("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          DEBUG_PRINT("\nparsed json");

          if (json.containsKey("hostname")) strncpy(host_name, json["hostname"], 40);
          if (json.containsKey("passcode")) strncpy(passcode, json["passcode"], 40);
          if (json.containsKey("port_str")) {
            strncpy(port_str, json["port_str"], 20);
            port = atoi(json["port_str"]);
          }
        } else {
          DEBUG_PRINT("failed to load json config");
        }
      }
    }
  } 
  else 
  {
    DEBUG_PRINT("failed to mount FS");
  }
  
  WiFiManagerParameter custom_hostname("hostname", "Choose a hostname to this IRBlaster", host_name, 40);
  wifiManager.addParameter(&custom_hostname);
  WiFiManagerParameter custom_passcode("passcode", "Choose a passcode", passcode, 40);
  wifiManager.addParameter(&custom_passcode);
  WiFiManagerParameter custom_port("port_str", "Choose a port", port_str, 40);
  wifiManager.addParameter(&custom_port);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(wifi_config_name)) 
  {
    DEBUG_PRINT("failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  // if you get here you have connected to the WiFi
  strncpy(host_name, custom_hostname.getValue(), 40);
  strncpy(passcode, custom_passcode.getValue(), 40);
  strncpy(port_str, custom_port.getValue(), 20);
  port = atoi(port_str);

  if (port != 80) {
    DEBUG_PRINT("Default port changed");
    server = ESP8266WebServer(port);
  }

  Serial.println("WiFi connected! User chose hostname '" + String(host_name) + String("' passcode '") + String(passcode) + "' and port '" + String(port_str) + "'");

  // save the custom parameters to FS
  if (shouldSaveConfig) 
  {
    DEBUG_PRINT(" config...");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["hostname"] = host_name;
    json["passcode"] = passcode;
    json["port_str"] = port_str;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DEBUG_PRINT("failed to open config file for writing");
    }

    json.printTo(Serial);
    DEBUG_PRINT("");
    json.printTo(configFile);
    configFile.close();
    //e nd save
  }
  ticker.detach();

  // keep LED on
  digitalWrite(LED_PIN, LOW);
  return true;
}

/**************************************************************************
   Setup web server and IR receiver/blaster
**************************************************************************/
void setup()
{
  // Initialize serial
  Serial.begin(115200);
  DEBUG_PRINT("");
  DEBUG_PRINT("ESP8266 IR Controller");
  pinMode(CONFIG_PIN, INPUT_PULLUP);
  if (!setupWifi(digitalRead(CONFIG_PIN) == LOW))
    return;
  if (host_name[0] == 0 ) strncpy(host_name, "irblaster", 40);    //set default hostname when not set!
    WiFi.hostname(host_name);
    while (WiFi.status() != WL_CONNECTED) 
    {
      delay(500);
      Serial.print(".");
    }

    wifi_set_sleep_type(LIGHT_SLEEP_T);
    digitalWrite(LED_PIN, LOW);
    // Turn off the led in 2s
    ticker.attach(2, disableLed);

    // Configure mDNS
    if (MDNS.begin(host_name)) DEBUG_PRINT("mDNS started. Hostname is set to " + String(host_name) + ".local");
    MDNS.addService("http", "tcp", port); // Announce the ESP as an HTTP service
    
    DEBUG_PRINT("URL to send commands: http://" + String(host_name) + ".local:" + port_str);

    if (getTime) 
    {
      timeClient.begin();                                                               // Get the time
      if (timeClient.update()) 
      {
        setTime(timeClient.getEpochTime());
        String boottimetemp = String(hour()) + ":" + String(minute()) + " " + String(day()) + "." + String(month()) + "." + String(year());
//        strncpy(boottime, String(timeClient.getFormattedTime()).c_str(), 40);           // If we got time set boottime
        strncpy(boottime, boottimetemp.c_str(), 40);           // If we got time set boottime
      }
      else
      { 
        getTime = false;                                                             // deactivate if error occours
      }  
    }  

    // Configure the server
    // JSON handler for more complicated IR blaster routines
    server.on("/json", []()
    {
      DEBUG_PRINT("Connection received - JSON");

      // disable the receiver
      irrecv.disableIRIn();

      DynamicJsonBuffer jsonBuffer;
      JsonArray& root = jsonBuffer.parseArray(server.arg("plain"));

      if (!root.success())
      {
        DEBUG_PRINT("JSON parsing failed");

        // http response
        server.send(400, "text/html", "JSON parsing failed");
      }
      else if (server.arg("pass") != passcode)
      {
        DEBUG_PRINT("Unauthorized access");

        // http response
        server.send(401, "text/html", "Invalid passcode");
      }
      else
      {
        // http response
        server.send(200, "text/html", "Code sent: /json?plain=" + server.arg("plain"));

        digitalWrite(LED_PIN, LOW);
        ticker.attach(0.5, disableLed);
        for (int x = 0; x < root.size(); x++) {
          String type = root[x]["type"];
          String ip = root[x]["ip"];
          int rdelay = root[x]["rdelay"];
          int pulse = root[x]["pulse"];
          int pdelay = root[x]["pdelay"];
          int repeat = root[x]["repeat"];
          int cdelay = root[x]["cdelay"];

          if (pulse <= 0) pulse = 1; // Make sure pulse isn't 0
          if (repeat <= 0) repeat = 1; // Make sure repeat isn't 0
          if (pdelay <= 0) pdelay = 100; // Default pdelay
          if (rdelay <= 0) rdelay = 1000; // Default rdelay
          if (cdelay <= 0) cdelay = 500; // default delay between two commands

          if (type == "delay") {
            delay(rdelay);
          } else if (type == "raw") {
            JsonArray &raw = root[x]["data"]; // Array of unsigned int values for the raw signal
            int khz = root[x]["khz"];
            if (khz <= 0) khz = 38; // Default to 38khz if not set
            rawblast(raw, khz, rdelay, pulse, pdelay, repeat);
          } else if (type == "roku") {
            String data = root[x]["data"];
            rokuCommand(ip, data);
          } else {
            String data = root[x]["data"];
            long address = root[x]["address"];
            int len = root[x]["length"];
            irblast(type, data, len, rdelay, pulse, pdelay, repeat, address);
          }

          if (x + 1 < root.size())
          {
            DEBUG_PRINT("wait between two commands");
            delay(cdelay);
          }
        }
      }

      // enable the receiver
      irrecv.enableIRIn();
    });

    server.on("/received", []() {
      DEBUG_PRINT("Connection received: /received");
      int id = server.arg("id").toInt();
      String output;
      if (id == 1 && last_recv.valid) {
        sendCodePage(last_recv);
      } else if (id == 2 && last_recv_2.valid) {
        sendCodePage(last_recv_2);
      } else if (id == 3 && last_recv_3.valid) {
        sendCodePage(last_recv_3);
      } else if (id == 4 && last_recv_4.valid) {
        sendCodePage(last_recv_4);
      } else if (id == 5 && last_recv_5.valid) {
        sendCodePage(last_recv_5);
      } else {
        sendHomePage("Code does not exist", "Alert", 2, 404); // 404
      }
    });
    server.on("/config", Handle_config);

    server.on("/upload", Handle_upload);

    server.on("/update", HTTP_POST, Handle_update, FlashESP);

    server.on("/style", Handle_Style);

    server.on("/reset", Handle_ResetWiFi);

    server.on("/reboot", Handle_Reboot);
    
    server.on("/", []() {
      DEBUG_PRINT("Connection received: /");
      sendHomePage(); // 200
    });

    server.begin();
    DEBUG_PRINT("HTTP Server started on port " + String(port));

    // create unique DeviceID and send key value information
    deviceID = "IR_Blaster " + GetChipID();
    sendMultiCast(CreateKVPInitString());
    sendMultiCast(CreateKVPSystemInfoString());
    sendMultiCast(CreateKVPCommandURLString());
    sendKVPCodeString();

    // initialize the IR interface
    irsend.begin();
    irrecv.enableIRIn();
    DEBUG_PRINT("Ready to send and receive IR signals");
    
  }
  void Handle_config()
  {
    DEBUG_PRINT("Connection received - /config");
    sendConfigPage();  //todo
  }

  void Handle_Reboot(){
    ESP.restart();
  }

  void Handle_ResetWiFi()
  {
    DEBUG_PRINT("Reset WiFi settings and reboot gateway");
    // define WiFiManager instance
    WiFiManager wifiManager;

    // perform reset of WiFi setting
    wifiManager.resetSettings();

    // restart ESP
    ESP.restart();

    // wait some time
    delay(1000);
}
/**************************************************************************
   Over the air update
**************************************************************************/
void FlashESP()
{
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    Serial.setDebugOutput(false);
    WiFiUDP::stopAll();
    Serial.printf("Update: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    //start with max available size
    if (!Update.begin(maxSketchSpace))
    {

      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    //true to set the size to the current progress
    if (Update.end(true))
    {
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    }
    else
    {
      Update.printError(Serial);
    }
    Serial.setDebugOutput(false);
  }
  yield();
}
void Handle_Style()
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/css", GetStyle());
}

void Handle_upload()
{
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", GetUploadHTML());
}
void Handle_update()
{
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  bool error = Update.hasError();
  server.send(200, "application/json", (error) ? "{\"success\":false}" : "{\"success\":true}");

  if (!error)
  {
    ESP.restart();
  }
}
String GetUploadHTML()
{
  return F("<!DOCTYPE html>"
    "<html lang=\"en\" class=\"no-js\">"
    "<head>"
    "<meta charset=\"utf-8\">"
    "<title>IR - Firmware-Update</title>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\" />"
    "<link href=\"./style\" rel=\"stylesheet\">"
    "</head>"
    "<body>"
    "<div class=\"container\" role=\"main\">"
    "<h1><b>ESP8266 IR Controller - Firmware-Update</b></h1>"
    "<form method=\"post\" action=\"/update\" enctype=\"multipart/form-data\" novalidate class=\"box\">"
    "<div class=\"box__input\">"
    "<svg class=\"box__icon\" xmlns=\"http://www.w3.org/2000/svg\" width=\"50\" height=\"43\" viewBox=\"0 0 50 43\"><path d=\"M48.4 26.5c-.9 0-1.7.7-1.7 1.7v11.6h-43.3v-11.6c0-.9-.7-1.7-1.7-1.7s-1.7.7-1.7 1.7v13.2c0 .9.7 1.7 1.7 1.7h46.7c.9 0 1.7-.7 1.7-1.7v-13.2c0-1-.7-1.7-1.7-1.7zm-24.5 6.1c.3.3.8.5 1.2.5.4 0 .9-.2 1.2-.5l10-11.6c.7-.7.7-1.7 0-2.4s-1.7-.7-2.4 0l-7.1 8.3v-25.3c0-.9-.7-1.7-1.7-1.7s-1.7.7-1.7 1.7v25.3l-7.1-8.3c-.7-.7-1.7-.7-2.4 0s-.7 1.7 0 2.4l10 11.6z\" /></svg>"
    "<input type=\"file\" name=\"update\" id=\"file\" class=\"box__file\" />"
    "<label for=\"file\">"
    "<strong>Select a firmware binary...</strong>"
    "<br /><span class=\"\"> or drop it here</span>."
    "</label>"
    "<button type=\"submit\" class=\"box__button\">Upload</button>"
    "</div>"
    "<div class=\"box__uploading\">Uploading&hellip;</div>"
    "<div class=\"box__success\">"
    "Complete!<br />"
    "ESP8266 IR Controller reboot now...<br />"
    "<progress value=\"0\" max=\"15\" id=\"progressBar\"></progress>"
    "</div>"
    "<div class=\"box__error\">Error! <span></span>.</div>"
    "</form>"
    "<footer></footer>"
    "</div>"
    "<script>"
    "function reboot() {"
    "var timeleft = 15;"
    "var downloadTimer = setInterval(function () {"
    "document.getElementById(\"progressBar\").value = 15 - --timeleft;"
    "if (timeleft <= 0) {"
    "clearInterval(downloadTimer);"
    "window.location.href = \"/\";"
    "}"
    "}, 1000);"
    "}"
    "'use strict';"
    "; (function (document, window, index) {"
    "var isAdvancedUpload = function () {"
    "var div = document.createElement('div');"
    "return (('draggable' in div) || ('ondragstart' in div && 'ondrop' in div)) && 'FormData' in window && 'FileReader' in window;"
    "}();"
    "var forms = document.querySelectorAll('.box');"
    "Array.prototype.forEach.call(forms, function (form) {"
    "var input = form.querySelector('input[type=\"file\"]'),"
    "label = form.querySelector('label'),"
    "errorMsg = form.querySelector('.box__error span'),"
    "restart = form.querySelectorAll('.box__restart'),"
    "droppedFiles = false,"
    "showFiles = function (files) {"
    "label.textContent = files.length > 1 ? (input.getAttribute('data-multiple-caption') || '').replace('{count}', files.length) : files[0].name;"
    "},"
    "triggerFormSubmit = function () {"
    "var event = document.createEvent('HTMLEvents');"
    "event.initEvent('submit', true, false);"
    "form.dispatchEvent(event);"
    "};"
    "var ajaxFlag = document.createElement('input');"
    "ajaxFlag.setAttribute('type', 'hidden');"
    "ajaxFlag.setAttribute('name', 'ajax');"
    "ajaxFlag.setAttribute('value', 1);"
    "form.appendChild(ajaxFlag);"
    "input.addEventListener('change', function (e) {"
    "showFiles(e.target.files);"
    "triggerFormSubmit();"
    "});"
    "if (isAdvancedUpload) {"
    "form.classList.add('has-advanced-upload');"
    "['drag', 'dragstart', 'dragend', 'dragover', 'dragenter', 'dragleave', 'drop'].forEach(function (event) {"
    "form.addEventListener(event, function (e) {"
    "e.preventDefault();"
    "e.stopPropagation();"
    "});"
    "});"
    "['dragover', 'dragenter'].forEach(function (event) {"
    "form.addEventListener(event, function () {"
    "form.classList.add('is-dragover');"
    "});"
    "});"
    "['dragleave', 'dragend', 'drop'].forEach(function (event) {"
    "form.addEventListener(event, function () {"
    "form.classList.remove('is-dragover');"
    "});"
    "});"
    "form.addEventListener('drop', function (e) {"
    "droppedFiles = e.dataTransfer.files;"
    "showFiles(droppedFiles);"
    "triggerFormSubmit();"
    "});"
    "}"
    "form.addEventListener('submit', function (e) {"
    "if (form.classList.contains('is-uploading')) return false;"
    "form.classList.add('is-uploading');"
    "form.classList.remove('is-error');"
    "if (isAdvancedUpload) {"
    "e.preventDefault();"
    "var ajaxData = new FormData(form);"
    "if (droppedFiles) {"
    "Array.prototype.forEach.call(droppedFiles, function (file) {"
    "ajaxData.append(input.getAttribute('name'), file);"
    "});"
    "}"
    "var ajax = new XMLHttpRequest();"
    "ajax.open(form.getAttribute('method'), form.getAttribute('action'), true);"
    "ajax.onload = function () {"
    "form.classList.remove('is-uploading');"
    "if (ajax.status >= 200 && ajax.status < 400) {"
    "var data = JSON.parse(ajax.responseText);"
    "form.classList.add(data.success == true ? 'is-success' : 'is-error');"
    "if (!data.success) {"
    "errorMsg.textContent = data.error;"
    "}"
    "else {"
    "reboot();"
    "}"
    "}"
    "else alert('Error. Please, contact the webmaster!');"
    "};"
    "ajax.onerror = function () {"
    "form.classList.remove('is-uploading');"
    "alert('Error. Please, try again!');"
    "};"
    "ajax.send(ajaxData);"
    "}"
    "else {"
    "var iframeName = 'uploadiframe' + new Date().getTime(),"
    "iframe = document.createElement('iframe');"
    "$iframe = $('<iframe name=\"' + iframeName + '\" style=\"display: none;\"></iframe>');"
    "iframe.setAttribute('name', iframeName);"
    "iframe.style.display = 'none';"
    "document.body.appendChild(iframe);"
    "form.setAttribute('target', iframeName);"
    "iframe.addEventListener('load', function () {"
    "var data = JSON.parse(iframe.contentDocument.body.innerHTML);"
    "form.classList.remove('is-uploading');"
    "form.classList.add(data.success == true ? 'is-success' : 'is-error');"
    "form.removeAttribute('target');"
    "if (!data.success) {"
    "errorMsg.textContent = data.error;"
    "}"
    "else {"
    "reboot();"
    "}"
    "iframe.parentNode.removeChild(iframe);"
    "});"
    "}"
    "});"
    "Array.prototype.forEach.call(restart, function (entry) {"
    "entry.addEventListener('click', function (e) {"
    "e.preventDefault();"
    "form.classList.remove('is-error', 'is-success');"
    "input.click();"
    "});"
    "});"
    "input.addEventListener('focus', function () { input.classList.add('has-focus'); });"
    "input.addEventListener('blur', function () { input.classList.remove('has-focus'); });"
    "});"
    "}(document, window, 0));"
    "</script>"
    "<script>(function (e, t, n) { var r = e.querySelectorAll(\"html\")[0]; r.className = r.className.replace(/(^|\\s)no-js(\\s|$)/, \"$1js$2\") })(document, window, 0);</script>"
    "</body>"
    "</html>");
}
String GetStyle()
{
  return F("body {"
    "    /*padding-top: 60px;*/"
    "    /*padding-bottom: 60px;*/"
    "    background: #ffffff;"
    "    color: #adadad;"
    "}"
    ""
    ".dashed {"
    "    border-bottom: 2px dashed #737373;"
    "    background-color: #474747 !important;"
    "}"
    ""
    ".pull-right {"
    "    float: right;"
    "}"
    ""
    ".pull-left {"
    "    float: left;"
    "}"
    ".container {"
    "    width: 100%;"
    "    max-width: 680px; /* 800 */"
    "    text-align: center;"
    "    margin: 0 auto;"
    "}"
    ""
    ".boxu {"
    "    font-size: 1.25rem; /* 20 */"
    "    background-color: #474747;"
    "    position: relative;"
    "    outline: 2px dashed #737373;"
    "    outline-offset: -10px;"
    "    padding: 20px 20px;"
    "}"
    ""
    ".box {"
    "    font-size: 1.25rem; /* 20 */"
    "    background-color: #474747;"
    "    position: relative;"
    "    padding: 100px 20px;"
    "}"
    ""
    "    .box.has-advanced-upload {"
    "        outline: 2px dashed #737373;"
    "        outline-offset: -10px;"
    "    }"
    ""
    "    .box.is-dragover {"
    "        outline-color: #3D3D3D;"
    "        background-color: #737373;"
    "    }"
    ""
    ".box__dragndrop,"
    ".box__icon {"
    "    display: none;"
    "}"
    ""
    ""
    ".box.has-advanced-upload .box__icon {"
    "    width: 100%;"
    "    height: 80px;"
    "    fill: #737373;"
    "    display: block;"
    "    margin-bottom: 40px;"
    "}"
    ""
    ".box.is-uploading .box__input,"
    ".box.is-success .box__input,"
    ".box.is-error .box__input {"
    "    visibility: hidden;"
    "}"
    ""
    ".box__uploading,"
    ".box__success,"
    ".box__error {"
    "    display: none;"
    "}"
    ""
    ".box.is-uploading .box__uploading,"
    ".box.is-success .box__success,"
    ".box.is-error .box__error {"
    "    display: block;"
    "    position: absolute;"
    "    top: 50%;"
    "    right: 0;"
    "    left: 0;"
    "    transform: translateY( -50% );"
    "}"
    ""
    ".box__uploading {"
    "    font-style: italic;"
    "}"
    ""
    ".box__success {"
    "    -webkit-animation: appear-from-inside .25s ease-in-out;"
    "    animation: appear-from-inside .25s ease-in-out;"
    "}"
    ""
    ""
    ".box__restart {"
    "    font-weight: 700;"
    "}"
    ""
    ".js .box__file {"
    "    width: 0.1px;"
    "    height: 0.1px;"
    "    opacity: 0;"
    "    overflow: hidden;"
    "    position: absolute;"
    "    z-index: -1;"
    "}"
    ""
    "    .js .box__file + label {"
    "        max-width: 80%;"
    "        text-overflow: ellipsis;"
    "        white-space: nowrap;"
    "        cursor: pointer;"
    "        display: inline-block;"
    "        overflow: hidden;"
    "    }"
    ""
    "        .js .box__file + label:hover strong,"
    "        .box__file:focus + label strong,"
    "        .box__file.has-focus + label strong {"
    "            color: #797979;"
    "        }"
    ""
    "    .js .box__file:focus + label,"
    "    .js .box__file.has-focus + label {"
    "        outline: 1px dotted #000;"
    "        outline: -webkit-focus-ring-color auto 5px;"
    "    }"
    ""
    ""
    ".no-js .box__file + label {"
    "    display: none;"
    "}"
    ""
    ".no-js .box__button {"
    "    display: block;"
    "}"
    ""
    ".box__button {"
    "    font-weight: 700;"
    "    color: #e5edf1;"
    "    background-color: #39bfd3;"
    "    display: none;"
    "    padding: 8px 16px;"
    "    margin: 40px auto 0;"
    "}"
    ""
    "    .box__button:hover,"
    "    .box__button:focus {"
    "        background-color: #0f3c4b;"
    "    }");
}

/**************************************************************************
   IP Address to String
**************************************************************************/
String ipToString(IPAddress ip)
{
  String s = "";
  for (int i = 0; i < 4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}

/**************************************************************************
   Send command to local roku
**************************************************************************/
int rokuCommand(String ip, String data)
{
  HTTPClient http;
  String url = "http://" + ip + ":8060/" + data;
  http.begin(url);
  DEBUG_PRINT(url);
  DEBUG_PRINT("Sending roku command");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, data.c_str(), 40);
  last_send.bits = 1;
  strncpy(last_send.encoding, "roku", 20);
  strncpy(last_send.address, ip.c_str(), 40);
  strncpy(last_recv.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);
  last_send.valid = true;

  return http.POST("");
  http.end();
}

/**************************************************************************
   Split string by character
**************************************************************************/
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

/**************************************************************************
   Get encoding type as string
**************************************************************************/
String encoding(decode_results *results)
{
  String output;
  switch (results->decode_type) {
    default:
    case UNKNOWN:      output = "UNKNOWN";            break;
    case NEC:          output = "NEC";                break;
    case SONY:         output = "SONY";               break;
    case RC5:          output = "RC5";                break;
    case RC6:          output = "RC6";                break;
    case DISH:         output = "DISH";               break;
    case SHARP:        output = "SHARP";              break;
    case JVC:          output = "JVC";                break;
    case SANYO:        output = "SANYO";              break;
    case SANYO_LC7461: output = "SANYO_LC7461";       break;
    case MITSUBISHI:   output = "MITSUBISHI";         break;
    case SAMSUNG:      output = "SAMSUNG";            break;
    case LG:           output = "LG";                 break;
    case WHYNTER:      output = "WHYNTER";            break;
    case AIWA_RC_T501: output = "AIWA_RC_T501";       break;
    case PANASONIC:    output = "PANASONIC";          break;
    case DENON:        output = "DENON";              break;
    case COOLIX:       output = "COOLIX";             break;
  }
  return output;
}

/**************************************************************************
   Convert Uint64 value to string
**************************************************************************/
String Uint64toString(uint64_t input, uint8_t base)
{
  char buf[8 * sizeof(input) + 1];  // Assumes 8-bit chars plus zero byte.
  char *str = &buf[sizeof(buf) - 1];

  *str = '\0';

  // prevent crash if called with base == 1
  if (base < 2) base = 10;

  do {
    char c = input % base;
    input /= base;

    *--str = c < 10 ? c + '0' : c + 'A' - 10;
  } while (input);

  std::string s(str);
  return s.c_str();
}

/**************************************************************************
   Write code to serial interface
**************************************************************************/
void fullCode (decode_results *results)
{
  Serial.print("One line: ");
  serialPrintUint64(results->value, 16);
  Serial.print(":");
  Serial.print(encoding(results));
  Serial.print(":");
  Serial.print(results->bits, DEC);
  if (results->overflow)
    Serial.println("WARNING: IR code too long."
                   "Edit IRrecv.h and increase RAWBUF");
  Serial.println("");
}

/**************************************************************************
   Send HTML header
**************************************************************************/
void sendHeader()
{
  sendHeader(200);
}
void sendHeader(int httpcode)
{
  buildJavascript();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(httpcode, "text/html; charset=utf-8", "");
  server.sendContent("<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n");
  server.sendContent("<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en'>\n");
  server.sendContent("  <head>\n");
  server.sendContent("    <meta name='viewport' content='width=device-width, initial-scale=.75' />\n");
  server.sendContent("    <link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css' />\n");
  server.sendContent("    <style>@media (max-width: 991px) {.nav-pills>li {float: none; margin-left: 0; margin-top: 5px; text-align: center;}}</style>\n");
  server.sendContent("    <title>" + FIRMWARE_NAME + " - " + VERSION + "</title>\n");
  server.sendContent("  </head>\n");
  server.sendContent(javaScript);
  server.sendContent("  <body>\n");
  server.sendContent("    <div class='container'>\n");
  server.sendContent("      <h1><a href='https://forum.fhem.de/index.php/topic,72950.0.html'>" + FIRMWARE_NAME + " - " + VERSION + "</a></h1>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <ul class='nav nav-pills'>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='http://" + String(host_name) + ".local" + ":" + String(port) + "'>Hostname <span class='badge'>" + String(host_name) + ".local" + ":" + String(port) + "</span></a></li>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "'>Local <span class='badge'>" + ipToString(WiFi.localIP()) + ":" + String(port) + "</span></a></li>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='#'>MAC <span class='badge'>" + String(WiFi.macAddress()) + "</span></a></li>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='/config'>Config</a></li>\n");
  server.sendContent("          </ul>\n");
  server.sendContent("        </div>\n");
  server.sendContent("      </div><hr />\n");
}

void buildHeader()
{
  
  htmlHeader="<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n";
  htmlHeader+="<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en'>\n";
  htmlHeader+="  <head>\n";
  htmlHeader+="    <meta name='viewport' content='width=device-width, initial-scale=.75' />\n";
  htmlHeader+="    <link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css' />\n";
  htmlHeader+="    <style>@media (max-width: 991px) {.nav-pills>li {float: none; margin-left: 0; margin-top: 5px; text-align: center;}}</style>\n";
  htmlHeader+="    <title>" + FIRMWARE_NAME + " - " + VERSION + "</title>\n";
  htmlHeader+="  </head>\n";
  htmlHeader+="  <body>\n";
  htmlHeader+="    <div class='container'>\n";
  htmlHeader+="      <h1><a href='https://forum.fhem.de/index.php/topic,72950.0.html'>" + FIRMWARE_NAME + " - " + VERSION + "</a></h1>\n";
  htmlHeader+="      <div class='row'>\n";
  htmlHeader+="        <div class='col-md-12'>\n";
  htmlHeader+="          <ul class='nav nav-pills'>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='http://" + String(host_name) + ".local" + ":" + String(port) + "'>Hostname <span class='badge'>" + String(host_name) + ".local" + ":" + String(port) + "</span></a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "'>Local <span class='badge'>" + ipToString(WiFi.localIP()) + ":" + String(port) + "</span></a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='#'>MAC <span class='badge'>" + String(WiFi.macAddress()) + "</span></a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='/config'>Config</a></li>\n";
  htmlHeader+="          </ul>\n";
  htmlHeader+="        </div>\n";
  htmlHeader+="      </div><hr />\n";

}

/**************************************************************************
   Send HTML footer
**************************************************************************/
void sendFooter()
{
  server.sendContent("      <div class='row'><div class='col-md-12'><em>" + String(millis()/1000) + "s uptime since " + String(boottime) + "</em></div></div>\n");
  server.sendContent("    </div>\n");
  server.sendContent("  </body>\n");
  server.sendContent("</html>\n");
  server.client().stop();
}

/**************************************************************************
   Build HTML footer
**************************************************************************/
void buildFooter()
{
  htmlFooter="      <div class='row'><div class='col-md-12'><em>" + String(millis()/1000) + "s uptime since " + String(boottime) + "</em></div></div>\n";
  htmlFooter+="    </div>\n";
  htmlFooter+="  </body>\n";
  htmlFooter+="</html>\n";
}

/**************************************************************************
   Send HTML Config page
**************************************************************************/
void sendConfigPage()
{
  sendConfigPage("", "");
}
void sendConfigPage(String message, String header)
{
  sendConfigPage(message, header, 0);
}
void sendConfigPage(String message, String header, int type)
{
  sendConfigPage(message, header, type, 200);
}
void sendConfigPage(String message, String header, int type, int httpcode)
{
char passcode_conf[40] = "";
char host_name_conf[40] = "";
char port_str_conf[20] = "";

if (SPIFFS.begin()) 
  {
    DEBUG_PRINT("mounted file system");
    if (SPIFFS.exists("/config.json")) 
    {
      //file exists, reading and loading
      DEBUG_PRINT("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_PRINT("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          DEBUG_PRINT("\nparsed json");

          if (json.containsKey("hostname")) strncpy(host_name_conf, json["hostname"], 40);
          if (json.containsKey("passcode")) strncpy(passcode_conf, json["passcode"], 40);
          if (json.containsKey("port_str")) {
            strncpy(port_str_conf, json["port_str"], 20);
          }
        } else {
          DEBUG_PRINT("failed to load json config");
        }
      }
    }
  } 
  else 
  {
    DEBUG_PRINT("failed to mount FS");
  }


  sendHeader(httpcode);
  if (type == 1)
    server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-success'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 2)
    server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-warning'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 3)
    server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-danger'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h3>Config</h3>\n");
  server.sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server.sendContent("            <thead><tr><th>Option</th><th>Current Value</th><th>New Value</th></tr></thead>\n"); //Title
  server.sendContent("            <tbody>\n");
  server.sendContent("            <tr class='text-uppercase'><td>NTP enabled?</td><td><code>" + String(getTime) + "</code></td><td><input type='checkbox' id='ntpok' name='getTime' value='" + String(getTime) + "'></td></tr>\n");
  server.sendContent("            <tr class='text-uppercase'><td>NTP Server</td><td><code>" + String(poolServerName) + "</code></td><td><input type='text' id='ntpserver' name='ntpserver' value='" + String(poolServerName) + "'></td></tr>\n");
  server.sendContent("            <tr class='text-uppercase'><td>Hostname</td><td><code>" + String(host_name_conf) + "</code></td><td><input type='text' id='host_name_conf' name='host_name_conf' value='" + String(host_name_conf) + "'></td></tr>\n");
  server.sendContent("            <tr class='text-uppercase'><td>Passcode</td><td><code>" + String(passcode_conf) + "</code></td><td><input type='text' id='passcode_conf' name='passcode_conf' value='" + String(passcode_conf) + "'></td></tr>\n");
  server.sendContent("            <tr class='text-uppercase'><td>Server Port</td><td><code>" + String(port_str_conf) + "</code></td><td><input type='text' id='port_str_conf' name='port_str_conf' value='" + String(port_str_conf) + "'></td></tr>\n");


  /*if (last_send.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send.timestamp) + "</td><td><code>" + String(last_send.data) + "</code></td><td><code>" + String(last_send.encoding) + "</code></td><td><code>" + String(last_send.bits) + "</code></td><td><code>" + String(last_send.address) + "</code></td></tr>\n");
  if (last_send_2.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_2.timestamp) + "</td><td><code>" + String(last_send_2.data) + "</code></td><td><code>" + String(last_send_2.encoding) + "</code></td><td><code>" + String(last_send_2.bits) + "</code></td><td><code>" + String(last_send_2.address) + "</code></td></tr>\n");
  if (last_send_3.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_3.timestamp) + "</td><td><code>" + String(last_send_3.data) + "</code></td><td><code>" + String(last_send_3.encoding) + "</code></td><td><code>" + String(last_send_3.bits) + "</code></td><td><code>" + String(last_send_3.address) + "</code></td></tr>\n");
  if (last_send_4.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_4.timestamp) + "</td><td><code>" + String(last_send_4.data) + "</code></td><td><code>" + String(last_send_4.encoding) + "</code></td><td><code>" + String(last_send_4.bits) + "</code></td><td><code>" + String(last_send_4.address) + "</code></td></tr>\n");
  if (last_send_5.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_5.timestamp) + "</td><td><code>" + String(last_send_5.data) + "</code></td><td><code>" + String(last_send_5.encoding) + "</code></td><td><code>" + String(last_send_5.bits) + "</code></td><td><code>" + String(last_send_5.address) + "</code></td></tr>\n");
  if (!last_send.valid && !last_send_2.valid && !last_send_3.valid && !last_send_4.valid && !last_send_5.valid)
    server.sendContent("              <tr><td colspan='5' class='text-center'><em>No codes sent</em></td></tr>");
  server.sendContent("            </tbody></table>\n");
  server.sendContent("          </div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h3>Codes Received</h3>\n");
  server.sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server.sendContent("            <thead><tr><th>Details</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>\n"); //Title
  server.sendContent("            <tbody>\n");
  if (last_recv.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=1'>" + String(last_recv.timestamp) + "</a></td><td><code>" + String(last_recv.data) + "</code></td><td><code>" + String(last_recv.encoding) + "</code></td><td><code>" + String(last_recv.bits) + "</code></td><td><code>" + String(last_recv.address) + "</code></td></tr>\n");
  if (last_recv_2.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=2'>" + String(last_recv_2.timestamp) + "</a></td><td><code>" + String(last_recv_2.data) + "</code></td><td><code>" + String(last_recv_2.encoding) + "</code></td><td><code>" + String(last_recv_2.bits) + "</code></td><td><code>" + String(last_recv_2.address) + "</code></td></tr>\n");
  if (last_recv_3.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=3'>" + String(last_recv_3.timestamp) + "</a></td><td><code>" + String(last_recv_3.data) + "</code></td><td><code>" + String(last_recv_3.encoding) + "</code></td><td><code>" + String(last_recv_3.bits) + "</code></td><td><code>" + String(last_recv_3.address) + "</code></td></tr>\n");
  if (last_recv_4.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=4'>" + String(last_recv_4.timestamp) + "</a></td><td><code>" + String(last_recv_4.data) + "</code></td><td><code>" + String(last_recv_4.encoding) + "</code></td><td><code>" + String(last_recv_4.bits) + "</code></td><td><code>" + String(last_recv_4.address) + "</code></td></tr>\n");
  if (last_recv_5.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=5'>" + String(last_recv_5.timestamp) + "</a></td><td><code>" + String(last_recv_5.data) + "</code></td><td><code>" + String(last_recv_5.encoding) + "</code></td><td><code>" + String(last_recv_5.bits) + "</code></td><td><code>" + String(last_recv_5.address) + "</code></td></tr>\n");
  if (!last_recv.valid && !last_recv_2.valid && !last_recv_3.valid && !last_recv_4.valid && !last_recv_5.valid)*/


  server.sendContent(" <tr><td colspan='5' class='text-center'><em><button type='submit' class='btn btn-sm btn-info'>Save</button>  <a href='/' class='btn btn-sm btn-warning'>Cancel</a>  <a href='/reboot' class='btn btn-sm btn-danger'>Reboot</a></em></td></tr>");
  server.sendContent("            </tbody></table>\n");
  server.sendContent("          </div></div>\n");
  sendFooter();
}


/**************************************************************************
   Send HTML main page
**************************************************************************/
void sendHomePage()
{
  sendHomePage("", "");
}
void sendHomePage(String message, String header)
{
  sendHomePage(message, header, 0);
}
void sendHomePage(String message, String header, int type)
{
  sendHomePage(message, header, type, 200);
}
void sendHomePage(String message, String header, int type, int httpcode)
{
  sendHeader(httpcode);
  if (type == 1)
    server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-success'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 2)
    server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-warning'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 3)
    server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-danger'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h3>Codes Transmitted</h3>\n");
  server.sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server.sendContent("            <thead><tr><th>Sent</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>\n"); //Title
  server.sendContent("            <tbody>\n");
  if (last_send.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send.timestamp) + "</td><td><code>" + String(last_send.data) + "</code></td><td><code>" + String(last_send.encoding) + "</code></td><td><code>" + String(last_send.bits) + "</code></td><td><code>" + String(last_send.address) + "</code></td></tr>\n");
  if (last_send_2.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_2.timestamp) + "</td><td><code>" + String(last_send_2.data) + "</code></td><td><code>" + String(last_send_2.encoding) + "</code></td><td><code>" + String(last_send_2.bits) + "</code></td><td><code>" + String(last_send_2.address) + "</code></td></tr>\n");
  if (last_send_3.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_3.timestamp) + "</td><td><code>" + String(last_send_3.data) + "</code></td><td><code>" + String(last_send_3.encoding) + "</code></td><td><code>" + String(last_send_3.bits) + "</code></td><td><code>" + String(last_send_3.address) + "</code></td></tr>\n");
  if (last_send_4.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_4.timestamp) + "</td><td><code>" + String(last_send_4.data) + "</code></td><td><code>" + String(last_send_4.encoding) + "</code></td><td><code>" + String(last_send_4.bits) + "</code></td><td><code>" + String(last_send_4.address) + "</code></td></tr>\n");
  if (last_send_5.valid)
    server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_5.timestamp) + "</td><td><code>" + String(last_send_5.data) + "</code></td><td><code>" + String(last_send_5.encoding) + "</code></td><td><code>" + String(last_send_5.bits) + "</code></td><td><code>" + String(last_send_5.address) + "</code></td></tr>\n");
  if (!last_send.valid && !last_send_2.valid && !last_send_3.valid && !last_send_4.valid && !last_send_5.valid)
    server.sendContent("              <tr><td colspan='5' class='text-center'><em>No codes sent</em></td></tr>");
  server.sendContent("            </tbody></table>\n");
  server.sendContent("          </div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h3>Codes Received</h3>\n");
  server.sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server.sendContent("            <thead><tr><th>Details</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>\n"); //Title
  server.sendContent("            <tbody>\n");
  if (last_recv.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=1'>" + String(last_recv.timestamp) + "</a></td><td><code>" + String(last_recv.data) + "</code></td><td><code>" + String(last_recv.encoding) + "</code></td><td><code>" + String(last_recv.bits) + "</code></td><td><code>" + String(last_recv.address) + "</code></td></tr>\n");
  if (last_recv_2.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=2'>" + String(last_recv_2.timestamp) + "</a></td><td><code>" + String(last_recv_2.data) + "</code></td><td><code>" + String(last_recv_2.encoding) + "</code></td><td><code>" + String(last_recv_2.bits) + "</code></td><td><code>" + String(last_recv_2.address) + "</code></td></tr>\n");
  if (last_recv_3.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=3'>" + String(last_recv_3.timestamp) + "</a></td><td><code>" + String(last_recv_3.data) + "</code></td><td><code>" + String(last_recv_3.encoding) + "</code></td><td><code>" + String(last_recv_3.bits) + "</code></td><td><code>" + String(last_recv_3.address) + "</code></td></tr>\n");
  if (last_recv_4.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=4'>" + String(last_recv_4.timestamp) + "</a></td><td><code>" + String(last_recv_4.data) + "</code></td><td><code>" + String(last_recv_4.encoding) + "</code></td><td><code>" + String(last_recv_4.bits) + "</code></td><td><code>" + String(last_recv_4.address) + "</code></td></tr>\n");
  if (last_recv_5.valid)
    server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=5'>" + String(last_recv_5.timestamp) + "</a></td><td><code>" + String(last_recv_5.data) + "</code></td><td><code>" + String(last_recv_5.encoding) + "</code></td><td><code>" + String(last_recv_5.bits) + "</code></td><td><code>" + String(last_recv_5.address) + "</code></td></tr>\n");
  if (!last_recv.valid && !last_recv_2.valid && !last_recv_3.valid && !last_recv_4.valid && !last_recv_5.valid)
    server.sendContent("              <tr><td colspan='5' class='text-center'><em>No codes received</em></td></tr>");
  server.sendContent("            </tbody></table>\n");
  server.sendContent("          </div></div>\n");
  sendFooter();
}

/**************************************************************************
   Send HTML code page
**************************************************************************/
void sendCodePage(Code& selCode)
{
  sendCodePage(selCode, 200);
}
void sendCodePage(Code& selCode, int httpcode)
{
  String htmlData+=;
  buildHeader();  // httpcode later was parameter htmlHeader
  buildJavascript();  //                          javaScript
  buildFooter();      //                          htmlFooter

  htmlData=htmlHeader;

  htmlData+="      <div class='row'>\n";
  htmlData+="        <div class='col-md-12'>\n";
  htmlData+="          <h2><span class='label label-success'>" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</span></h2><br/>\n";
  htmlData+="          <dl class='dl-horizontal'>\n";
  htmlData+="            <dt>Data</dt>\n";
  htmlData+="            <dd><code>" + String(selCode.data)  + "</code></dd></dl>\n";
  htmlData+="          <dl class='dl-horizontal'>\n";
  htmlData+="            <dt>Type</dt>\n";
  htmlData+="            <dd><code>" + String(selCode.encoding)  + "</code></dd></dl>\n";
  htmlData+="          <dl class='dl-horizontal'>\n";
  htmlData+="            <dt>Length</dt>\n";
  htmlData+="            <dd><code>" + String(selCode.bits)  + "</code></dd></dl>\n";
  htmlData+="          <dl class='dl-horizontal'>\n";
  htmlData+="            <dt>Address</dt>\n";
  htmlData+="            <dd><code>" + String(selCode.address)  + "</code></dd></dl>\n";
  htmlData+="          <dl class='dl-horizontal'>\n";
  htmlData+="            <dt>Raw</dt>\n";
  htmlData+="            <dd><code>" + String(selCode.raw)  + "</code></dd></dl>\n";
  htmlData+="          <dl class='dl-horizontal'>\n";
  htmlData+="            <dt>Rawgraph</dt>\n";
  htmlData+="         <dd><canvas id='myCanvas' width='100' height='100' style='border:1px solid #d3d3d3;>Your browser does not support the canvas element.</canvas></dd></dl>\n";
  htmlData+="        </div></div>\n";
  htmlData+="      <div class='row'>\n";
  htmlData+="        <div class='col-md-12'>\n";
  htmlData+="          <div class='alert alert-warning'>Don't forget to add your passcode to the URLs below if you set one</div>\n";
  htmlData+="            <input id='data' type='text' name='data' hidden value='" + String(selCode.raw) + "'>\n";
  htmlData+="      </div></div>\n";

  if (String(selCode.encoding) == "UNKNOWN")
  {
    htmlData+="      <div class='row'>\n";
    htmlData+="        <div class='col-md-12'>\n";
    htmlData+="          <ul class='list-unstyled'>\n";
    htmlData+="            <li>Hostname <span class='label label-default'>JSON</span></li>\n";
    htmlData+="            <li><pre><a href='http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{\"data\":[" + String(selCode.raw) + "], \"type\":\"raw\", \"khz\":38}]'>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{\"data\":[" + String(selCode.raw) + "], \"type\":\"raw\", \"khz\":38}]</a></pre></li>\n";
    htmlData+="            <li>Local IP <span class='label label-default'>JSON</span></li>\n";
    htmlData+="            <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{\"data\":[" + String(selCode.raw) + "], \"type\":\"raw\", \"khz\":38}]</pre></li>\n";
    htmlData+="          </ul>\n";
  }
  else
  {
    htmlData+="      <div class='row'>\n";
    htmlData+="        <div class='col-md-12'>\n";
    htmlData+="          <ul class='list-unstyled'>\n";
    htmlData+="            <li>Hostname <span class='label label-default'>JSON</span></li>\n";
    htmlData+="            <li><pre><a href='http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{\"data\":\"" + String(selCode.data) + "\", \"type\":\"" + String(selCode.encoding) + "\", \"length\":" + String(selCode.bits) + "}]'> http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{\"data\":\"" + String(selCode.data) + "\", \"type\":\"" + String(selCode.encoding) + "\", \"length\":" + String(selCode.bits) + "}]</a></pre></li>\n";
    htmlData+="            <li>Local IP <span class='label label-default'>JSON</span></li>\n";
    htmlData+="            <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{\"data\":\"" + String(selCode.data) + "\", \"type\":\"" + String(selCode.encoding) + "\", \"length\":" + String(selCode.bits) + "}]</pre></li>\n";
    htmlData+="          </ul>\n";
  }

  htmlData+="        </div>\n";
  htmlData+="     </div>\n";
  htmlData+=javaScript;
  htmlData+=htmlFooter;
  
  //server.setContentLength(CONTENT_LENGTH_UNKNOWN);   //timeout 2sec before javascritp start!
  server.send(httpcode, "text/html; charset=utf-8", htmlData);
  server.client().stop();
}

void buildJavascript(){
  javaScript="<script>   window.onload=showdata(); function showdata(data){ var data = document.getElementById('data').value.split(',').map(Number); var downscaleFactor= 0.01; var linebegin = 5; var lineend = 10; var highpos = 10;";
  javaScript+="var lowpos = 90; var i = 0; var linespacing = 20; var lastpos = 0; var last = 5/downscaleFactor; var dlen = data.length; ";
  javaScript+="var canvas = document.getElementById('myCanvas'); var ctx = canvas.getContext('2d'); for (i = 0;i < dlen;i++){ "; 
  javaScript+="last += data[i]; }; canvas.width=((last*downscaleFactor)+(linebegin+lineend)); ctx.scale( downscaleFactor, 1 ); last = linebegin/downscaleFactor; ctx.moveTo(0,lowpos);";
  javaScript+="ctx.lineTo(last,lowpos); ctx.stroke(); ctx.moveTo(last,lowpos); ctx.lineTo(last,highpos); ctx.stroke(); ";
  javaScript+="for (i = 0;i < dlen;i++){ if (i % 2 === 0){ ctx.moveTo(last,highpos); last += (data[i]); ctx.lineTo(last,highpos); ";
  javaScript+="ctx.stroke(); ctx.moveTo(last,highpos); ctx.lineTo(last,lowpos); lastpos=lowpos; ctx.stroke(); } else { ctx.moveTo(last,lowpos); last += (data[i]); ";
  javaScript+=" ctx.lineTo(last,lowpos); ctx.stroke(); ctx.moveTo(last,lowpos); ctx.lineTo(last,highpos); lastpos=highpos; ctx.stroke(); } }; ";
  javaScript+="ctx.moveTo(last,lastpos); ctx.lineTo((last+(lineend/downscaleFactor)),lastpos); ctx.stroke(); ctx.globalAlpha = 0.2; ctx.fillStyle = 'gray'; ";
  javaScript+="for (i=linebegin;i < canvas.width;i=i+(linespacing*2)){ ctx.fillRect(i/downscaleFactor,0,linespacing/downscaleFactor,canvas.height); ctx.stroke(); } } </script>";

}

/**************************************************************************
   Handle code to JSON object
**************************************************************************/
void codeJson(JsonObject &codeData, decode_results *results)
{
  if (results->value)  {  
      codeData["data"] = Uint64toString(results->value, 16);
      codeData["encoding"] = encoding(results);
      codeData["bits"] = results->bits;
      String r = "";
      for (uint16_t i = 1; i < results->rawlen; i++) {
        r += results->rawbuf[i] * RAWTICK;
        if (i < results->rawlen - 1)
          r += ",";                           // ',' not needed on last one
        if (!(i & 1)) r += " ";
      }
      codeData["uint16_t"] = r;
      if (results->decode_type != UNKNOWN) {
        codeData["address"] = "0x" + String(results->address, HEX);
        codeData["command"] = "0x" + String(results->command, HEX);
      } else {
        codeData["address"] = "0x";
        codeData["command"] = "0x";
      }
  }
}

//
// new convert 
//
void copyCode (Code& c1, Code& c2) {
  strncpy(c2.data, c1.data, 40);
  strncpy(c2.encoding, c1.encoding, 20);
  strncpy(c2.timestamp, c1.timestamp, 40);
  strncpy(c2.address, c1.address, 40);
  strncpy(c2.command, c1.command, 40);
  c2.bits = c1.bits;
  c2.raw = c1.raw;
  c2.valid = c1.valid;
}

//
// new convert 
//
void cvrtCode(Code& codeData, decode_results *results)
{
  strncpy(codeData.data, Uint64toString(results->value, 16).c_str(), 40);
  strncpy(codeData.encoding, encoding(results).c_str(), 20);
  codeData.bits = results->bits;
  String r = "";
      for (uint16_t i = 1; i < results->rawlen; i++) {
      r += results->rawbuf[i] * RAWTICK;
      if (i < results->rawlen - 1)
        r += ",";                           // ',' not needed on last one
      //if (!(i & 1)) r += " ";
    }
  codeData.raw = r;
  if (results->decode_type != UNKNOWN) {
    strncpy(codeData.address, ("0x" + String(results->address, HEX)).c_str(), 40);
    strncpy(codeData.command, ("0x" + String(results->command, HEX)).c_str(), 40);
  } else {
    strncpy(codeData.address, "0x0", 40);
    strncpy(codeData.command, "0x0", 40);
  }
}



/**************************************************************************
   Dump out the decode results structure
**************************************************************************/
void dumpInfo(decode_results *results)
{
  if (results->overflow)
    Serial.println("WARNING: IR code too long."
                   "Edit IRrecv.h and increase RAWBUF");

  // Show Encoding standard
  Serial.print("Encoding  : ");
  Serial.print(encoding(results));
  Serial.println("");

  // Show Code & length
  Serial.print("Code      : ");
  serialPrintUint64(results->value, 16);
  Serial.print(" (");
  Serial.print(results->bits, DEC);
  Serial.println(" bits)");
}
void dumpRaw(decode_results *results)
{
  // Print Raw data
  Serial.print("Timing[");
  Serial.print(results->rawlen - 1, DEC);
  Serial.println("]: ");

  for (uint16_t i = 1;  i < results->rawlen;  i++) {
    if (i % 100 == 0)
      yield();  // Preemptive yield every 100th entry to feed the WDT.
    uint32_t x = results->rawbuf[i] * RAWTICK;
    if (!(i & 1)) {  // even
      Serial.print("-");
      if (x < 1000) Serial.print(" ");
      if (x < 100) Serial.print(" ");
      Serial.print(x, DEC);
    } else {  // odd
      Serial.print("     ");
      Serial.print("+");
      if (x < 1000) Serial.print(" ");
      if (x < 100) Serial.print(" ");
      Serial.print(x, DEC);
      if (i < results->rawlen - 1)
        Serial.print(", ");  // ',' not needed for last one
    }
    if (!(i % 8)) Serial.println("");
  }
  Serial.println("");  // Newline
}
void dumpCode(decode_results *results)
{
  // Start declaration
  Serial.print("uint16_t  ");              // variable type
  Serial.print("rawData[");                // array name
  Serial.print(results->rawlen - 1, DEC);  // array size
  Serial.print("] = {");                   // Start declaration

  // Dump data
  for (uint16_t i = 1; i < results->rawlen; i++) {
    Serial.print(results->rawbuf[i] * RAWTICK, DEC);
    if (i < results->rawlen - 1)
      Serial.print(",");  // ',' not needed on last one
    if (!(i & 1)) Serial.print(" ");
  }

  // End declaration
  Serial.print("};");  //

  // Comment
  Serial.print("  // ");
  Serial.print(encoding(results));
  Serial.print(" ");
  serialPrintUint64(results->value, 16);

  // Newline
  Serial.println("");

  // Now dump "known" codes
  if (results->decode_type != UNKNOWN) {
    // Some protocols have an address &/or command.
    // NOTE: It will ignore the atypical case when a message has been decoded
    // but the address & the command are both 0.
    if (results->address > 0 || results->command > 0) {
      Serial.print("uint32_t  address = 0x");
      Serial.print(results->address, HEX);
      Serial.println(";");
      Serial.print("uint32_t  command = 0x");
      Serial.print(results->command, HEX);
      Serial.println(";");
    }

    // All protocols have data
    Serial.print("uint64_t  data = 0x");
    serialPrintUint64(results->value, 16);
    Serial.println(";");
  }
}

/**************************************************************************
   Convert string to hex
**************************************************************************/
unsigned long HexToLongInt(String h)
{
  // this function replace the strtol as this function is not able to handle hex numbers greather than 7fffffff
  // I'll take char by char converting from hex to char then shifting 4 bits at the time
  int i;
  unsigned long tmp = 0;
  unsigned char c;
  int s = 0;
  h.toUpperCase();
  for (i = h.length() - 1; i >= 0 ; i--)
  {
    // take the char starting from the right
    c = h[i];
    // convert from hex to int
    c = c - '0';
    if (c > 9)
      c = c - 7;
    // add and shift of 4 bits per each char
    tmp += c << s;
    s += 4;
  }
  return tmp;
}

/**************************************************************************
   Send IR code
**************************************************************************/
void irblast(String type, String dataStr, unsigned int len, int rdelay, int pulse, int pdelay, int repeat, long address)
{
  DEBUG_PRINT("Blasting off");
  type.toLowerCase();
  unsigned long data = HexToLongInt(dataStr);
  // Repeat Loop
  for (int r = 0; r < repeat; r++)
  {
    // Pulse Loop
    for (int p = 0; p < pulse; p++)
    {
      Serial.print(data, HEX);
      Serial.print(":");
      Serial.print(type);
      Serial.print(":");
      Serial.println(len);
      if (type == "nec") {
        irsend.sendNEC(data, len);
      } else if (type == "sony") {
        irsend.sendSony(data, len);
      } else if (type == "coolix") {
        irsend.sendCOOLIX(data, len);
      } else if (type == "whynter") {
        irsend.sendWhynter(data, len);
      } else if (type == "panasonic") {
        Serial.println(address);
        irsend.sendPanasonic(address, data);
      } else if (type == "jvc") {
        irsend.sendJVC(data, len, 0);
      } else if (type == "samsung") {
        irsend.sendSAMSUNG(data, len);
      } else if (type == "sharp") {
        irsend.sendSharpRaw(data, len);
      } else if (type == "dish") {
        irsend.sendDISH(data, len);
      } else if (type == "rc5") {
        irsend.sendRC5(data, len);
      } else if (type == "rc6") {
        irsend.sendRC6(data, len);
      } else if (type == "roomba") {
        roomba_send(atoi(dataStr.c_str()), pulse, pdelay);
      }

      // don't add a delay for the last pulse
      if (p + 1 < pulse)
      {
        delay(pdelay);
      }
    }

    // don't add a delay for the last repeat
    if (r + 1 < repeat)
    {
      delay(rdelay);
    }
  }

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, dataStr.c_str(), 40);
  last_send.bits = len;
  strncpy(last_send.encoding, type.c_str(), 20);
  strncpy(last_send.address, ("0x" + String(address, HEX)).c_str(), 20);
  strncpy(last_send.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);
  last_send.valid = true;


}
void rawblast(JsonArray &raw, int khz, int rdelay, int pulse, int pdelay, int repeat)
{
  DEBUG_PRINT("Raw transmit");

  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      DEBUG_PRINT("Sending code");
      irsend.enableIROut(khz);
      int first_temp = raw[0];
      int first = abs(first_temp);
    
      for (unsigned int i = 0; i < raw.size(); i++) {
        int val_temp = raw[i];
        unsigned int val = abs(val_temp);
         
        if (i & 1) irsend.space(val);
        else       irsend.mark(val);
      }
      irsend.space(0);

      // don't add a delay for the last pulse
      if (p + 1 < pulse)
      {
        delay(pdelay);
      }
    }
    // don't add a delay for the last repeat
    if (r + 1 < repeat)
    {
      delay(rdelay);
    }
  }
  Serial.println("Transmission complete");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, "", 40);
  last_send.bits = raw.size();
  strncpy(last_send.encoding, "RAW", 20);
  strncpy(last_send.address, "0x0", 40);
  strncpy(last_send.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);
  last_send.valid = true;

}
void roomba_send(int code, int pulse, int pdelay)
{
  DEBUG_PRINT("Sending Roomba code");
  DEBUG_PRINT(code);

  int length = 8;
  uint16_t raw[length * 2];
  unsigned int one_pulse = 3000;
  unsigned int one_break = 1000;
  unsigned int zero_pulse = one_break;
  unsigned int zero_break = one_pulse;
  uint16_t len = 15;
  uint16_t hz = 38;

  int arrayposition = 0;
  for (int counter = length - 1; counter >= 0; --counter) {
    if (code & (1 << counter)) {
      raw[arrayposition] = one_pulse;
      raw[arrayposition + 1] = one_break;
    }
    else {
      raw[arrayposition] = zero_pulse;
      raw[arrayposition + 1] = zero_break;
    }
    arrayposition = arrayposition + 2;
  }
  for (int i = 0; i < pulse; i++) {
    irsend.sendRaw(raw, len, hz);
    delay(pdelay);
  }
}

/**************************************************************************
   Support for key-value-protocol of FHEM
**************************************************************************/
String GetChipID()
{
  return String(ESP.getChipId());
}

void sendKVPCodeString()
{
  if (last_recv.valid) sendCodeReceivedString(last_recv, 1);
  if (last_recv_2.valid) sendCodeReceivedString(last_recv_2, 2);
  if (last_recv_3.valid) sendCodeReceivedString(last_recv_3, 3);
  if (last_recv_4.valid) sendCodeReceivedString(last_recv_4, 4);
  if (last_recv_5.valid) sendCodeReceivedString(last_recv_5, 5);
}

void sendCodeReceivedString(Code& data, int number)
{
  // Hostname
  if (String(data.encoding) == "UNKNOWN")
  {
    sendMultiCast("OK VALUES " + deviceID + " CR0" + number + "_Json_Local_IP=http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{'data':[" + String(data.raw) + "], 'type':'raw', 'khz':38}]");
  }
  else
  {
    sendMultiCast("OK VALUES " + deviceID + " CR0" + number + "_Json_Local_IP=http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{'data':'" + String(data.data) + "', 'type':'" + String(data.encoding) + "', 'length':" + String(data.bits) + "}]");
  }
}

String CreateKVPSystemInfoString()
{
  DEBUG_PRINT("Get KVP system information string...");
  String result;

  result = "OK VALUES ";
  result += deviceID;
  result += " ChipID=";
  result += GetChipID();
  result += ",FlashChipId=";
  result += ESP.getFlashChipId();
  result += ",MAC=";
  result += WiFi.macAddress();
  result += ",Version=";
  result += VERSION;

  return result;
}
String CreateKVPCommandURLString()    
{   
  DEBUG_PRINT("Get KVP command URL string...");   
  String result;    
  result = "OK VALUES ";    
  result += deviceID;   
  result += " UpdateURL=http://";   
  result += ipToString(WiFi.localIP()) + ":" + String(port) + "/upload";    
  result += ",ResetURL=http://";    
  result += ipToString(WiFi.localIP()) + ":" + String(port) + "/reset";   
  return result;    
}
String CreateKVPInitString()
{
  DEBUG_PRINT("Create KVP init string...");
  String result;
  result = "OK VALUES ";
  result += deviceID;
  return result;
}
void sendMultiCast(String msg) {
  DEBUG_PRINT("Send UPD-Multicast: ");
  DEBUG_PRINT(msg);
  if (WiFiUdp.beginPacketMulticast(ipMulti, portMulti, WiFi.localIP()) == 1) 
  {
    WiFiUdp.write(msg.c_str());
    WiFiUdp.endPacket();
    yield();
  }
}

/**************************************************************************
   Main loop
**************************************************************************/
void loop() {
  server.handleClient();
  decode_results  results;                                       // Somewhere to store the results
  timeClient.update();                // update only after 



  if (irrecv.decode(&results)) {                                  // Grab an IR code
    Serial.println("Signal received:");
    
    copyCode(last_recv_4,last_recv_5);
    copyCode(last_recv_3,last_recv_4);
    copyCode(last_recv_2,last_recv_3);
    copyCode(last_recv,last_recv_2);
    cvrtCode(last_recv, &results);
    strncpy(last_recv.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);  // Set the new update time
    last_recv.valid = true;
    
    fullCode(&results); 
    dumpCode(&results);                                           // Output the results as source code
    
    if (last_recv.valid)  Serial.println( last_recv.raw );
    if (last_recv_2.valid) Serial.println( last_recv_2.raw );
    if (last_recv_3.valid) Serial.println( last_recv_3.raw );
    if (last_recv_4.valid) Serial.println( last_recv_4.raw );
    if (last_recv_5.valid) Serial.println( last_recv_5.raw );
    irrecv.resume();                                              // Prepare for the next value
    digitalWrite(LED_PIN, LOW);                                    // Turn on the LED for 0.5 seconds
    ticker.attach(0.5, disableLed);
    sendKVPCodeString();
  }

  if (Serial.available() == true)
  {
    String command = Serial.readString();
    DEBUG_PRINT(command);

    if (command == "reset") Handle_ResetWiFi();
  }
}
