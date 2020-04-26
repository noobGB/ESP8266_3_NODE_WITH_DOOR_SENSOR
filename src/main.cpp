/*
 * GB 17/02/2019 : Skip wifi connection before main loop on start-up,now on start-up, physical buttons can be operated instantly, meanwhile blynk and wifi will connect in background.
 * GB 16/07/2019 : Priority given to physical switches, on every blynk server connection physical switch state will get reflected on app.
 * GB 08/08/2019 : Added WiFi.hostname(host_name); -- hostname against IP is visible in router
 * GB 09/08/2019 : Running Blynk.run() when BLynk is connected.
 * GB 09/08/2019 : Blynk token,WIFI SSID-Password, hostname are hard coded in case of hardware-reset(SPIFFS memory flush). // removed
 * GB 09/08/2019 : Added arduino OTA_Password
 * GB 09/08/2019 : Added ArduinoOTA.setHostname -- visible port name in arduino ports
 * GB 05/09/2019 : Added wifi reset button on app
 * GB 05/09/2019 : Added HTTP OTA update via internet.
 * GB 13/09/2019 : Changing physical switch check interval to 300ms from 100ms for overall better performance
 * GB 18/09/2019 : Updated door sensor code for accurate notification
 * GB 08/10/2019 : Migrated to arduinoJson 6, uping json data in variable data change. Removed writejson
 * GB 08/10/2019 : saving blynk token in EEPROM and reading  as backup for SPIFFS mount failure and auto SPIFFS format
 */

//#define BLYNK_PRINT Serial //disabled it for production use
#define BLYNK_HEARTBEAT 10 // ping blynk server every 10 second to check for the connection

#include <Arduino.h>
#include <FS.h>                 //handles file system SPIFFS
#include <ESP8266mDNS.h>        // Useful for hostname in router and arduino OTA
#include <WiFiManagerESP8266.h> //https://github.com/tzapu/WiFiManager
#include <BlynkSimpleEsp8266.h> // main blynk library for esp8266
#include <ArduinoJson.h>        //version - 5.13.2    https://github.com/bblanchon/ArduinoJson
#include <ArduinoOTA.h>         // basic arduino OTA library
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>  // saving Blynk token in EEPROM as backup for SPIFFS mount failure
#include <TimeLib.h> //Blynk RTC library
#include <WidgetRTC.h>
//#include <WiFiClient.h>
//#include <ESP8266HTTPClient.h>
//#include <ESP8266WebServer.h>  // web server creation for web config portal
//#include <ESP8266WiFi.h>  //https://github.com/esp8266/Arduino

//Declare wifi client for HTTP update
WiFiClient client;
HTTPClient httpClient;

//Declare rtc object
WidgetRTC rtc;
String currentTime; // store current timestamp

// declare terminal for WiFi reset and HTTP update msg
WidgetTerminal terminal(V22);

// Declare Blynk provided timer, each timer object can handle 10 function calls max
BlynkTimer timer;
BlynkTimer timer2;

// declaring DynamicJsonDocument globally
const size_t capacity = JSON_OBJECT_SIZE(8) + 400; //  + bytes suggested by ArduinoJson assistant
DynamicJsonDocument doc(capacity);


char blynk_tokenJson[34] = "default";     // Blynk Token provided a default value in case of SPIFFS mount failure and auto format, it will help to connect with blynk via EEPROM saved token
char blynk_temp_value[34];
char host_name[100] = "3 Node Module";    // default hostname for wifimanager AP and arduino OTA, we can change it in configuration portal as one time setup
char server_id[3];                        // 2 for blynk cloud, receive this value via config portal
char ssid[100] = "default";               // set default to identify the initial state or SPIFFS mount failure
char password[100] = "default";           // set default to identify the initial state or SPIFFS mount failure
char Product_Key_Json[12] = "qwertyuiop"; // it is smart_key in configuration page, it shows the device ownership
char product_key_temp_value[12];
char Device_name[100];                    // receive device name via config portal as hostname
char OTA_Password[100] = "admin";         // Arduino OTA password, change it when needed

//bool Wifi_init=false; // for 1st time wifi connection, use ssid, password variable and set Wifi_init=true then use internally saved wifi credentials..
//sometimes wifi is getting disconnected in between the normal operation, I suspect the issue is with ssid, password variable

// sometimes ESP disconnect from wifi in midway of the program and does not connect, we need to restart the esp after monitoring the behaviour
bool scan_reboot = false;                // ESP reboot, if true
unsigned long previousMillis = millis(); // timer variable
unsigned long currentMillis = millis();  // current timestamp variable
const long interval = 3600000;           // one hour Interval to reset the ESP if wifi is not connected

//Declare variables for HTTP update
const char *version_url = "http://gbinfosystems.store/HTTP_Update_Production/3_Node_Module/Door_Sensor/3_node_Door_Sensor_code_version_new.version"; // change URL here
const char *code_url = "http://gbinfosystems.store/HTTP_Update_Production/3_Node_Module/Door_Sensor/3_node_Door_Sensor_code_http_OTA_new.bin";       // change URL here
int HTTP_OTA_UPDATE = HIGH;                                                                                                                          // Blynk switch enable button(V24) variable to check for ota update via app
int HttpCode_version_file_check;
int HttpCode_code_file_check;
const int file_version = 10;    // change file version with each updates and keep the same in version file
int HTTP_Update_Completed = 1; // Sets to 0 on HTTP update start, this variable(V27) help us to find out if the HTTP update conpleted

//magnetic switch : door sensor
bool Door_opened;
bool Door_closed;
bool door_action = false;
//bool Door_sensor_checked_once=false;
int Door_Switch_Enable = HIGH;
int door_call = HIGH;
int Door_Status = HIGH;
const int Magnetic_Sensor = 5; //D1 -- reserved for digital sensor(PIR,DOOR,TEMP etc.)
//int Magnetic_Sensor_State;

// Set your LED  pins here
const int ledPin1 = 0;  //D3
const int ledPin2 = 14; //D5
const int ledPin3 = 13; //D7

// and one way switch pins here
const int btnPin1 = 4;  //D2
const int btnPin2 = 12; //D6
const int btnPin3 = 3;  //Rx

//Tx // it behaves as the ground pin for physical push/one way switches, once the mcu reboot completed
const int Tx_Pin_GND = 1; //Tx

//Declaring resetbutton : It is used for OnDemandConfig. Press this button for 2-3 second
const int resetbutton = 2; //D4

int resetbutton_state = HIGH;
int resetbutton_enable = HIGH; // app button enable to trigger wifi reset

//flag for saving wifimanager custom parameters to FS
bool shouldSaveConfig = false;

bool Blynk_Connected = false; // becomes true if connected to blynk via Blynk_connected()

// restrict the load state change for the first time when the hardware is powered. For Example, reset due to electricity failure or intensial reset or HTTP update reset etc.
int reset_power = 1;

// shows the wifi connectivity
bool result = false;

//variable for builtin LED control, LED should blink when there is no wifi or blynk connection
bool indicator = false; // it primarily shows blynk connectivity

// In main loop attemp wifi connection in the begining for once then mark it false
bool WifiCheckOnce = true;

// Keep LED states HIGH(off) in the begining, declared low and high one way switch states
int led1State = HIGH;   // LED1 state
int btn1State_h = HIGH; // two way switch or latch switch HIGH state
int btn1State_l = HIGH; //// two way switch or latch switch LOW state

int led2State = HIGH;
int btn2State_h = HIGH;
int btn2State_l = HIGH;

int led3State = HIGH;
int btn3State_h = HIGH;
int btn3State_l = HIGH;

//int led4State = HIGH;
//int btn4State_h = HIGH;
//int btn4State_l = HIGH;

//Initially the BuiltIn LED should be OFF
const int BUILTIN_BOARD_LED = 16; //D0
int BUILTIN_BOARD_LED_state = HIGH;

BLYNK_CONNECTED() // Anything written inside BLYNK_CONNECTED() will get executed once when blynk is connected after some disconnection
{
    terminal.clear(); // clear Terminal window

    // Request the latest state from the server
    //Blynk.syncVirtual(V12);
    //Blynk.syncVirtual(V13);
    //Blynk.syncVirtual(V14);
    //Blynk.syncVirtual(V15);

    Blynk.syncVirtual(V20); // FOR DOOR SENSOR LATEST STATE enable button
    Blynk.syncVirtual(V18); // For wifi RESET enable button latest state
    Blynk.syncVirtual(V24); // For HTTP enable button latest state
    Blynk.syncVirtual(V27); // receive latest for HTTP_Update_Completed from server
    Blynk.syncVirtual(V28); // FOR DOOR SENSOR CALL enable button

    ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot

    //sending local LEDs state to blynk app virtual pins - giving priority to local states
    Blynk.virtualWrite(V12, led1State);
    Blynk.virtualWrite(V13, led2State);
    Blynk.virtualWrite(V14, led3State);
    //Blynk.virtualWrite(V15, led4State);

    Blynk.virtualWrite(V16, HIGH); // keep [All on] button off on blynk connection
    Blynk.virtualWrite(V17, HIGH); // keep [All off] button off on blynk connection
    Blynk.virtualWrite(V19, HIGH); // keep [wifi reset] button off on blynk connection
    Blynk.virtualWrite(V21, HIGH); // Door current status button off on blynk connection

    ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot

    rtc.begin(); // begin rtc

    // we should execute below code whenever the device gets connected to blynk server after some disconnection or first time connection.
    if (digitalRead(Magnetic_Sensor) == HIGH)
    {
        Door_opened = true;
        Door_closed = false;
        //Blynk.notify("{DEVICE_NAME}'s door opened !!");
    }
    else
    {
        Door_opened = false;
        Door_closed = true;
        //Blynk.notify("{DEVICE_NAME}'s door closed !!");
    }

    //updating some local variables on blynk connection
    indicator = true;       // for fast blynk.run() access, make indicator true as soon as it connects to server
    result = true;          // shows wifi connected
    Blynk_Connected = true; // true if connected, will use it in blynkconnectivity function to improve the performance

    ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
}

void ReadConfigJson()
{
    if (SPIFFS.begin())
    {
        // Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json"))
        {
            //file exists, reading and loading
            // Serial.println("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile)
            {
                //Serial.println("opened config file");
                delay(100);
                //const size_t capacity = JSON_OBJECT_SIZE(8) + 400; //  + bytes suggested by ArduinoJson assistant
                //DynamicJsonDocument doc(capacity);
                DeserializationError error = deserializeJson(doc, configFile);
                ESP.wdtFeed();
                if (error)
                {
                    //Serial.print(F("deserializeJson() failed: "));
                }
                //Serial.print("deserializeJson() without error. ");

                // Copy json values to respective variables from the file system

                led1State = doc["led1StateJson"].as<int>();
                led2State = doc["led2StateJson"].as<int>();
                led3State = doc["led3StateJson"].as<int>();

                strlcpy(server_id, doc["server_idJson"] | "2", sizeof(server_id)); // keep the default "2" for blynk cloud server connect
                strlcpy(ssid, doc["Wifi_SSID_Json"] | "default", sizeof(ssid));
                strlcpy(password, doc["Wifi_Pass_Json"] | "default", sizeof(password));
                strlcpy(blynk_tokenJson, doc["blynk_tokenJson"] | "default", sizeof(blynk_tokenJson));
                strlcpy(host_name, doc["Host_name_Json"] | "ESP_Node", sizeof(host_name));

                // serializeJson(doc, Serial);
            }
            else
            {
                // Serial.println("failed to load json config");
            }

            configFile.close();
        }
    }
    else
    {
        // Serial.println("failed to mount FS");
    }
}

void UpdateJsonString(String x1, String x2, String x3, String x4, String x5)
{

    if (SPIFFS.begin())
    {
        // Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json"))
        {
            //file exists, reading and loading
            // Serial.println("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile)
            {
                //Serial.println("opened config file");
                //const size_t capacity = JSON_OBJECT_SIZE(8) + 400; //  + bytes suggested by ArduinoJson assistant
                //DynamicJsonDocument doc(capacity);
                deserializeJson(doc, configFile);
                ESP.wdtFeed();
            }
            else
            {
                // Serial.println("failed to load json config");
            }
            configFile.close();
        }
    }
    else
    {
        // Serial.println("failed to mount FS");
    }

    doc["blynk_tokenJson"] = x1;
    doc["server_idJson"] = x2;
    doc["Wifi_SSID_Json"] = x3;
    doc["Wifi_Pass_Json"] = x4;
    doc["Host_name_Json"] = x5;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
        //Serial.println("failed to open config file !!");
    }

    serializeJson(doc, configFile);
    //delay(100);
    //serializeJson(doc, Serial);
    configFile.close();
}

void UpdateJsonInt(int x1, int x2, int x3)
{

    if (SPIFFS.begin())
    {
        // Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json"))
        {
            //file exists, reading and loading
            // Serial.println("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile)
            {
                //Serial.println("opened config file");
                deserializeJson(doc, configFile);
                ESP.wdtFeed();
            }
            else
            {
                // Serial.println("failed to load json config");
            }
            configFile.close();
        }
    }
    else
    {
        // Serial.println("failed to mount FS");
    }

    doc["led1StateJson"] = x1;
    doc["led2StateJson"] = x2;
    doc["led3StateJson"] = x3;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
        //Serial.println("failed to open config file !!");
    }

    serializeJson(doc, configFile);
    //delay(100);
    //serializeJson(doc, Serial);
    configFile.close();
}

void HttpOtaUpdateNotify()
{

    if (indicator == true)
    {
        //HTTPClient httpClient;
        httpClient.begin(client, version_url);
        HttpCode_version_file_check = httpClient.GET();

        if (HttpCode_version_file_check == 200)
        {

            String firmware_version = httpClient.getString();
            //Blynk.virtualWrite(V23, "\nChecking for firmware updates.");
            int new_file_version = firmware_version.toInt();
            ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
            if (new_file_version > file_version)
            {
                //Blynk.notify("New firmware version available for {DEVICE_NAME}'s module, Please update.");
                Blynk.notify(String("New firmware version(") + String(new_file_version) + String(".0) available for {DEVICE_NAME}'s module, Please update."));
            }
        }
        httpClient.end();
    }
    //HTTP_Update_Completed=1;
    Blynk.virtualWrite(V27, HIGH);
    Blynk.virtualWrite(V26, HIGH);
}

void CheckHttpOtaUpdate()
{
    if (HTTP_OTA_UPDATE == LOW)
    { // HTTP check button enabled
        terminal.clear();
        delay(100);
        Blynk.virtualWrite(V22, "* Checking for firmware updates.");
        //HTTPClient httpClient;
        httpClient.begin(client, version_url);
        HttpCode_version_file_check = httpClient.GET();

        if (HttpCode_version_file_check == 200)
        { // if ping successful

            String firmware_version = httpClient.getString();
            int new_file_version = firmware_version.toInt();

            if (new_file_version > file_version)
            {
                ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot

                char currentString[200];
                sprintf(currentString, "\n* Updated version(%d.0) available for this module.", new_file_version);
                Blynk.virtualWrite(V22, currentString);
                //Blynk.virtualWrite(V22, "\n* Updated version available for this module.");
                Blynk.virtualWrite(V22, "\n* Preparing to update.");
                Blynk.virtualWrite(V22, "\n* Turning off all the loads.");
                // Turning all the loads off, meanwhile physical and digital switches will not be accessible
                digitalWrite(BUILTIN_BOARD_LED, HIGH);
                digitalWrite(ledPin1, HIGH);
                Blynk.virtualWrite(V12, HIGH);
                digitalWrite(ledPin2, HIGH);
                Blynk.virtualWrite(V13, HIGH);
                digitalWrite(ledPin3, HIGH);
                Blynk.virtualWrite(V14, HIGH);
                //digitalWrite(ledPin4, HIGH);
                //Blynk.virtualWrite(V15, HIGH);
                delay(100);

                UpdateJsonInt(led1State, led2State, led3State);

                Blynk.virtualWrite(V22, "\n* Firmware update initiated.");
                Blynk.virtualWrite(V22, "\n* Now your app will disconnect from this module.");
                Blynk.virtualWrite(V22, "\n* Meanwhile physical and digital switches will not be \n  accessible.");
                delay(100);
                //Blynk.run();

                HTTP_Update_Completed = 0;    // This variable is used to mark the initiation of HTTP update
                Blynk.virtualWrite(V27, LOW); // save this variable data to server(cloud or local)
                //delay(200);
                ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
                Blynk.run();
                //delay(200);
                ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
                t_httpUpdate_return ret = ESPhttpUpdate.update(client, code_url);
                switch (ret)
                {
                case HTTP_UPDATE_FAILED:
                    char currentString[64];
                    sprintf(currentString, "\n* HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
                    Blynk.virtualWrite(V22, currentString);

                    break;

                case HTTP_UPDATE_NO_UPDATES:

                    Blynk.virtualWrite(V22, "\n* HTTP_UPDATE_NO_UPDATES\n");

                    break;

                case HTTP_UPDATE_OK:
                    printf("ESP restarts after OTA completion");

                    break;
                }
            }
            else
            {

                char currentString2[200]; //
                sprintf(currentString2, "\n* No updates, this module already running latest \n  version(%d.0) of firmware.\n", file_version);
                Blynk.virtualWrite(V22, currentString2);
            }
        }
        else
        {
            //char currentString[64];
            //sprintf(currentString, "\n* Firmware version check failed, got HTTP response code : %d\n",HttpCode_code_file_check);
            Blynk.virtualWrite(V22, "\n* Firmware version check failed due to connection issue with the web server.");
        }
        httpClient.end();
    }

    // restoring the last load states if wifi/internet disconnect in between the HTTP update
    digitalWrite(ledPin1, led1State);
    digitalWrite(ledPin2, led2State);
    digitalWrite(ledPin3, led3State);
    //digitalWrite(ledPin4, led4State);

    if (WiFi.status() == WL_CONNECTED)
    {
        if (Blynk.connected() == false)
        {
            ESP.wdtFeed();       // feed ESP for its internal processing or it will reboot
            Blynk.connect(3000); // attempt blynk connection
            ESP.wdtFeed();       // feed ESP for its internal processing or it will reboot
        }
        else
        {
            Blynk.virtualWrite(V12, led1State);
            Blynk.virtualWrite(V13, led2State);
            Blynk.virtualWrite(V14, led3State);
        }
    }
}

// [All ON] button on app
BLYNK_WRITE(V16)
{
    if (param.asInt() == 0)
    {
        led1State = param.asInt(); // receive the V16 pin state in INT from blynk server and write it to led1state
        led2State = param.asInt();
        led3State = param.asInt();
        //led4State = param.asInt();
        UpdateJsonInt(led1State, led2State, led3State);

        digitalWrite(ledPin1, led1State);
        Blynk.virtualWrite(V12, led1State); //change the widget status as well
        digitalWrite(ledPin2, led2State);
        Blynk.virtualWrite(V13, led2State);
        digitalWrite(ledPin3, led2State);
        Blynk.virtualWrite(V14, led3State);
        //digitalWrite(ledPin4, led3State);
        //Blynk.virtualWrite(V15, led4State);
        // save(or update) current LedStates to SPIFFS in json format
        //UpdateJsonInt(led1State, led2State, led3State);
    }
    Blynk.virtualWrite(V16, HIGH); // using [All ON] widget in switch mode, so turning off once process done
}

//[All OFF] button on app
BLYNK_WRITE(V17)
{
    if (param.asInt() == 0)
    {
        led1State = !param.asInt(); // receive the V17 pin state in INT from blynk server and write complement of it to led1state
        led2State = !param.asInt();
        led3State = !param.asInt();
        //led4State = !param.asInt();
        UpdateJsonInt(led1State, led2State, led3State);

        digitalWrite(ledPin1, led1State);
        Blynk.virtualWrite(V12, led1State); //change the widget status as well
        digitalWrite(ledPin2, led2State);
        Blynk.virtualWrite(V13, led2State);
        digitalWrite(ledPin3, led2State);
        Blynk.virtualWrite(V14, led3State);
        //digitalWrite(ledPin4, led3State);
        //Blynk.virtualWrite(V15, led4State);

        // save current LedStates to SPIFFS in json format
        //UpdateJsonInt(led1State, led2State, led3State);
    }
    Blynk.virtualWrite(V17, HIGH); //using [All OFF] widget in switch mode, so turning off once process done
}

// Loads are conneted to Blynk virtual pins (V12,V13,V14,V15)
BLYNK_WRITE(V12)
{                              // app button one
    led1State = param.asInt(); // receive the V12 pin state in INT from blynk server and write it to led1state
    UpdateJsonInt(led1State, led2State, led3State);
    digitalWrite(ledPin1, led1State);
    //Serial.println(F("It is working man!!"));
    // save current LedStates to SPIFFS in json
    //UpdateJsonInt(led1State, led2State, led3State);
}
BLYNK_WRITE(V13)
{ // app button two
    led2State = param.asInt();
    UpdateJsonInt(led1State, led2State, led3State);
    digitalWrite(ledPin2, led2State);
    //UpdateJsonInt(led1State, led2State, led3State);
}
BLYNK_WRITE(V14)
{ // app button three
    led3State = param.asInt();
    UpdateJsonInt(led1State, led2State, led3State);
    digitalWrite(ledPin3, led3State);
    //UpdateJsonInt(led1State, led2State, led3State);
}

//BLYNK_WRITE(V15) { // app button four
//    led4State = param.asInt();
//    digitalWrite(ledPin4, led4State);
//    UpdateJsonInt(led1State,led2State,led3State);
//}

BLYNK_WRITE(V18)
{ // wifi reset enable button
    resetbutton_enable = param.asInt();
}

BLYNK_WRITE(V19)
{ //WIFI reset button
    resetbutton_state = param.asInt();
    if (resetbutton_state == LOW && resetbutton_enable == LOW)
    {
        digitalWrite(resetbutton, resetbutton_state);
        delay(500);
    }
    Blynk.virtualWrite(V19, HIGH); // using V19 in switch mode, turning off once process complete
}

BLYNK_WRITE(V20) // enable door sensor notification
{
    Door_Switch_Enable = param.asInt();
}

BLYNK_WRITE(V21) // door current status check
{
    Door_Status = param.asInt();
    if (Door_Status == LOW && Door_Switch_Enable == LOW)
    {
        if (digitalRead(Magnetic_Sensor) == HIGH)
        {
            Door_opened = true;
            Door_closed = false;
            Blynk.notify("{DEVICE_NAME}'s door opened !!");
        }
        else
        {
            Door_opened = false;
            Door_closed = true;
            Blynk.notify("{DEVICE_NAME}'s door closed !!");
        }
    }
    Blynk.virtualWrite(V21, HIGH);
}

BLYNK_WRITE(V24)
{ //http update enable button
    HTTP_OTA_UPDATE = param.asInt();
}

BLYNK_WRITE(V25)
{ // check HTTP OTA update push button on blynk app
    if (param.asInt() == 0)
    {
        CheckHttpOtaUpdate(); // main HTTP update function
    }
}

// below just for testing HTTP update notification function, button widget(V26) will not be available in production app design
BLYNK_WRITE(V26)
{
    if (param.asInt() == 0)
    {
        HttpOtaUpdateNotify(); // check current state for latest version availability
    }
    Blynk.virtualWrite(V26, HIGH); // using V26 in switch mode, turning off once process complete
}

BLYNK_WRITE(V27)
{
    // on HTTP update start, set  HTTP_Update_Completed = 0 and V27 = 0
    // after update(fail or success), send a notification to user and update this variable and V27 to 1

    if (param.asInt() == 0)
    {
        HTTP_Update_Completed = 0;
    }
}

BLYNK_WRITE(V28)
{ //http update enable button
    door_call = param.asInt();
}

//callback notifying us of the need to save config for wifimanager
void saveConfigCallback()
{
    //Serial.println("Should save config");
    shouldSaveConfig = true;
}

void WifimangerSetupCall()
{

    ReadConfigJson(); // Read saved json data

    // in case of power failure or intensional MCU reset, the LEDs should light up with the last stored state as soon as the MCU gets powered up,
    // for to achieve this we are declaring LED pinmodes here

    pinMode(BUILTIN_BOARD_LED, OUTPUT);
    pinMode(ledPin1, OUTPUT);
    pinMode(ledPin2, OUTPUT);
    pinMode(ledPin3, OUTPUT);
    //pinMode(ledPin4, OUTPUT);

    // DigitalWrite LEDs with the last stored state in FS
    digitalWrite(BUILTIN_BOARD_LED, BUILTIN_BOARD_LED_state);
    digitalWrite(ledPin1, led1State);
    digitalWrite(ledPin2, led2State);
    digitalWrite(ledPin3, led3State);
    //digitalWrite(ledPin4, led4State);

    WiFi.persistent(false); // it helps to update the saved credentials for current program only, not permanently on flash memory
    //WiFi.disconnect();
    WiFi.mode(WIFI_STA);      // putting ESP on station mode to connect with a access point as wifi client
    WiFi.hostname(host_name); // set hostname to be visible in router

    ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot

    String x = String(blynk_tokenJson);

    // if due to SPIFFS mount failure and auto format ReadConfigJson() did not work then we will fetch token data from EEPROM based on below if condition
    //if ((strcmp(blynk_tokenJson, "default") == 0) || sizeof(blynk_tokenJson)!=32) {
    if ((strcmp(blynk_tokenJson, "default") == 0) || x.length() != 32)
    {
        int address = 0;
        strcpy(server_id, "2");
        for (int i = 0; i < 32; i++)
        {
            byte value = EEPROM.read(address);
            char a = char(value);
            blynk_tokenJson[address] = a;
            // Serial.println(blynk_tokenJson[address]);
            address = address + 1;
        }
    }

    // configure blynk connection based on server id received from config portal, 2 is for cloud and 1 or 0 for local server
    if (strcmp(server_id, "0") == 0)
    {
        Blynk.config(blynk_tokenJson, IPAddress(192, 168, 0, 108), 8080);
    }

    if (strcmp(server_id, "1") == 0)
    {
        Blynk.config(blynk_tokenJson, IPAddress(192, 168, 1, 108), 8080);
    }

    if (strcmp(server_id, "2") == 0)
    {
        Blynk.config(blynk_tokenJson);
    }
}

void WifiReconnect()
{
    if (result == false)
    {

        // if ssid and password are default means,it is initial stage of SPIFFS mount failed and auto formated the data, in this case we will connect with the save wifi credential in sdk using wifi.begin()
        if ((strcmp(ssid, "default") == 0) && (strcmp(password, "default") == 0))
        {
            WiFi.begin(); // connect to WIFI
        }
        else
        {
            WiFi.begin(ssid, password); // connect to WIFI
        }

        indicator = false; // blynk not connected, on board led should be blinking
        result = false;    // set result to false, blynk.run() function will not run
        ESP.wdtFeed();     // feed ESP for its internal processing or it will reboot
    }
}

void OnDemandConfigPortalCheck()
{

    if (digitalRead(resetbutton) == LOW) // Check if resetbutton is LOW
    {
        terminal.clear(); // clear terminal window to show configuraion msgs
        delay(100);
        terminal.println("* Wifi reset process initiated.");
        terminal.flush();
        terminal.println("* Turning off all the loads.");
        terminal.flush();

        // Turning all the loads off before entering wifimanager mode for security, meanwhile physical and digital switches will not be accessible
        digitalWrite(BUILTIN_BOARD_LED, HIGH);
        digitalWrite(ledPin1, HIGH);
        Blynk.virtualWrite(V12, HIGH);
        digitalWrite(ledPin2, HIGH);
        Blynk.virtualWrite(V13, HIGH);
        digitalWrite(ledPin3, HIGH);
        Blynk.virtualWrite(V14, HIGH);
        //digitalWrite(ledPin4, HIGH);
        delay(500);

        UpdateJsonInt(led1State, led2State, led3State);

        String wifi_init = "* Check for WiFi named ";
        String bracket_start = "[";
        String AP_name = String(host_name) + " Connect";
        String bracket_end = "].";
        String wifi_name_msg = wifi_init + bracket_start + AP_name + bracket_end;

        terminal.println("* Now your app will disconnect from the module.");
        terminal.flush();
        terminal.println("* Physical and Digital switches will not be accessible.");
        terminal.flush();
        terminal.println(wifi_name_msg);
        terminal.flush();
        terminal.println("* WiFi password is 12345678.");
        terminal.flush();
        terminal.println("* Check web portal at 192.168.4.1 on your browser.");
        terminal.flush();
        terminal.println("* Enter new WiFi credential and press SAVE button.");
        terminal.flush();
        terminal.println("* Now wait for a moment,your module and app should get");
        terminal.flush();
        terminal.println("  connected to the server soon.");
        terminal.flush();

        ESP.wdtFeed();
        delay(100);
        Blynk.run();
        ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
        delay(100);

        Blynk_Connected = false;
        digitalWrite(BUILTIN_BOARD_LED, HIGH); // turn off the builtIn LED
        digitalWrite(resetbutton, HIGH);       // turn off the reset LED
        WiFiManager wifiManager;               //initialize wifimanager

        // The extra parameters to be configured (can be either global or just in the setup)
        // After connecting, parameter.getValue() will get you the configured value
        // parameters are in this order -- id/name, placeholder/prompt, default, length

        //Fetching blynk token
        WiFiManagerParameter custom_blynk_token("Token", "Blynk Token", "Not Available", 34);

        //Fetchin [Smart key] from the config portal to verify the owner access
        WiFiManagerParameter custom_Product_Key("Smart Key", "Smart Key", "Not Available", 15);

        //Server ID can have 3 values,[0,1,2] for (192.168.0.108),for(192.168.1.108) and for cloud server respectively, keeping default value "2" for cloud connection
        WiFiManagerParameter custom_Server_ID("Server ID", "2", "2", 1);

        //Fetching hostname
        WiFiManagerParameter custom_Device_Name("Host Name", "kitchen", "ESP_Node", 100);

        //set config save notify callback
        wifiManager.setSaveConfigCallback(saveConfigCallback);

        //set static ip
        //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

        //add all your custom parameters here
        wifiManager.addParameter(&custom_blynk_token);
        wifiManager.addParameter(&custom_Product_Key);
        wifiManager.addParameter(&custom_Server_ID);
        wifiManager.addParameter(&custom_Device_Name);

        //set minimum quality of signal so it ignores AP's under that quality,defaults to 8%
        //wifiManager.setMinimumSignalQuality();

        //keep persistant true to make any wifi credential change to flash drive
        WiFi.persistent(true);

        //disconnect wifi or forget wifi credential- since persistent is true then it will erase the wifi credential from the flash drive
        wifiManager.resetSettings();

        delay(100);

        // set the config portal timeout : if no action is taken then it will get timed out and return back to the main program
        wifiManager.setTimeout(45); // set to 45 seconds

        //start config portal : wifi name and password is mentioned,  AP_name variable declared in the begining of this function
        wifiManager.startConfigPortal(AP_name.c_str(), "12345678");

        delay(100);

        // restoring the last load states
        digitalWrite(ledPin1, led1State);
        digitalWrite(ledPin2, led2State);
        digitalWrite(ledPin3, led3State);
        //digitalWrite(ledPin4, led4State);

        //there may be two conditions.
        //1.AP gets timed out then no wifi credential change
        //2.new wifi credential entered and gets connected
        if (WiFi.status() == WL_CONNECTED)
        {
            strcpy(ssid, WiFi.SSID().c_str());    // store saved wifi SSID to ssid variable
            strcpy(password, WiFi.psk().c_str()); // store saved PASSWORD to password variable
            result = true;
        }
        else
        {
            WiFi.mode(WIFI_STA); // putting ESP on station mode to connect with a access point as wifi client
            result = false;
            WifiReconnect();
            delay(6000); // delay for wifi connection
        }

        WiFi.persistent(false); // it helps to update the saved credentials for current program only, not permanently on flash memory

        //read updated parameters
        strcpy(blynk_temp_value, custom_blynk_token.getValue());
        strcpy(product_key_temp_value, custom_Product_Key.getValue());

        // if smart key value matches with the provided value in congif portal then save all provided data
        if (strcmp(Product_Key_Json, product_key_temp_value) == 0)
        {
            strcpy(blynk_tokenJson, blynk_temp_value);
            strcpy(server_id, custom_Server_ID.getValue());
            strcpy(Device_name, custom_Device_Name.getValue());
            strcpy(host_name, Device_name);

            if (shouldSaveConfig)
            {

                //WriteConfigJson();
                SPIFFS.format();                                                                                                 // format SPIFFS
                UpdateJsonInt(led1State, led2State, led3State);                                                                  // save load states in SPIFFS
                UpdateJsonString(String(blynk_tokenJson), String(server_id), String(ssid), String(password), String(host_name)); // save these data in SPIFFS

                delay(500);

                // Saving blynk token to EEPROM as backup when SPIFFS mount fails and it may auto format the SPIFFS data
                String x = String(blynk_tokenJson);
                //Serial.print("blynk token : ");
                //Serial.println(x);
                for (unsigned int i = 0; i < x.length(); i++)
                {
                    EEPROM.write(i, x[i]);
                    Serial.println(x[i]);
                }
                EEPROM.commit();
            }
        }

        // read sensor state
        if (digitalRead(Magnetic_Sensor) == HIGH)
        {
            Door_opened = true;
            Door_closed = false;
            //Blynk.notify("{DEVICE_NAME}'s door opened !!");
        }
        else
        {
            Door_opened = false;
            Door_closed = true;
            //Blynk.notify("{DEVICE_NAME}'s door closed !!");
        }

        // configure blynk for server_id from configuration portal
        if (strcmp(server_id, "0") == 0)
        {
            Blynk.config(blynk_tokenJson, IPAddress(192, 168, 0, 108), 8080);
        }

        if (strcmp(server_id, "1") == 0)
        {
            Blynk.config(blynk_tokenJson, IPAddress(192, 168, 1, 108), 8080);
        }

        if (strcmp(server_id, "2") == 0)
        {
            Blynk.config(blynk_tokenJson);
        }

        ESP.wdtFeed();

        // if connected with wifi then connect blynk server(local/cloud) else try connecting wifi again
        if (result != false)
        {
            ESP.wdtFeed();       // feed ESP for its internal processing or it will reboot
            Blynk.connect(3000); // 3 second timeout
            ESP.wdtFeed();       // feed ESP for its internal processing or it will reboot
        }
        else
        {
            result = false;
            WifiReconnect();
            delay(6000);         // delay for wifi connection
            ESP.wdtFeed();       // feed ESP for its internal processing or it will reboot
            Blynk.connect(3000); // 3 second timeout
            ESP.wdtFeed();       // feed ESP for its internal processing or it will reboot
        }
    }
}


void CheckWifiStatusIndicator()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        result = false;
        indicator = false;
        // it has been observed that sometime ESP disconnects from wifi in middle of the program and does not connect even if the last saved wifi ssid is available
        // in this case we will scan wifi network
        // if the last saved wifi is available and not connected to it then start a timer
        if (scan_reboot == false)
        {
            int n = WiFi.scanNetworks();
            for (int i = 0; i < n; i++)
            {
                ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
                if (WiFi.SSID(i) == ssid)
                {
                    previousMillis = millis(); // start timer with current timestamp
                    scan_reboot = true;
                }
                //yield(); // to keep ESP8266 internal house keeping process running and avoid any wdt reset
            }
        }
    }
    else
    {
        result = true;
        scan_reboot = false;
        previousMillis = millis(); // reset the timer
    }
}

void ScanSSIDexist() // scan network in every 5 minute to check if previously connected SSID is available, if not then reset the timer to current timestamp and update scan_reboot to false
{
    int s = 0;

    int m = WiFi.scanNetworks();
    for (int i = 0; i < m; i++)
    {
        if (WiFi.SSID(i) != ssid)
        {
            s = s + 1;
        }
        else
        {
            break;
        }
        //yield(); // to keep ESP8266 internal house keeping process running and avoid any wdt reset
        ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
    }
    if (s == m)
    {
        scan_reboot = false;
        previousMillis = millis();
    }
}

void CheckScanReboot()
{ // reboot ESP if conditions match
    if (scan_reboot == true)
    {
        currentMillis = millis(); // current timestamp

        if (currentMillis - previousMillis >= interval)
        {
            ESP.reset(); // reset if previously connected SSID is available in network scan but not connecting for last one hour due to some unknown issues
        }
    }
}

void LEDtoggle()
{
    if (indicator != true) // if indicator is not true or blynk not connected then builtIn LED should continue blinking else keep it on
    {
        BUILTIN_BOARD_LED_state = !digitalRead(BUILTIN_BOARD_LED);
        digitalWrite(BUILTIN_BOARD_LED, BUILTIN_BOARD_LED_state);
    }
    else
    {
        digitalWrite(BUILTIN_BOARD_LED, LOW);
    }
}

void CheckBlynkConnectivity()
{
    if (result == true) // check if WIFI is already connected,if connected then proceed
    {
        if (Blynk.connected() != true)
        {
            ESP.wdtFeed();          // feed ESP for its internal processing or it will reboot
            HTTP_OTA_UPDATE = HIGH; // make arduino OTA update available when HTTP update not possible
            Blynk_Connected = false;
            indicator = false;   // main variable to indicate if blynk is connected
            Blynk.connect(3000); //Attempt Blynk connection
            ESP.wdtFeed();       // feed ESP for its internal processing or it will reboot
        }
        else if (HTTP_Update_Completed == 0)
        {
            HTTPClient httpClient;
            httpClient.begin(client, version_url);
            HttpCode_version_file_check = httpClient.GET();

            if (HttpCode_version_file_check == 200)
            {
                String firmware_version = httpClient.getString();
                //Blynk.virtualWrite(V23, "\nChecking for firmware updates.");
                int new_file_version = firmware_version.toInt();
                ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
                if (new_file_version > file_version)
                { // if new version available
                    Blynk.notify("Firmware update for {DEVICE_NAME}'s module failed, Please try again.");
                    HTTP_Update_Completed = 1;
                    Blynk.virtualWrite(V27, HIGH);
                }
                else
                {
                    Blynk.notify("Firmware update for {DEVICE_NAME}'s module was successful !!");
                    HTTP_Update_Completed = 1;
                    Blynk.virtualWrite(V27, HIGH);
                }
            }
        }
    }
}


void DoorTimeStamp()
{
    int HOUR = hour();
    int MINUTE = int(minute());

    if (MINUTE < 10)
    {
        if (HOUR > 12)
        {
            HOUR = HOUR - 12;
            currentTime = String(HOUR) + ":" + "0" + minute() + " PM";
        }
        else if (HOUR == 12)
        {
            currentTime = String(HOUR) + ":" + "0" + minute() + " PM";
        }
        else if (HOUR == 0)
        {
            currentTime = "0" + String(HOUR) + ":" + "0" + minute() + " AM";
        }
        else
        {
            currentTime = String(HOUR) + ":" + "0" + minute() + " AM";
        }
    }
    else
    {
        if (HOUR > 12)
        {
            HOUR = HOUR - 12;
            currentTime = String(HOUR) + ":" + minute() + " PM";
        }
        else if (HOUR == 12)
        {
            currentTime = String(HOUR) + ":" + minute() + " PM";
        }

        else if (HOUR == 0)
        {
            currentTime = "0" + String(HOUR) + ":" + minute() + " AM";
        }
        else
        {
            currentTime = String(HOUR) + ":" + minute() + " AM";
        }
    }
}

void Door_Alert()
{
    if (door_action == false)
    {
        if (digitalRead(Magnetic_Sensor) == HIGH && Door_closed == true)
        {

            // unsigned long Magnetic_Switch_Timer = millis();
            Door_opened = true;
            Door_closed = false;

            if (indicator == true && Door_Switch_Enable == LOW)
            {
                DoorTimeStamp();
                Blynk.notify(String("Somebody opened {DEVICE_NAME}'s door at ") + currentTime + String(" !!"));
            }
            //if (door_call == LOW && indicator == true)
            // {
            //     httpClient.begin(client, "http://maker.ifttt.com/trigger/Hall_Door_Opened/with/key/REDACTED_IFTTT_KEY");
            //     httpClient.GET();
            //     httpClient.end();
            // }

            door_action = true;
        }
        else

        {
            if (digitalRead(Magnetic_Sensor) == LOW && Door_opened == true)
            {

                Door_opened = false;
                Door_closed = true;

                if (indicator == true && Door_Switch_Enable == LOW)
                {
                    DoorTimeStamp();
                    Blynk.notify(String("Somebody closed {DEVICE_NAME}'s door at ") + currentTime + String(" !!"));
                }
                // if (door_call == LOW && indicator == true)
                // {
                //     httpClient.begin(client, "http://maker.ifttt.com/trigger/Hall_Door_Closed/with/key/REDACTED_IFTTT_KEY");
                //     httpClient.GET();
                //     httpClient.end();
                // }

                door_action = true;
            }
        }
    }
    else
    {
        door_action = false;
    }
}

void checkPhysicalButton()
{
    if (digitalRead(btnPin1) == LOW) // check if one way switch  state is LOW
    {
        // btn1State is used to avoid sequential toggles
        if (btn1State_l != LOW)
        {

            if (reset_power == 1) //useful when controller gets reset due to powerfailure, it will help to keep the last state
            {

                led1State = led1State; // do not toggle for the first time for physical board switches
                digitalWrite(ledPin1, led1State);
                // Update Button Widget
                Blynk.virtualWrite(V12, led1State);
            }
            else
            {
                // Toggle LED state
                led1State = !led1State;
                UpdateJsonInt(led1State, led2State, led3State);
                digitalWrite(ledPin1, led1State);
                // Update Button Widget
                Blynk.virtualWrite(V12, led1State);
                // save current LedStates to SPIFFS in json
                //WriteConfigJson();
                //UpdateJsonInt(led1State, led2State, led3State);
            }
        }
        btn1State_l = LOW;
    }
    else
    {
        btn1State_l = HIGH;
    }

    if (digitalRead(btnPin1) == HIGH) // Check if the one way switch state is HIGH
    {
        // btn1State is used to avoid sequential toggles
        if (btn1State_h != LOW)
        {

            if (reset_power == 1)
            {

                led1State = led1State;
                digitalWrite(ledPin1, led1State);
                // Update Button Widget
                Blynk.virtualWrite(V12, led1State);
            }
            else
            {
                // Toggle LED state
                led1State = !led1State;
                UpdateJsonInt(led1State, led2State, led3State);
                digitalWrite(ledPin1, led1State);
                // Update Button Widget
                Blynk.virtualWrite(V12, led1State);
                // save current LedStates to SPIFFS in json
                //WriteConfigJson();
                //UpdateJsonInt(led1State, led2State, led3State);
            }
        }
        btn1State_h = LOW;
    }
    else
    {
        btn1State_h = HIGH;
    }

    if (digitalRead(btnPin2) == LOW)
    {
        // btnState is used to avoid sequential toggles
        if (btn2State_l != LOW)
        {

            if (reset_power == 1)
            {

                led2State = led2State;
                digitalWrite(ledPin2, led2State);
                // Update Button Widget
                Blynk.virtualWrite(V13, led2State);
            }
            else
            {
                // Toggle LED state
                led2State = !led2State;
                UpdateJsonInt(led1State, led2State, led3State);
                digitalWrite(ledPin2, led2State);
                // Update Button Widget
                Blynk.virtualWrite(V13, led2State);
                // save current LedStates to SPIFFS in json
                //WriteConfigJson();
                //UpdateJsonInt(led1State, led2State, led3State);
            }
            btn2State_l = LOW;
        }
    }
    else
    {
        btn2State_l = HIGH;
    }

    if (digitalRead(btnPin2) == HIGH)
    {
        // btnState is used to avoid sequential toggles
        if (btn2State_h != LOW)
        {

            if (reset_power == 1)
            {

                led2State = led2State;
                digitalWrite(ledPin2, led2State);
                // Update Button Widget
                Blynk.virtualWrite(V13, led2State);
            }
            else
            {
                // Toggle LED state
                led2State = !led2State;
                UpdateJsonInt(led1State, led2State, led3State);
                digitalWrite(ledPin2, led2State);
                // Update Button Widget
                Blynk.virtualWrite(V13, led2State);
                // save current LedStates to SPIFFS in json
                //WriteConfigJson();
                // UpdateJsonInt(led1State, led2State, led3State);
            }
        }
        btn2State_h = LOW;
    }
    else
    {
        btn2State_h = HIGH;
    }

    if (digitalRead(btnPin3) == LOW)
    {
        // btnState is used to avoid sequential toggles
        if (btn3State_l != LOW)
        {

            if (reset_power == 1)
            {

                led3State = led3State;
                digitalWrite(ledPin3, led3State);
                // Update Button Widget
                Blynk.virtualWrite(V14, led3State);
            }
            else
            {
                // Toggle LED state
                led3State = !led3State;
                UpdateJsonInt(led1State, led2State, led3State);
                digitalWrite(ledPin3, led3State);
                // Update Button Widget
                Blynk.virtualWrite(V14, led3State);
                // save current LedStates to SPIFFS in json
                //WriteConfigJson();
                //UpdateJsonInt(led1State, led2State, led3State);
            }
        }
        btn3State_l = LOW;
    }
    else
    {
        btn3State_l = HIGH;
    }

    if (digitalRead(btnPin3) == HIGH)
    {
        // btnState is used to avoid sequential toggles
        if (btn3State_h != LOW)
        {

            if (reset_power == 1)
            {

                led3State = led3State;
                digitalWrite(ledPin3, led3State);
                // Update Button Widget
                Blynk.virtualWrite(V14, led3State);
            }
            else
            {
                // Toggle LED state
                led3State = !led3State;
                UpdateJsonInt(led1State, led2State, led3State);
                digitalWrite(ledPin3, led3State);
                // Update Button Widget
                Blynk.virtualWrite(V14, led3State);
                // save current LedStates to SPIFFS in json
                //WriteConfigJson();
                //UpdateJsonInt(led1State, led2State, led3State);
            }
        }
        btn3State_h = LOW;
    }
    else
    {
        btn3State_h = HIGH;
    }

    // when the light comes up after a power cut, below variable(reset_power) helps to keep the last Ledstates intact without toggling, so this variable is one time use only when the MCU boots up
    reset_power = 0;
}

/*
void hw_wdt_disable(){
  *((volatile uint32_t*) 0x60000900) &= ~(1); // Hardware WDT OFF
}
*/

void setup()
{

    // Debug console
    //Serial.begin(9600);
    //Serial.swap();
    //Serial.println();
    //WiFi.hostname("BedRoom");
    ESP.wdtDisable(); // disable software WDT reset due to inactivity or function taking longer time than usual
    //hw_wdt_disable();
    delay(10);
    ESP.wdtEnable(WDTO_2S); // feed WDT timer or reset wdt timer every 2 second to avoid software reset
    //Software WDT = 3.2 seconds (cannot be changed)
    //Hardware WDT = 8.2 seconds (cannot be changed)

    //ESP.wdtDisable(); // It disables software watchdog timer(wdt) reset

    EEPROM.begin(32); // for EEPROM functioning
    delay(100);

    // by default if SPIFFS mount fails then it auto format SPIFFS data, below code will not allow auto format
    fs::SPIFFSConfig cfg;
    cfg.setAutoFormat(false);
    SPIFFS.setConfig(cfg);

    // call WifimangerSetupCall() to set load pinmodes and retrieve the last saved credentials from the SPIFFS
    WifimangerSetupCall();

    // setup pinmodes for one way switch buttons, input_pullup shows the default state is HIGH
    pinMode(btnPin1, INPUT_PULLUP);
    pinMode(btnPin2, INPUT_PULLUP);
    pinMode(btnPin3, INPUT_PULLUP);
    pinMode(Magnetic_Sensor, INPUT_PULLUP);

    // Providing LOW or GROUND to all the one way switches via Tx Pin
    // We could have directly connect the switches to ground but on boot up few pins needs to be high
    // So if we directly connect the switches to ground then it may result in boot up failure

    pinMode(Tx_Pin_GND, OUTPUT);
    digitalWrite(Tx_Pin_GND, LOW); // keep it LOW as ground pin

    pinMode(resetbutton, OUTPUT);
    digitalWrite(resetbutton, HIGH);

    if (digitalRead(Magnetic_Sensor) == HIGH)
    {
        Door_opened = true;
        Door_closed = false;
        //Blynk.notify("{DEVICE_NAME}'s door opened !!");
    }
    else
    {
        Door_opened = false;
        Door_closed = true;
        //Blynk.notify("{DEVICE_NAME}'s door closed !!");
    }

    // Checking physical one way board switch  status every 300 ms, toggle the Ledstate if switch is toggled
    timer.setInterval(300L, checkPhysicalButton);

    // Checking resetbutton every 3 second, if pushed for 3 or more second it will trigger OnDemandConfig portal
    timer.setInterval(3000L, OnDemandConfigPortalCheck);

    // checking Blynk connectivity in every 50 seconds, taking action based on the result
    timer.setInterval(50000L, CheckBlynkConnectivity);

    // checking local wifi connection on every 20 second, if not connected then try to reconnect
    timer.setInterval(20000L, WifiReconnect);

    // checking [indicator] variable status on every 500 ms, if (indicator!=true) then toggle the BUILTIN_BOARD_LED,
    // basically if the MCU is not connected to Blynk then the BuiltIn LED will keep on blinking with 500 ms delay
    timer.setInterval(500L, LEDtoggle);

    // cheking wifi status every 15 second, if not connected than indicator starts blinking and if connected then result sets to true for blynk.run() operation
    timer.setInterval(15000L, CheckWifiStatusIndicator);

    // Check every 60 second if the ESP needs to be rebooted due to wifi disconnection for an hour // 4/03/2020 : commenting this out
    //timer.setInterval(60000L, CheckScanReboot);

    //check every 5 Min. if the previously connected SSID is available in the network scan  // 4/03/2020 : commenting this out
    //timer.setInterval(300000L, ScanSSIDexist);

    //this timer function will trigger CheckBlynkConnectivity() function once in start up after 15 second.... this 15 second is for intial wifi connection
    timer.setTimeout(15000L, CheckBlynkConnectivity);

    // send firmware update notification every 6 hour if update available
    timer2.setInterval(21600000L, HttpOtaUpdateNotify);

    //checking door position every 3 second if the blynk is already connected
    //Blynk notification service is limited to one push notification every 5 second
    timer2.setInterval(3000L, Door_Alert);

    //attachInterrupt(digitalPinToInterrupt(Magnetic_Sensor), Door_Alert, HIGH);

    setSyncInterval(10 * 60); // RTC sync to blynk interval in seconds (10 minutes)

    ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot

    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(host_name);

    // No authentication by default
    ArduinoOTA.setPassword(OTA_Password);

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        { // U_SPIFFS
            type = "filesystem";
        }
        // Turning all the loads off, meanwhile physical and digital switches will not be accessible
        digitalWrite(BUILTIN_BOARD_LED, HIGH);
        digitalWrite(ledPin1, HIGH);
        digitalWrite(ledPin2, HIGH);
        digitalWrite(ledPin3, HIGH);
        //digitalWrite(ledPin4, HIGH);

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        // Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        //Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        //Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            //Serial.println("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            // Serial.println("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            //Serial.println("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            //Serial.println("Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            //Serial.println("End Failed");
        }
        // restoring the last load states
        digitalWrite(ledPin1, led1State);
        digitalWrite(ledPin2, led2State);
        digitalWrite(ledPin3, led3State);
        //digitalWrite(ledPin4, led4State);
    });
    ArduinoOTA.begin();
    //Serial.println("Ready");
    // Serial.print("IP address: ");
    //Serial.println(WiFi.localIP());

    // WiFi.setAutoConnect();
}

void loop()
{
    if (result == false)
    {
        if (WifiCheckOnce == true)
        {
            WifiReconnect(); // call this once in the main loop
            WifiCheckOnce = false;
        }
    }
    ESP.wdtFeed(); // feed ESP for its internal processing or it will reboot
    //Blynk.run() will only run when Blynk is connected, if not then it will consume processing power and impacts physical switch operation
    if (result == true && indicator == true)
    {

        Blynk.run(); // only process Blyk.run() function if we are connected to Blynk
    }
    //it is simpleTimer provided by Blynk with some modification
    timer.run();
    timer2.run(); // each timer object can only handle 10 timers, so had to create timer2 object for 11th timer function call

    // Below function is responsible for OTA
    if (HTTP_OTA_UPDATE == HIGH) // enable arduino OTA when HTTP update button is desable in blynk app, if there is internet disconnection then by default arduino OTA will get enabled
    {
        ArduinoOTA.handle();
    }
}
