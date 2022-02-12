/*
  references: https://randomnerdtutorials.com/esp32-deep-sleep-arduino-ide-wake-up-sources/
              https://electronicsinnovation.com/change-esp32-wifi-credentials-without-uploading-code-from-arduino-ide/
              https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/issues/224
       Board: "AI-Thinker ESP32-CAM"
  release 20: Sketch that periodically connects to the local network, checks Telegram messages and returns to sleep mode.
              The sketch is performed for a single cycle and then the ESP32 returns to sleep mode, this makes it possible
              to create battery-powered systems with a very long autonomy. With the variable "minutes_of_sleep" it is
              possible to set every how many minutes the sketch is executed (from 1 to 255). By setting the variable to 0
              the sketch will be executed every 15 seconds. The sketch always starts at 0 second (time detected by the
              network) when "minutes_of_sleep" is set from 1 to 255 (otherwise at seconds 0, 15, 30 or 45 if 
              "minutes_of_sleep" is set to 0). With the "keep_awake" flag it is possible not to make the sketch terminate
              after a cycle. In this case it remains functional and goes into sleep mode for only 5 seconds every 5 minutes.
              The system remains on for 7 seconds on average and then goes into sleep mode for the time programmed in
              "minutes_of_sleep". The sketch is intended for an always powered system. To change the network credentials,
              it is necessary to press the reset button three times (or perform three power cycles) within 3 seconds of
              each other. This activates an access point with the name "ESP32cam" to which you have to access and with the
              browser you need to go to the page "192.168.4.1" where you can set the credentials for the network and
              Telegram Token and Chat_ID. Once the credentials are done. are saved and after 10 seconds the access point
              is closed and the system restarts connecting to the selected network.

              This sketch can be used as a starting point for your projects.
              This sketch sends a photo on the Telegram channel if the text "/ photo" is sent, otherwise it sends a message
              with instructions for any other message. By adding instructions to the "handleNewMessages" routine, you can
              add customized functions.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <UniversalTelegramBot.h>
#include "time.h"
#include <EEPROM.h>

WebServer server(80);

//Address of flash LED embedded 
#define FLASH_LED_PIN  4
#define BUILTIN_LED   33
#define ACCESS_POINT   0
#define NORMAL         1

//CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM                     32
#define RESET_GPIO_NUM                    -1
#define XCLK_GPIO_NUM                      0
#define SIOD_GPIO_NUM                     26
#define SIOC_GPIO_NUM                     27
#define Y9_GPIO_NUM                       35
#define Y8_GPIO_NUM                       34
#define Y7_GPIO_NUM                       39
#define Y6_GPIO_NUM                       36
#define Y5_GPIO_NUM                       21
#define Y4_GPIO_NUM                       19
#define Y3_GPIO_NUM                       18
#define Y2_GPIO_NUM                        5
#define VSYNC_GPIO_NUM                    25
#define HREF_GPIO_NUM                     23
#define PCLK_GPIO_NUM                     22
#define DEFAULT_TIME_END_ACCESS_POINT 600000
#define MAX_TIME_KEEP_AWAKE           300000

// Initialize the NTP function
const char* ntpServer = "europe.pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
struct      tm timeinfo;
byte        second;
byte        minute;
byte        hour;
byte        day;
byte        month;

// Variables declarations
bool          sendPhoto = false;
bool          keep_awake = false;
byte          minutes_of_sleep = 0;
byte          number_consecutive_reset;
byte          access_point_or_normal = NORMAL; 
String        stringListNetworks;
String        BOTtoken; 
String        defaultChatId;
unsigned long when_finish_master_mode = DEFAULT_TIME_END_ACCESS_POINT;
unsigned long when_finish_keep_wakeup_mode = MAX_TIME_KEEP_AWAKE;

// Wifi connections
WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOTtoken, clientTCP);

//***************************************************************
//                       MAIN ROUTINE                           *
//***************************************************************
void handleNewMessages(int numNewMessages)
{
  String textAnswer;
  for (int i = 0; i < numNewMessages; i++)
  {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != defaultChatId)
    {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

// Print the received message
    String telegram_text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    Serial.print("Massage received from Telegram: ");
    Serial.print(telegram_text);
    Serial.print(" from: ");
    Serial.println(from_name);
    
//***************************************************************
    if (telegram_text == "/photo")
    {
      sendPhoto = true;
      Serial.println("New photo request");
    }
//***************************************************************
    else
    {
      textAnswer = "Welcome , " + from_name + "!\n";
      textAnswer += "Use the following commands to interact with the ESP32-CAM \n";
      textAnswer += "/photo : takes a new photo\n";
      bot.sendMessage(defaultChatId, textAnswer, "");
    }
//***************************************************************
  }
}

void setup()
{
  // Init Serial Monitor
  Serial.begin(115200);
  Serial.println(" ");
  Serial.flush();

  // Set LED Flash as output
  pinMode(FLASH_LED_PIN, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  digitalWrite(BUILTIN_LED, HIGH); // Inverted

  // Initialasing EEPROM
  EEPROM.begin(512);

  // Method to know the reason of the wakeup
  byte wakeup_reason = esp_sleep_get_wakeup_cause();

  // Wakeup caused by timer
  if (wakeup_reason == 4)
  {
    Serial.println("Wakeup caused by timer");
  }
  // Wakeup cause by power outage or reset
  else
  {
    Serial.print("Wakeup was not caused by deep sleep. Reason: ");
    Serial.println(wakeup_reason);

    // Read and update the number of consecutive reset. 3 consecutive reset is the command to change the network credentials
    number_consecutive_reset = EEPROM.read(200);
    if (number_consecutive_reset < 3)
    {
      number_consecutive_reset++;
      EEPROM.write(200, number_consecutive_reset);
      EEPROM.commit();
    }
  }

  // If there have been 3 consecutive resets, the master mode is activated
  if (number_consecutive_reset >= 3)
  {
    access_point_or_normal = ACCESS_POINT; 
    Serial.println("ESP starts as ACCESS POINT");
  
    // Before to start the hotspot are checked the networks available
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(200);
    int n_networks = WiFi.scanNetworks();
    Serial.println(F("Scan done"));
    if (n_networks == 0)
    {
      Serial.println(F("No networks found"));
      stringListNetworks = F("No networks found<br>");
    }
    else
    {
      Serial.print(n_networks);
      Serial.println(F(" networks found"));
      stringListNetworks = F("");
      for (int i = 0; i < n_networks; ++i)
      {
        // Print SSID and RSSI for each network found
        Serial.print(i + 1);
        Serial.print(F(": "));
        Serial.print(WiFi.SSID(i));
        Serial.print(F(" ("));
        Serial.print(WiFi.RSSI(i));
        Serial.println(F("dB)"));
        stringListNetworks += WiFi.SSID(i);
        stringListNetworks += F(" (");
        stringListNetworks += WiFi.RSSI(i);
        stringListNetworks += F("dB)");
        stringListNetworks += F("<br>");
        delay(10);
      }
      Serial.println("");
    }

    // Opening of hotspot
    delay(200);
    WiFi.softAP("ESP32cam", "");
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
    server.onNotFound(handleWebPage);
    server.begin();
    Serial.println("Server started");
  }
  else
  {
    access_point_or_normal = NORMAL; 
    Serial.print("ESP starts as NORMAL. Number of consecutive reset: ");
    Serial.println(number_consecutive_reset);

    // Read SSID and Password from EEPROM
    String esid;
    for (byte i = 0; i < 32; ++i)
    {
      esid += char(EEPROM.read(i));
    }
    String epass = "";
    for (int i = 32; i < 96; ++i)
    {
      epass += char(EEPROM.read(i));
    }
    for (int i = 96; i < 160; ++i)
    {
      BOTtoken += char(EEPROM.read(i));
    }
    BOTtoken = BOTtoken.c_str();
    bot.updateToken(String(BOTtoken));
    for (int i = 160; i < 176; ++i)
    {
      defaultChatId += char(EEPROM.read(i));
    }
    defaultChatId = defaultChatId.c_str();

    //Serial.print(F("    SSID stored in EEPROM: "));
    //Serial.println(esid);
    //Serial.print(F("PASSWORD stored in EEPROM: "));
    //Serial.println(epass);
    //Serial.print(F("   TOKEN stored in EEPROM: "));
    //Serial.println(BOTtoken);
    //Serial.print(F(" CHAT ID stored in EEPROM: "));
    //Serial.println(defaultChatId);

    // Connect to Wi-Fi
    Serial.print("Connecting to ");
    Serial.println(esid);
    WiFi.setAutoConnect(true);   
    WiFi.mode(WIFI_STA);
    WiFi.begin(esid.c_str(), epass.c_str());

    // Add root certificate for api.telegram.org  
    clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  
    // WiFi connection with 10 seconds of timeout
    int connection_timeout = 10 * 10; // 10 seconds
    bool initial_configuration_done = false;
    while(WiFi.status() != WL_CONNECTED  && (connection_timeout-- > 0))
    {
      // Config and init the camera in meantime that the wifi connection is fixed
      if (initial_configuration_done == false)
      {
        config_camera();
        initial_configuration_done = true;
      }
      delay(100);
      Serial.print(".");
    }
    Serial.println("");
  
    if(WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Connection timeout. Failed to connect, going back to sleep");
    }
  
    // Update the monitor with the IP address
    Serial.print("ESP32-CAM IP Address: ");
    Serial.println(WiFi.localIP());
  
    // init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();
  }
}

void loop()
{
  // Normal routine used if there is not a request to chage the networ credentials
  if (access_point_or_normal == NORMAL)
  {

    // Check the Telegram messages
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages)
    {
      Serial.println("Received message");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    // The picture must be done the cycle before the telegram message ?!?I don't know why?!?
    if (sendPhoto) 
    {
      Serial.println("Preparing photo");
      sendPhotoTelegram(); 
      sendPhoto = false; 
    }

    // Sleep procedure with syncronization of wakeup
    if ((keep_awake == false) || (millis() > MAX_TIME_KEEP_AWAKE))
    {
      byte seconds_to_next_wakeup;
      if(!getLocalTime(&timeinfo))
      {
        Serial.println("Failed to obtain time before to sleep");
        seconds_to_next_wakeup = 60;
      }
      else
      {
        second = timeinfo.tm_sec;
        if ((minutes_of_sleep == 0) || (keep_awake == true))
        {
          seconds_to_next_wakeup = (16 - (second % 15));
        }
        else
        {
          seconds_to_next_wakeup = (60 - second) + ((minutes_of_sleep - 1) * 60);
        }
      }
  
      // Reset the number of consecutive reset
      if (number_consecutive_reset != 0)
      {
        number_consecutive_reset = 0;
        EEPROM.write(200, number_consecutive_reset);
        EEPROM.commit();
      }
      esp_sleep_enable_timer_wakeup(seconds_to_next_wakeup * 1000000); 
  
      // The two rows below allow to keep the flash led embedded switched off
      gpio_hold_en(GPIO_NUM_4);
      gpio_deep_sleep_hold_en();   
  
      Serial.print("Going to sleep now. Time left powered (in millisends): ");
      Serial.println(millis());
      Serial.println();
  
      Serial.flush();
      esp_deep_sleep_start();
    }
  }
  // Routine used when there is a request to chage the networ credentials
  else if (access_point_or_normal == ACCESS_POINT)
  {
    digitalWrite(BUILTIN_LED, LOW); // Inverted
    delay(10);
    digitalWrite(BUILTIN_LED, HIGH); // Inverted
    delay(1000);

    // System routine that it manages the server
    server.handleClient();

    // After a fixed time the ACCESS_POINT ends. 
    if (millis() > when_finish_master_mode)
    {
      // Reset the number of consecutive reset
      number_consecutive_reset = 0;
      EEPROM.write(200, number_consecutive_reset);
      EEPROM.commit();

      // Goind in sleep mode for 5 secods
      Serial.println("End of ACCESS_POINT. Going to sleep now for 5 seconds");
      Serial.println();
      esp_sleep_enable_timer_wakeup(5 * 1000000);    

      // The two rows below allow to keep the flash led embedded switched off
      gpio_hold_en(GPIO_NUM_4);
      gpio_deep_sleep_hold_en();   

      Serial.flush();
      esp_deep_sleep_start();
    }
  }
}

String sendPhotoTelegram()
{
  const char* myDomain = "api.telegram.org";
  String getAll = "";
  String getBody = "";

  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();  
  if(!fb)
  {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
    return "Camera capture failed";
  }  
  
  Serial.println("Connect to " + String(myDomain));

  if (clientTCP.connect(myDomain, 443))
  {
    Serial.println("Connection successful");
    
    String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + defaultChatId + "\r\n--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--RandomNerdTutorials--\r\n";
    uint16_t imageLen = fb->len;
    uint16_t extraLen = head.length() + tail.length();
    uint16_t totalLen = imageLen + extraLen;
  
    clientTCP.println("POST /bot" + BOTtoken + "/sendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(myDomain));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
    clientTCP.println();
    clientTCP.print(head);
  
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0;n<fbLen;n=n+1024)
    {
      if (n + 1024 < fbLen)
      {
        clientTCP.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen % 1024 > 0)
      {
        size_t remainder = fbLen % 1024;
        clientTCP.write(fbBuf, remainder);
      }
    }  
    
    clientTCP.print(tail);
    esp_camera_fb_return(fb);
    
    int waitTime = 10000;   // timeout 10 seconds
    long startTimer = millis();
    boolean state = false;
    
    while ((startTimer + waitTime) > millis())
    {
      Serial.print(".");
      delay(100);      
      while (clientTCP.available())
      {
        char c = clientTCP.read();
        if (state == true) getBody += String(c);        
        if (c == '\n')
        {
          if (getAll.length() == 0) state = true; 
          getAll = "";
        } 
        else if (c != '\r')
          getAll += String(c);
        startTimer = millis();
      }
      if (getBody.length() > 0) break;
    }
    clientTCP.stop();
    Serial.println(".");
  }
  else
  {
    getBody = "Connected to api.telegram.org failed.";
    Serial.println("Connected to api.telegram.org failed.");
  }
  return getBody;
}

void config_camera()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;

  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  // camera init 
  esp_err_t err = esp_camera_init(&config); // need 650mS
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  // Drop down frame size for higher initial frame rate
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_CIF);  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA
}

void printLocalTime()
{
  if(!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
  second = timeinfo.tm_sec;
  minute = timeinfo.tm_min;
  hour = timeinfo.tm_hour;
  day = timeinfo.tm_mday;
  month = timeinfo.tm_mon + 1;
}

void handleWebPage()
{
  // Read the request
  String req = server.uri();
  if (req == "/favicon.ico") return;

  // check if was required a setting change
  if (req.indexOf(F("/setting")) != -1)
  {
    String qsid = server.arg(0);
    String qpass = server.arg(1);
    String qtoken = server.arg(2);
    String qtelechat = server.arg(3);
    Serial.println("");
    if (qsid.length() > 0 && qpass.length() > 0)
    {
      for (int i = 0; i < 96; ++i)
      {
        EEPROM.write(i, 0);
      }
      Serial.print(F("Writing eeprom     SSID:"));
      for (int i = 0; i < qsid.length(); ++i)
      {
        EEPROM.write(i, qsid[i]);
        Serial.print(qsid[i]);
      }
      Serial.println();
      Serial.print(F("Writing eeprom PASSWORD:"));
      for (int i = 0; i < qpass.length(); ++i)
      {
        EEPROM.write(32 + i, qpass[i]);
        Serial.print(qpass[i]);
      }
      Serial.println();
      EEPROM.commit();
    }
    if (qtoken.length() > 0 && qtelechat.length() > 0)
    {
      for (int i = 96; i < 176; ++i)
      {
        EEPROM.write(i, 0);
      }
      Serial.print(F("Writing eeprom    TOKEN:"));
      for (int i = 0; i < qtoken.length(); ++i)
      {
        EEPROM.write(96 + i, qtoken[i]);
        Serial.print(qtoken[i]);
      }
      Serial.println();
      Serial.print(F("Writing eeprom  CHAT_ID:"));
      for (int i = 0; i < qtelechat.length(); ++i)
      {
        EEPROM.write(160 + i, qtelechat[i]);
        Serial.print(qtelechat[i]);
      }
      Serial.println();
      EEPROM.commit();
    }
    Serial.println(F("10 seconds to enter in sleep mode"));
    when_finish_master_mode = millis() + 10000;
  }
  
  String stringHTML = F("");
  if (when_finish_master_mode == DEFAULT_TIME_END_ACCESS_POINT)
  {
    stringHTML = F("<!DOCTYPE HTML>\r\n<html><center><b>ESP32cam Wifi Credentials Update</b><p>");
    stringHTML += stringListNetworks;
    stringHTML += F("</p><form method='get' enctype='application/x-www-form-urlencoded' action='setting'><label>SSID: </label><input name='ssid' length=32><br>PASSWORD: <input type=\"password\" name='pass' length=64><br>TELEGRAM TOKEN: <input name='token' length=64><br>TELEGRAM CHAT_ID: <input name='telechat' length=16><br><input type='submit'><center></form>");
    stringHTML += F("</html>");
  }
  else // if when_finish_master_mode != DEFAULT_TIME_END_ACCESS_POINT (mean that the user has changed the credential on web page)
  {
    stringHTML = F("<!DOCTYPE HTML>\r\n<html>ESP32cam Wifi Credentials Update</br></br>In a few seconds the device will be restart and connect to the specified network</html>");
  }
  server.send(200, "text/html", stringHTML);
}
