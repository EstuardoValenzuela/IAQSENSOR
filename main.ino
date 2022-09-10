 
/*
  -------------------------
  Title: BME680 AIR QUALITY + UI + Firebase + Wifi
  Autor: Estuardo Valenzuela Girón
  Facultad de Ingenieria - UNIS
  Proyecto de Tesis
  -------------------------
  
*/
#if !defined( ESP32 )
  #error This code is intended to run on the ESP32 platform! Please check your Tools->Board setting.
#endif

#include "ESP32TimerInterrupt.h"
#include "SPI.h"
#include "TFT_eSPI.h"
#include <Wire.h>
#include "bsec.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include "Free_Fonts.h"
#include "icons.h"
#include <Firebase_ESP_Client.h>
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"


#define TIMER_INTERRUPT_DEBUG      1

#define   IAQ_BACKGROUND   tft.color565(171, 211, 245)
#define  TEMP_BACKGROUND   tft.color565(242, 191, 44)
#define   HUM_BACKGROUND   tft.color565(125, 221, 235)
#define   CO2_BACKGROUND   tft.color565(0, 128, 64)
#define PRESS_BACKGROUND   tft.color565(128, 128, 128)
#define   ACT_BACKGROUND   tft.color565(51, 153, 255)
#define     IAQEXCELLENT   tft.color565(6, 142, 23)
#define          IAQGOOD   tft.color565(0, 128, 0)
#define       IAQAVERAGE   tft.color565(255, 128, 0)
#define           IAQBAD   tft.color565(156, 10, 61)
#define        IAQSEVERE   tft.color565(128,  0, 64)
#define        IAQDANGER   tft.color565(255,  0, 0)
#define         TFT_GREY   0x5AEB

#define  KEYBOARD_BG       tft.color565(153, 217, 234)
#define  ACTIONB_BG        tft.color565(195, 195, 195)
#define  SPACE_BG          tft.color565(143, 167, 248)
#define  DEL_BG            tft.color565(248, 7, 7)
#define  TEXT_COLORM       tft.color565(0, 0, 160)
#define  BTN_COLORM        tft.color565(141, 217, 143)

#define TIMER1_INTERVAL_MS        20
#define DEBOUNCING_INTERVAL_MS    100
#define LONG_PRESS_INTERVAL_MS    5000
#define BUZZER_PIN 32 // ESP32 GIOP21 pin connected to Buzzer's pin
#define SCREEN_PIN 33 // ESP32 GIOP21 pin connected to Buzzer's pin

//#define API_KEY "AIzaSyBqCO97KXcNagdqSoKuaEijeXQbCv-kU54"
//#define DATABASE_URL "https://small-talk-fmek.firebaseio.com/" 

// Init ESP32 timer 1
ESP32Timer ITimer1(1);
//Init WebServor on Port 80
WebServer server(80);
//Init Screen
TFT_eSPI tft = TFT_eSPI();
//Init preferences
Preferences preferences;
//Init Sensor
Bsec iaqSensor;
//Firebase configuration
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

//Settings BTN
unsigned int SWPin = 13;
unsigned long sendDataPrevMillis = 0;
int count = 0;
//Firebase Flag
bool signupOK = false;


//==================== Struct Variables ========================
struct data_proccess {
  int currentIAQ;
  int currentTemp;
  int currentHum;
  int currentCo2;
  int currentPress;
};
//----------------------------------------------------------------
void checkIaqSensorStatus(void);
//======================================== GLOBAL VARIABLES =====================================
//=============== INTERRUPT VARIABLES ================
volatile bool SWPressed     = false;
volatile bool SWLongPressed = false;
volatile uint64_t lastSWPressedTime     = 0;
volatile uint64_t lastSWLongPressedTime = 0;

volatile bool lastSWPressedNoted     = true;
volatile bool lastSWLongPressedNoted = true;
//-----------------------------------------------------

String output; //Output sensor
uint8_t status_iaq; //Variable to indicate the frecuency of the buzzer
uint8_t time_out = 0; //Timeout to pass the test of the wifi 
uint16_t w = tft.width();
uint8_t keyboard = 0;   //Variable to indicate what keyboard draw
uint16_t start_ypoint = 120;  //Start coordinate Y of the keyboard.
//============BME680 Variables ===========================
//Variables to Store the data of the sensor
float temperature, humidity, pressure, IAQ, carbon, VOC;
const char* IAQsts;     //Save the IAQ TEXT
//Variables to avoid reploy data
String old_temp;        
String old_hum;
String old_press;
String old_co2;
String old_iaq;
String old_status_iaq;
//LOWER AND HIGGEST VALUES
int lower_iaq = 999;
int higger_iaq = 0;

int lower_temp = 999;
int higger_temp = 0;
int lower_hum = 999;
int higger_hum = 0;

int lower_co2 = 999;
int higger_co2 = 0;

int lower_press = 999;
int higger_press = 0;
//------------------------------------------------------------
//========== UI Variables ============================
//Config variables
String text;              
uint8_t optionToSave = 0;
bool typeKeyboard = true;
bool transKeyboard = false;
//Store Variables
String mainWifiSSID, mainWifiPSW, mainNameDevice, mainFirebaseKey, mainFirebaseURL;    
//Flag Status Wifi 
bool changeSSID, changeSSIDPSW;
//FLAGS
bool sensorHeating, loadTemplate,
     plotDataValues, plotIAQstatus,
     plotValues, plotMenu, drawKeyboard,
     printKeysFlag, come_to_home, 
     statusScreen = true;
//---------------------------------------------
//==================== FIREBASE VARIABLES ===================================
bool flag_firebase = false; //Send or not
String key_parameter; //Parameter to the PATH
int value_sensor;    //Value to send
//--------------------------------------------------------------------------

//============================= MAIN Functions=============================
void draw_icon(const uint16_t* icon, uint16_t w, uint16_t h, uint16_t x, uint16_t y){
  int row, col, buffidx=0;
  for (row=0; row<w; row++) { // For each scanline...
    for (col=0; col<h; col++) { // For each pixel...
      //To read from Flash Memory, pgm_read_XXX is required.
      //Since image is stored as uint16_t, pgm_read_word is used as it uses 16bit address
      tft.drawPixel(col+x, row+y, pgm_read_word(icon + buffidx));
      buffidx++;
    } // end pixel
  
  }
}


void IRAM_ATTR lastSWPressedMS()
{
  lastSWPressedTime   = millis();
  lastSWPressedNoted  = false;
}

void IRAM_ATTR lastSWLongPressedMS()
{
  lastSWLongPressedTime   = millis();
  lastSWLongPressedNoted  = false;
}

// With core v2.0.0+, you can't use Serial.print/println in ISR or crash.
// and you can't use float calculation inside ISR
// Only OK in core v1.0.6-
bool IRAM_ATTR TimerHandler1(void * timerNo)
{ 
  static unsigned int debounceCountSWPressed  = 0;
  static unsigned int debounceCountSWReleased = 0;

  if ( (!digitalRead(SWPin)) )
  {
    // Start debouncing counting debounceCountSWPressed and clear debounceCountSWReleased
    debounceCountSWReleased = 0;

    if (++debounceCountSWPressed >= DEBOUNCING_INTERVAL_MS / TIMER1_INTERVAL_MS)
    {
      // Call and flag SWPressed
      if (!SWPressed)
      {
        SWPressed = true;
        // Do something for SWPressed here in ISR
        // But it's better to use outside software timer to do your job instead of inside ISR
        //Your_Response_To_Press();
        lastSWPressedMS();
      }

      if (debounceCountSWPressed >= LONG_PRESS_INTERVAL_MS / TIMER1_INTERVAL_MS)
      {
        // Call and flag SWLongPressed
        if (!SWLongPressed)
        {
          SWLongPressed = true;
          // Do something for SWLongPressed here in ISR
          // But it's better to use outside software timer to do your job instead of inside ISR
          //Your_Response_To_Long_Press();
          lastSWLongPressedMS();
        }
      }
    }
  }
  else
  {
    // Start debouncing counting debounceCountSWReleased and clear debounceCountSWPressed
    if ( SWPressed && (++debounceCountSWReleased >= DEBOUNCING_INTERVAL_MS / TIMER1_INTERVAL_MS))
    {
      SWPressed     = false;
      SWLongPressed = false;

      // Do something for !SWPressed here in ISR
      // But it's better to use outside software timer to do your job instead of inside ISR
      //Your_Response_To_Release();

      // Call and flag SWPressed
      debounceCountSWPressed = 0;
    }
  }
  return true;
}



void send_data_firebase(){
  String preassurePathPrepare = mainNameDevice+"/PREASSURE";
  
//  const char preassurePath=preassurePathPrepare.c_str();
  String humidityPath= mainNameDevice+"/HUMIDITY";
  String temperaturePath= mainNameDevice+"/TEMPERATURE";
  String iaqPath= mainNameDevice+"/IAQ";
  String co2Path= mainNameDevice+"/CO2";
   
  if(Firebase.RTDB.setInt(&fbdo, preassurePathPrepare, int(iaqSensor.pressure / 100.0))){
    Serial.println("PASSED");
    Serial.println("PATH: "+fbdo.dataPath());
    Serial.println("TYPE: "+fbdo.dataType());
    delay(100);
  }else{
    Serial.println("FAILED");
    Serial.println("REASON: "+ fbdo.errorReason());
  }
  if(Firebase.RTDB.setInt(&fbdo, humidityPath, int(iaqSensor.humidity))){
    Serial.println("PASSED");
    Serial.println("PATH: "+fbdo.dataPath());
    Serial.println("TYPE: "+fbdo.dataType());
    delay(100);
  }else{
    Serial.println("FAILED");
    Serial.println("REASON: "+ fbdo.errorReason());
  }
    if(Firebase.RTDB.setInt(&fbdo, temperaturePath, int(iaqSensor.temperature))){
    Serial.println("PASSED");
    Serial.println("PATH: "+fbdo.dataPath());
    Serial.println("TYPE: "+fbdo.dataType());
    delay(100);
  }else{
    Serial.println("FAILED");
    Serial.println("REASON: "+ fbdo.errorReason());
  }
  if(Firebase.RTDB.setInt(&fbdo, iaqPath, int(iaqSensor.staticIaq))){
    Serial.println("PASSED");
    Serial.println("PATH: "+fbdo.dataPath());
    Serial.println("TYPE: "+fbdo.dataType());
    delay(100);
  }else{
    Serial.println("FAILED");
    Serial.println("REASON: "+ fbdo.errorReason());
  }
  if(Firebase.RTDB.setInt(&fbdo, co2Path, int(iaqSensor.co2Equivalent))){
    Serial.println("PASSED");
    Serial.println("PATH: "+fbdo.dataPath());
    Serial.println("TYPE: "+fbdo.dataType());
    delay(100);
  }else{
    Serial.println("FAILED");
    Serial.println("REASON: "+ fbdo.errorReason());
  } 
  delay(200);
}

//======================================= SECOND TASK ==========================================================
void alarm_system( void * parameter ) {
   sensorHeating = true;
   pinMode(SCREEN_PIN, OUTPUT);
   digitalWrite(SCREEN_PIN, HIGH);
   init_system();
   //====================Atacch interrupt button in core 0 ======================
   pinMode(SWPin, INPUT_PULLUP);
     // Interval in microsecs
    if (ITimer1.attachInterruptInterval(TIMER1_INTERVAL_MS * 1000, TimerHandler1))
    {
        Serial.print(F("Starting  ITimer1 OK, millis() = ")); Serial.println(millis());
    }
    else{
        Serial.println(F("Can't set ITimer1. Select another freq. or timer"));
    }
  //=============================================================================
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    for (;;) {
        if (!lastSWPressedNoted)
        {
            lastSWPressedNoted = true;
            Serial.print(F("lastSWPressed @ millis() = ")); Serial.println(lastSWPressedTime);
            plotDataValues = false;
            loadTemplate = false;
            plotIAQstatus = false;
            plotMenu = true;
            draw_menu();
        }
        if (!lastSWLongPressedNoted)
        {
            lastSWLongPressedNoted = true;
            Serial.print(F("lastSWLongPressed @ millis() = ")); Serial.println(lastSWLongPressedTime);
        }
        if(plotDataValues == true && signupOK == true){
             send_data_firebase();
        }
        switch (status_iaq)
            {
                case 1:
                    digitalWrite(BUZZER_PIN, HIGH);
                    delay(50);
                    digitalWrite(BUZZER_PIN, LOW);
                    delay(50);
                    break;
                case 2:
                    digitalWrite(BUZZER_PIN, HIGH);
                    break;
                default:
                    break;
            }
    }
}

//====================================== FIRST TASK ============================================================
void sensor_measure_1(){
    for(;;){
            server.handleClient();
            unsigned long time_trigger = millis();
            if (iaqSensor.run()) { // If new data is available
                output = String(time_trigger);
                output += ", " + String(iaqSensor.rawTemperature);
                output += ", " + String(iaqSensor.pressure);
                output += ", " + String(iaqSensor.rawHumidity);
                output += ", " + String(iaqSensor.gasResistance);
                output += ", " + String(iaqSensor.iaq);
                output += ", " + String(iaqSensor.iaqAccuracy);
                output += ", " + String(iaqSensor.temperature);
                output += ", " + String(iaqSensor.humidity);
                output += ", " + String(iaqSensor.staticIaq);
                output += ", " + String(iaqSensor.co2Equivalent);
                output += ", " + String(iaqSensor.breathVocEquivalent);
                Serial.println(output);
            } 
            else {
                 checkIaqSensorStatus();
            }

            if ((iaqSensor.staticIaq > 0)  && (iaqSensor.staticIaq  <= 50)) {
                IAQsts = "Excellent";
                Serial.print("IAQ: Excellent");
                status_iaq = 0;
            }
            if ((iaqSensor.staticIaq > 51)  && (iaqSensor.staticIaq  <= 100)) {
                IAQsts = "Good";
                Serial.print("IAQ: Good");
                status_iaq = 0;
            }
            if ((iaqSensor.staticIaq > 101)  && (iaqSensor.staticIaq  <= 150)) {
                IAQsts = "Average";
                Serial.print("IAQ: Average");
                status_iaq = 0;
            }
            if ((iaqSensor.staticIaq > 151)  && (iaqSensor.staticIaq  <= 200)) {
                IAQsts = "Bad";
                Serial.print("IAQ: Bad");
                status_iaq = 0;
            }
            if ((iaqSensor.staticIaq > 201)  && (iaqSensor.staticIaq  <= 300)) {
                IAQsts = "Severe";
                Serial.print("IAQ: Severe");
                status_iaq = 0;
            }
            if ((iaqSensor.staticIaq > 301)  && (iaqSensor.staticIaq  <= 500)) {
                IAQsts = "Critical";
                Serial.print("IAQ: Critical");
                status_iaq = 1;
            }
            if ((iaqSensor.staticIaq > 500)){
                IAQsts = "Danger";
                Serial.print("IAQ: Danger");
                status_iaq = 2;
            }
            plot_status_iaq(IAQsts, plotIAQstatus);
            Serial.println();

            if (sensorHeating == false && plotValues == true && plotMenu == false && drawKeyboard == false && printKeysFlag == false) {
                Serial.println("Ready to plot");
                plot_data(int(iaqSensor.temperature), int(iaqSensor.humidity), int(iaqSensor.pressure / 100.0), int(iaqSensor.co2Equivalent), int(iaqSensor.staticIaq));
            }
             delay(500);
            }
}

void sensor_measure( void * parameter ) {
  getData();
  if (mainWifiSSID == "" && mainWifiPSW == ""){
      Serial.println("No settings to stablish connection");
      Serial.print("SSID: ");
      Serial.println(mainWifiSSID.c_str());
      Serial.print("PASSWORD: ");
      Serial.println(mainWifiPSW.c_str());
    }
    else {
          //============== Wifi Connection ===============================
      //Connect to your local wi-fi network
      WiFi.begin(mainWifiSSID.c_str(), mainWifiPSW.c_str());
      //check wi-fi is connected to wi-fi network
      while (WiFi.status() != WL_CONNECTED) {
              delay(1000);
              Serial.print(".");
              time_out++;
              if (time_out == 15){
                  break;
              }
      }
      Serial.println("");
      Serial.println("WiFi connected..!");
      Serial.print("Got IP: ");  Serial.println(WiFi.localIP());
      //--------------------------------------------------------------
      //=============== Inicialize Web Server =======================
      server.on("/", handle_OnConnect);
      server.onNotFound(handle_NotFound);
      server.begin();
      Serial.println("HTTP server started");
      //-------------------------------------------------------------
      //=============== Login Firebase ==========================
      loginFirebase(mainFirebaseURL, mainFirebaseKey, false);
    }
   for (;;) {
      sensor_measure_1(); //Loop sensor_measure_1()
    }
}

void init_system(){
  if (sensorHeating == true)
  {
    tft.fillScreen(TFT_BLACK);
    draw_button(icon_info, tft.color565(237, 28, 36), 70, 70, 30, 85, "Sensor is", "heating. ", "Please wait");
    tft.setTextColor(TFT_GREEN,TFT_BLUE);
    contar(7);
   // delay(5000);
    sensorHeating = false;
    loadTemplate = true;
    template_load();
    }
  }

void contar(int minutos) { 
      int progress = 0;
      int aux_delay = 0;
      aux_delay = ((minutos * 60)/195) *1000;
      for (uint8_t i = 0; i <195 ; i++)
      {
        tft.fillRect(7+10, 212, i, 26, TFT_GREEN);
        delay(aux_delay);
      }
}

void onBuzzer(){
  digitalWrite(BUZZER_PIN, HIGH);
  delay(50);
  digitalWrite(BUZZER_PIN, LOW);
}

void setup()
{
    delay(500);
    Serial.begin(115200);
    tft.begin();
    tft.setRotation(0);
    uint16_t calData[5] = { 297, 3583, 426, 3470, 6 };
    tft.setTouch(calData);
    tft.fillScreen(TFT_BLACK);
    status_iaq = 0;
    Wire.begin();
    //================= Run sensor ===============================
    iaqSensor.begin(BME680_I2C_ADDR_SECONDARY, Wire);
    output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
    Serial.println(output);
    checkIaqSensorStatus();
    bsec_virtual_sensor_t sensorList[10] = {
                BSEC_OUTPUT_RAW_TEMPERATURE,
                BSEC_OUTPUT_RAW_PRESSURE,
                BSEC_OUTPUT_RAW_HUMIDITY,
                BSEC_OUTPUT_RAW_GAS,
                BSEC_OUTPUT_IAQ,
                BSEC_OUTPUT_STATIC_IAQ,
                BSEC_OUTPUT_CO2_EQUIVALENT,
                BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
                BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
                BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };
    iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);
    tft.setTextColor(TFT_GREEN);
    checkIaqSensorStatus();
    //----------------------------------------------------------------------------------
    //================================= Show Board settings =================================
    Serial.print(F("\nStarting SwitchDebounce on ")); Serial.println(ARDUINO_BOARD);
    Serial.println(ESP32_TIMER_INTERRUPT_VERSION);
    Serial.print(F("CPU Frequency = ")); Serial.print(F_CPU / 1000000); Serial.println(F(" MHz"));
    //====================================== Create Thread task ============================================
    xTaskCreatePinnedToCore(sensor_measure,   "primaryTask", 40000, NULL, 0, NULL, 0);
    delay(500);  // needed to start-up task
    xTaskCreatePinnedToCore(  alarm_system, "secondaryTask", 40000, NULL, 1, NULL, 1);
    delay(500);
    //------------------------------------------------------------------------------------------------------
}

void loop()
{

}

//=============================================== Auxiliary Functions ========================================================
// Helper function definitions
void checkIaqSensorStatus(void){
  if (iaqSensor.status != BSEC_OK) {
    if (iaqSensor.status < BSEC_OK) {
      output = "BSEC error code : " + String(iaqSensor.status);
      Serial.println(output);
    } else {
      output = "BSEC warning code : " + String(iaqSensor.status);
      Serial.println(output);
    }
  }
 
  if (iaqSensor.bme680Status != BME680_OK) {
    if (iaqSensor.bme680Status < BME680_OK) {
      output = "BME680 error code : " + String(iaqSensor.bme680Status);
      Serial.println(output);
    } else {
      output = "BME680 warning code : " + String(iaqSensor.bme680Status);
      Serial.println(output);
    }
  }
}

void handle_OnConnect() {
  temperature = iaqSensor.temperature;
  humidity = iaqSensor.humidity;
  pressure = iaqSensor.pressure / 100;
  IAQ = iaqSensor.staticIaq;
  carbon = iaqSensor.co2Equivalent;
  VOC = iaqSensor.breathVocEquivalent;
  server.send(200, "text/html", SendHTML(temperature, humidity, pressure, IAQ, carbon, VOC, IAQsts));
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

void plot_data(int temp, int hum, int pres, int co2_eq, int iaq_d){
if (plotMenu == false && drawKeyboard == false && printKeysFlag == false)
{
 if (iaq_d > higger_iaq)
  {
        higger_iaq = iaq_d;
        printAndClearL( 180, 42,String(higger_iaq), IAQ_BACKGROUND, 3);
  }
  else if (iaq_d < lower_iaq)
  {
        lower_iaq = iaq_d;
        printAndClearL(180, 62, String(lower_iaq), IAQ_BACKGROUND, 3);
  }
  
  if (temp > higger_temp){
    higger_temp = temp;
    tft.setTextColor(TFT_BLACK, TEMP_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL(25, 125, String(higger_temp), TEMP_BACKGROUND, 3);
  }else if (temp < lower_temp){
    lower_temp = temp;
    tft.setTextColor(TFT_BLACK, TEMP_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL(72, 125, String(lower_temp), TEMP_BACKGROUND, 3);
  }

  if (hum > higger_hum){
    higger_hum = hum;
    tft.setTextColor(TFT_BLACK, HUM_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL(160, 125, String(higger_hum), HUM_BACKGROUND, 2);
  }else if (hum < lower_hum){
    lower_hum = hum;
    tft.setTextColor(TFT_BLACK, HUM_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL(200, 125, String(lower_hum), HUM_BACKGROUND, 2);
  }

  if (co2_eq > higger_co2){
    higger_co2 = co2_eq;
    tft.setTextColor(TFT_BLACK, CO2_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL(3, 205, String(higger_co2), CO2_BACKGROUND, 4);
  }else if (co2_eq < lower_co2){
    lower_co2 = co2_eq;
    tft.setTextColor(TFT_BLACK, CO2_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL(62, 205, String(lower_co2), CO2_BACKGROUND, 4);
  }

  if (pres > higger_press){
    higger_press = pres;
    tft.setTextColor(TFT_BLACK, PRESS_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL( 3+(w/2), 205, String(higger_press), PRESS_BACKGROUND, 4);
  }else if (pres < lower_press){
    lower_press = pres;
    tft.setTextColor(TFT_BLACK, PRESS_BACKGROUND);
    tft.setFreeFont(FF1);
    printAndClearL(62+(w/2), 205, String(lower_press), PRESS_BACKGROUND, 4);
  }

  Serial.println("Im in the function plot_data");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(FS24);

  String s_temp = String(int(temp));
  String s_hum  = String(int(hum));
  String s_pres = String(int(pres));
  s_pres.remove(3);
  String s_c02 = String(int(co2_eq));
  String s_iaq_d = String(int(iaq_d));
  //String data_pres = String(int(pres));
  
  if (old_temp != s_temp ){
    //Temp section
    tft.setTextColor(TFT_BLACK, TEMP_BACKGROUND);
    printAndClear(45, 87, s_temp, TEMP_BACKGROUND, 2);
     flag_firebase = true;
     key_parameter = "TEMPERATURE";
     value_sensor  = int(temp);
     old_temp = s_temp;
  }
  if (old_hum != s_hum ){
    printAndClear(168, 87, s_hum,   HUM_BACKGROUND, 2);
     flag_firebase = true;
     key_parameter = "HUMIDITY";
     value_sensor  = int(hum);
     old_hum = s_hum;
  }

  if (old_press != s_pres ){
    tft.setTextColor(TFT_BLACK, PRESS_BACKGROUND);
    printAndClear(42+(w/2), 145, s_pres, PRESS_BACKGROUND, 4);
     flag_firebase = true;
     key_parameter = "PREASSURE";
     value_sensor  = int(pres);
     old_press = s_pres;
  }
  if (old_co2 != s_c02 ){
    tft.setTextColor(TFT_BLACK, CO2_BACKGROUND);
    printAndClear(42, 145,s_c02,  CO2_BACKGROUND, 4);
    tft.setFreeFont(FF1);
      flag_firebase = true;
      key_parameter = "CO2";
      value_sensor  = int(co2_eq);
      old_co2 = s_c02;
  }
  if(old_iaq != s_iaq_d ){
    printAndClear(102, 45, s_iaq_d, IAQ_BACKGROUND, 3);
        flag_firebase = true;
        key_parameter = "IAQ";
        value_sensor  = int(iaq_d);
        old_iaq = s_iaq_d;
    }

            uint16_t x = 0, y = 0; // To store the touch coordinates
            // Pressed will be set true is there is a valid touch on the screen
            bool pressed = tft.getTouch(&x, &y);
            if (pressed)
            {
              while (true)
              {
              Serial.println("pressed!");
              Serial.println("X = "+String(x)+" Y: "+String(y));
              if (statusScreen == true)
              {
                Serial.println("ON SCREEN");
                digitalWrite(SCREEN_PIN, HIGH);
                statusScreen = false;
                delay(500);
                break;
              }
               if ( x > 185 && x < 240 &&  y > 225 && y < 275){
                      Serial.println("OFF SCREEN");
                      digitalWrite(SCREEN_PIN, LOW);
                      statusScreen = true;
                      delay(500);
                      break;
               }
                    else{
                      break;
                    }
                  break;
                }
              }
           
}

 
}

void plot_status_iaq(String iaq_status, bool flag){
  if (flag == true){
    if (old_status_iaq != iaq_status or come_to_home == true)
    {
        come_to_home = false;
        Serial.println("Plotting IAQ STATUS...");
        tft.fillRect(60, 0, 260, 40, IAQ_BACKGROUND); //Clear 
        tft.fillRect(0,   270, 180, 100, ACT_BACKGROUND);  //Clear current Message
    if(iaq_status == "Excellent"){
        tft.setTextColor(IAQEXCELLENT, IAQ_BACKGROUND);
        tft.setFreeFont(FF18);
        tft.drawString("EXCELLENT", 95, 11); //IAQ text

       // Suggestion section
        tft.setTextColor(TFT_BLACK, ACT_BACKGROUND);
        tft.setFreeFont(FF1);
        tft.drawString("No measures", 30, 280);
        tft.drawString("needed", 55, 294);

    }
    if(iaq_status == "Good"){
      tft.setTextColor(IAQGOOD, IAQ_BACKGROUND);
      tft.setFreeFont(FF19);
      tft.drawString("GOOD", 125, 5); //IAQ text
      //Suggestion section
      tft.setTextColor(TFT_BLACK, ACT_BACKGROUND);
      tft.setFreeFont(FF1);
      tft.drawString("No measures", 30, 280);
      tft.drawString("needed", 55, 294);
      }
    if(iaq_status == "Average"){
      tft.setTextColor(IAQAVERAGE, IAQ_BACKGROUND);
      tft.setFreeFont(FF19);
      tft.drawString("AVERAGE", 68, 5); //IAQ text
      //Suggestion section
       tft.setTextColor(TFT_BLACK, ACT_BACKGROUND);
      tft.setFreeFont(FF1);
      tft.drawString("Ventilation", 27, 280);
      tft.drawString("suggested", 40, 295);
      }

    if(iaq_status == "Bad"){
        tft.setTextColor(TFT_RED, IAQ_BACKGROUND);
        tft.setFreeFont(FF19);
        tft.drawString("BAD", 160, 5); //IAQ text
        //Suggestion section
        tft.setTextColor(TFT_BLACK, ACT_BACKGROUND);
        tft.setFreeFont(FF1);
        tft.drawString("Increase", 42, 280);
        tft.drawString("ventilation", 27, 295);
      }
      
    if(iaq_status == "Severe"){
        tft.setTextColor(IAQSEVERE, IAQ_BACKGROUND);
        tft.setFreeFont(FF19);
        tft.drawString("SEVERE", 93, 5); //IAQ text
              //Suggestion section
      tft.setTextColor(TFT_BLACK, ACT_BACKGROUND);
      tft.setFreeFont(FF1);
      tft.drawString("Optimize", 42, 280);
      tft.drawString("ventilation", 27, 295);
      }
  
    if(iaq_status == "Critical"){
        tft.setTextColor(IAQDANGER, IAQ_BACKGROUND);
        tft.setFreeFont(FF19);
        tft.drawString("CRITICAL", 78, 5); //IAQ text
              //Suggestion section
      tft.setTextColor(TFT_BLACK, ACT_BACKGROUND);
      tft.setFreeFont(FF1);
      tft.drawString("Maximize", 42, 280);
      tft.drawString("ventilation", 27, 295);
      }

    if(iaq_status == "Danger"){
        tft.setTextColor(IAQDANGER, IAQ_BACKGROUND);
        tft.setFreeFont(FF19);
        tft.drawString("DANGER", 88, 5); //IAQ text
              //Suggestion section
      tft.setTextColor(TFT_BLACK, ACT_BACKGROUND);
      tft.setFreeFont(FF1);
      tft.drawString("Avoid presence", 14, 280);
      tft.drawString("in the room", 29, 295);
      }

      old_status_iaq = iaq_status;
    }
    
  }

}

//###############################################################################################################################
void dataSavePrint(){
    switch (optionToSave)
    {
    case 1:
        printwrap(mainWifiSSID, 8, 10);
        text = mainWifiSSID;
        break;
    case 2:
        printwrap(" ", 8, 10);
        break;
    case 3:
        printwrap(mainNameDevice, 8, 10);    
        text = mainNameDevice;
        break;
    case 4:
        printwrap(mainFirebaseURL, 8, 10);
        text = mainFirebaseURL;
        break;
    case 5:
        printwrap(" ", 8, 10);
      //  text = mainFirebaseKey; //DEVELOPMENT ONLY!
        break;

    default:
        break;
    }
        
}

//============================================== Main Functions ==================================================================
String SendHTML(float temperature, float humidity, float pressure, float IAQ, float carbon, float VOC, const char* IAQsts) {
        String html = "<!DOCTYPE html>";
        html += "<html>";
        html += "<head>";
        html += "<title>Sensor Status</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.7.2/css/all.min.css'>";
        html += "<link rel='stylesheet' type='text/css' href='styles.css'>";
        html += "<style>";
        html += "body { background-color: #fff; font-family: sans-serif; color: #333333; font: 12px Helvetica, sans-serif box-sizing: border-box;}";
        html += "#page { margin: 18px; background-color: #fff;}";
        html += ".container { height: inherit; padding-bottom: 18px;}";
        html += ".header { padding: 18px;}";
        html += ".header h1 { padding-bottom: 0.3em; color: #f4a201; font-size: 25px; font-weight: bold; font-family: Garmond, 'sans-serif'; text-align: center;}";
        html += "h2 { padding-bottom: 0.2em; border-bottom: 1px solid #eee; margin: 2px; text-align: center;}";
        html += ".box-full { padding: 18px; border 1px solid #ddd; border-radius: 1em 1em 1em 1em; box-shadow: 1px 7px 7px 1px rgba(0,0,0,0.4); background: #fff; margin: 18px; width: 300px;}";
        html += "@media (max-width: 494px) { #page { width: inherit; margin: 5px auto; } #content { padding: 1px;} .box-full { margin: 8px 8px 12px 8px; padding: 10px; width: inherit;; float: none; } }";
        html += "@media (min-width: 494px) and (max-width: 980px) { #page { width: 465px; margin 0 auto; } .box-full { width: 380px; } }";
        html += "@media (min-width: 980px) { #page { width: 930px; margin: auto; } }";
        html += ".sensor { margin: 10px 0px; font-size: 2.5rem;}";
        html += ".sensor-labels { font-size: 1rem; vertical-align: middle; padding-bottom: 15px;}";
        html += ".units { font-size: 1.2rem;}";
        html += "hr { height: 1px; color: #eee; background-color: #eee; border: none;}";
        html += "</style>";

        //Ajax Code Start
        html += "<script>\n";
        html += "setInterval(loadDoc,1000);\n";
        html += "function loadDoc() {\n";
        html += "var xhttp = new XMLHttpRequest();\n";
        html += "xhttp.onreadystatechange = function() {\n";
        html += "if (this.readyState == 4 && this.status == 200) {\n";
        html += "document.body.innerHTML =this.responseText}\n";
        html += "};\n";
        html += "xhttp.open(\"GET\", \"/\", true);\n";
        html += "xhttp.send();\n";
        html += "}\n";
        html += "</script>\n";
        //Ajax Code END

        html += "</head>";
        html += "<body>";
        html += "<div id='page'>";
        html += "<div class='header'>";
        html += "<h1>Monitoring System: "+mainNameDevice+"</h1>";
        html += "</div>";
        html += "<div id='content' align='center'>";
        html += "<div class='box-full' align='left'>";
        html += "<h2>";
        html += "IAQ Status: ";
        html += IAQsts;
        html += "</h2>";
        html += "<div class='sensors-container'>";

        //For Temperature
        html += "<div class='sensors'>";
        html += "<p class='sensor'>";
        html += "<i class='fas fa-thermometer-half' style='color:#0275d8'></i>";
        html += "<span class='sensor-labels'> Temperature </span>";
        html += temperature;
        html += "<sup class='units'>°C</sup>";
        html += "</p>";
        html += "<hr>";
        html += "</div>";

        //For Humidity
        html += "<p class='sensor'>";
        html += "<i class='fas fa-tint' style='color:#0275d8'></i>";
        html += "<span class='sensor-labels'> Humidity </span>";
        html += humidity;
        html += "<sup class='units'>%</sup>";
        html += "</p>";
        html += "<hr>";

        //For Pressure
        html += "<p class='sensor'>";
        html += "<i class='fas fa-tachometer-alt' style='color:#ff0040'></i>";
        html += "<span class='sensor-labels'> Pressure </span>";
        html += pressure;
        html += "<sup class='units'>hPa</sup>";
        html += "</p>";
        html += "<hr>";

        //For VOC IAQ
        html += "<div class='sensors'>";
        html += "<p class='sensor'>";
        html += "<i class='fab fa-cloudversify' style='color:#483d8b'></i>";
        html += "<span class='sensor-labels'> IAQ </span>";
        html += IAQ;
        html += "<sup class='units'>PPM</sup>";
        html += "</p>";
        html += "<hr>";

        //For C02 Equivalent
        html += "<p class='sensor'>";
        html += "<i class='fas fa-smog' style='color:#35b22d'></i>";
        html += "<span class='sensor-labels'> Co2 Eq. </span>";
        html += carbon;
        html += "<sup class='units'>PPM</sup>";
        html += "</p>";
        html += "<hr>";

        //For Breath VOC
        html += "<p class='sensor'>";
        html += "<i class='fas fa-wind' style='color:#0275d8'></i>";
        html += "<span class='sensor-labels'> Breath VOC </span>";
        html += VOC;
        html += "<sup class='units'>PPM</sup>";
        html += "</p>";


        html += "</div>";
        html += "</div>";
        html += "</div>";
        html += "</div>";
        html += "</div>";
        html += "</body>";
        html += "</html>";
    return html;
}

void template_load(){
    if (loadTemplate == true){
    Serial.println("Template loading...");
    tft.fillScreen(TFT_BLACK);
    //=================== BACKGROUNDS ======================================
    tft.fillRect(  0,  0,   w, 80, IAQ_BACKGROUND);    //IAQ Section
    tft.fillRect(  0, 80, w/2, 60, TEMP_BACKGROUND);   //Temp Section
    tft.fillRect(w/2, 80, w/2, 60, HUM_BACKGROUND);    //Hum Section
    tft.fillRect(  0, 140, w/2, 80, CO2_BACKGROUND);    //CO2 Section 
    tft.fillRect(w/2, 140, w/2, 80, PRESS_BACKGROUND);  //Preassure Section
    tft.fillRect(0,   220, w-60, 100, ACT_BACKGROUND);        //Sugested action
    tft.fillRect(w-50, 220, 50,  100, TFT_BLACK);
    //-----------------------------------------------------------------------

    //=================== CONST TEXT ======================================
    //IAQ section
    tft.setTextColor(TFT_BLACK, IAQ_BACKGROUND);
    tft.setFreeFont(FF5);
    tft.drawString("IAQ:", 58, 50);
    tft.setFreeFont(FF19);
    draw_triangle(220, 40, true);
    draw_triangle(220, 75, false);
    draw_icon(icon_iaq, 48, 55, 5, 17);
    
    //Temperature section
    tft.setTextColor(TFT_BLACK, TEMP_BACKGROUND);
    tft.setFreeFont(FF19);
    tft.drawString("C", 94, 87);
    tft.setFreeFont(FF1);
    tft.drawString("o", 86, 82);
    draw_triangleS(65, 127, true);
    draw_triangleS(110, 137, false);
    draw_icon(icon_temp, 50, 17, 2, 87); 
  
    //Humidity section
    tft.setTextColor(TFT_BLACK, HUM_BACKGROUND);
    tft.setFreeFont(FF19);
    tft.drawString("%", 207, 87);
    draw_triangleS(190, 127, true);
    draw_triangleS(228, 139, false);
    draw_icon(icon_hum, 40, 31, (w/2)+5, 87); 

    //CO2 section
    tft.setTextColor(TFT_BLACK, CO2_BACKGROUND);
    tft.setFreeFont(FF18);
    tft.drawString("PPM", 65, 180);
    draw_triangleS(50, 205, true);
    draw_triangleS(110, 217, false);
    draw_icon(icon_co2, 40, 41, 0, 150); 

    //Preassure section
    tft.setTextColor(TFT_BLACK, PRESS_BACKGROUND);
    tft.setFreeFont(FF18);
    tft.drawString("HPa", 190, 180);
    draw_triangleS(50+(w/2), 205, true);
    draw_triangleS(110+(w/2), 217, false);
    draw_icon(icon_press, 40, 30, (w/2)+5, 150);

    //Action Section
    draw_icon(icon_action, 45, 45, 65, 225);

    //OFF Section
    draw_icon(icon_power, 50, 50, 185, 225);

    icon_set();
    //Plot current data
    plotDataValues = true; //Ready to print the IAQ STATUS
    plotIAQstatus = true;
    onLoadData();
    }
}

//================================= Plot values when templateload function execute =================================
void onLoadData(){ 
  if (plotDataValues == true)
  {
        Serial.println("OnLoadData function is on");
        data_proccess currentValues{int(iaqSensor.staticIaq), 
                                    int(iaqSensor.temperature), 
                                    int(iaqSensor.humidity), 
                                    int(iaqSensor.co2Equivalent),
                                    int(iaqSensor.pressure/100.0)
                                    };

        tft.setTextColor(TFT_BLACK, IAQ_BACKGROUND);
        printAndClear(102, 45, String(currentValues.currentIAQ), IAQ_BACKGROUND, 3);

        tft.setFreeFont(FF1);
        printAndClearL(180, 42, String(higger_iaq), IAQ_BACKGROUND, 3);
        printAndClearL( 180, 62, String(lower_iaq), IAQ_BACKGROUND, 3);   //min value IAQ

        tft.setTextColor(TFT_BLACK, TEMP_BACKGROUND);
        printAndClear(45, 87, String(currentValues.currentTemp), TEMP_BACKGROUND, 2);

        tft.setFreeFont(FF1);
        printAndClearL(25, 125, String(higger_temp), TEMP_BACKGROUND, 3); //max value temp
        printAndClearL(72, 125, String(lower_temp),  TEMP_BACKGROUND, 3); //min value temp

        tft.setTextColor(TFT_BLACK, HUM_BACKGROUND);
        printAndClear(168, 87, String(currentValues.currentHum), HUM_BACKGROUND, 2);

        tft.setFreeFont(FF1);
        printAndClearL(160, 125, String(higger_hum), HUM_BACKGROUND, 2); //max value hum
        printAndClearL(200, 125, String(lower_hum), HUM_BACKGROUND, 2); //min value temp

        tft.setTextColor(TFT_BLACK, PRESS_BACKGROUND);
        tft.setFreeFont(FF19);
        printAndClear(42+(w/2), 145, String(currentValues.currentPress), PRESS_BACKGROUND, 4);

        tft.setFreeFont(FF1);
        printAndClearL( 3+(w/2), 205, String(higger_press), PRESS_BACKGROUND, 4);
        printAndClearL(62+(w/2), 205, String(lower_press), PRESS_BACKGROUND, 4);

        tft.setTextColor(TFT_BLACK, CO2_BACKGROUND);
        printAndClear(42, 145, String(currentValues.currentCo2), CO2_BACKGROUND, 4);
        tft.setFreeFont(FF1);
        printAndClearL(62, 205, String(lower_co2), CO2_BACKGROUND, 4);
        printAndClearL(3, 205, String(higger_co2), CO2_BACKGROUND, 4);
        plotValues = true;
    }
}

void detectPress(const char *abc){
  if (drawKeyboard == true && printKeysFlag == true && loadTemplate == false && plotMenu == true)
  {
    Serial.println("detect press on");
    uint8_t showKeyboard = 0;
    while (true)
    {
            uint16_t x = 0, y = 0; // To store the touch coordinates
            // Pressed will be set true is there is a valid touch on the screen
            bool pressed = tft.getTouch(&x, &y);
            if (pressed && 0+start_ypoint<y && y<96+start_ypoint) { 
                uint16_t char_pass = 0;
                for (uint8_t iy = 0; iy < 4; iy++)
                {
                    for (uint8_t ix = 0; ix <10; ix++)
                    {
                       // tft.fillCircle(x, y, 2, TFT_WHITE);
                        if(24*ix< x && x < 24 +(24*ix)  &&  (iy*24)+start_ypoint  < y && y < 24+(24*iy)+start_ypoint){
                            Serial.println("PRESSED: "+String(abc[char_pass+1]));
                           if (text.length() < 80)
                           {
                            effect_press(String(abc[char_pass+1]), (24*ix)+7,  (24*iy)+5+start_ypoint, KEYBOARD_BG);
                             text += String(abc[char_pass+1]);
                            printwrap(text,8, 10);
                            onBuzzer();
                           }
                        }
                    char_pass++;
                    }
                 }
            }
            if (pressed)
            {
            if(100 < x && x < 190 && 100+start_ypoint < y && y < start_ypoint+130){ //Space key
                    text += " ";
                    effect_press("_____", 95, 110+start_ypoint, SPACE_BG);
                    printwrap(text,8, 10);
                    onBuzzer();
                }

            if(0 < x && x < 65 && 100+start_ypoint < y && y < start_ypoint+130){ //ABC key
                    if (typeKeyboard == true)
                    {
                       showKeyboard = 1;
                       typeKeyboard = false;
                       break;
                    }
                    if (typeKeyboard == false)
                    {
                       typeKeyboard = true;
                       showKeyboard = 0;
                       break;
                    }   
                }

            if(180 < x && x < 240 && 100+start_ypoint < y && y < start_ypoint+130){ //Special Char key
                    if (transKeyboard == true)
                    {
                       transKeyboard = false;
                       effect_press("?#!", 195, 110+start_ypoint, ACTIONB_BG);
                       delay(400);
                       tft.setTextColor(TFT_BLACK, ACTIONB_BG);
                       tft.drawString("abc", 15, 108+start_ypoint);
                       printKeysFlag = true;
                       printKeys(" 1234567890qwertyuiopasdfghjkl:zxcvbnm,.-", 24, start_ypoint);
                       onBuzzer();
                       break;
                    }
                    if (transKeyboard == false)
                    {
                       transKeyboard = true;
                       tft.setTextColor(TFT_BLACK, ACTIONB_BG);
                       effect_press("?#!", 195, 110+start_ypoint, ACTIONB_BG);
                       delay(400);
                       printKeysFlag = true;
                       printKeys(" 1234567890|!#$%&/()=?+{}:_[]<>;:,.-@      ", 24, start_ypoint);
                       onBuzzer();
                       break;
                    }   
                }
            
            if(190 < x && x < 240 && start_ypoint-35 < y && y < start_ypoint-5){ //Del key
                    int len;
                    len = text.length();
                    text.remove(len-1,1);
                    effect_press("DEL",   200, start_ypoint-28, DEL_BG);
                    printwrap(text,8, 10);
                    onBuzzer();
                }

            if(50 < x && x < 120 && 140+start_ypoint < y && y < start_ypoint+170){ //Ok key
                prepareDataToSave(text);
                text = "";
                drawKeyboard = false;
                printKeysFlag = false;
                draw_menu();
                onBuzzer();
                return;
                }

            if(140 < x && x < 210 && 140+start_ypoint < y && y < start_ypoint+170){ //Back key
                text = ""; //Clear global variable
                drawKeyboard = false;
                printKeysFlag = false;
                draw_menu();
                return;
                }
            }
    }
    if (showKeyboard == 0 && drawKeyboard == true && printKeysFlag == true && loadTemplate == false && plotMenu == true)
    {
        tft.setTextColor(TFT_BLACK, ACTIONB_BG);
        tft.fillRoundRect(0,   100+start_ypoint,  65, 30, 4, ACTIONB_BG); 
        tft.drawString("abc", 15, 108+start_ypoint);
        printKeysFlag = true;
        printKeys(" 1234567890qwertyuiopasdfghjkl:zxcvbnm,.-", 24, start_ypoint);    
        onBuzzer(); 
    }
    if (showKeyboard == 1 && drawKeyboard == true && printKeysFlag == true && loadTemplate == false && plotMenu == true)
    {
        tft.setTextColor(TFT_BLACK, ACTIONB_BG);
        tft.fillRoundRect(0,   100+start_ypoint,  65, 30, 4, ACTIONB_BG); //SHIFT
        tft.drawString("ABC", 15, 108+start_ypoint);
        printKeysFlag = true;
        printKeys(" 1234567890QWERTYUIOPASDFGHJKL:ZXCVBNM,.-", 24, start_ypoint);
        onBuzzer();
    }
  }
  else{
    Serial.println("DONT DETECT PRESS");
  }
}

void draw_keyboard(uint16_t y){
    if (drawKeyboard == true){
            tft.fillScreen(TFT_BLACK);
            //Text Area
            tft.fillRect(0, 0, 240, 80, TFT_GREY);
            tft.fillRect(5, 5, 230, 70, TFT_BLACK);
            tft.fillRoundRect(190, y-35,  50, 30, 4, TFT_RED); //DEL
            tft.fillRoundRect(0,   100+y,  65, 30, 4, ACTIONB_BG); //SHIFT
            tft.fillRoundRect(180,  100+y,  60, 30, 4, ACTIONB_BG); //Special characters
            tft.fillRoundRect(73, 100+y,  100, 30, 4, SPACE_BG); //Space
            tft.fillRoundRect(50,  140+y,  70, 30, 4, TFT_GREEN); //OK
            tft.fillRoundRect(140, 140+y,  70, 30, 4, TFT_BLUE); //Back
            
            tft.setFreeFont(FF1);
            tft.setTextColor(TFT_BLACK, ACTIONB_BG);
            tft.drawString("abc", 15, 108+start_ypoint);
            tft.drawString("?#!", 195, 110+y);
            tft.setTextColor(TFT_BLACK, SPACE_BG);
            tft.drawString("_____", 95, 110+y);
            tft.setTextColor(TFT_BLACK, DEL_BG);
            tft.drawString("DEL",   200, y-28);
            tft.setTextColor(TFT_WHITE, TFT_GREEN);
            tft.drawString("OK",   80-4, 147+y);
            tft.setTextColor(TFT_WHITE, TFT_BLUE);
            tft.drawString("Back",   155, 147+y);
            dataSavePrint();
    }
}

void prepareDataToSave(String textToSave){
    switch (optionToSave)
    {
    case 1:
        if (mainWifiSSID != textToSave)
        {
            mainWifiSSID = textToSave;
            changeSSID = true;
            saveData("ssid", mainWifiSSID);
        }
        break;
    case 2:
        if (mainWifiPSW != textToSave)
        {
            changeSSIDPSW = true;
            mainWifiPSW = textToSave;
            saveData("password", mainWifiPSW);
        }
        break;
    case 3:
        if (mainNameDevice != textToSave)
        {
            mainNameDevice = textToSave;   
            saveData("device", mainNameDevice);
        }
        break;
    case 4:
        if (mainFirebaseURL != textToSave)
        {
            mainFirebaseURL = textToSave;   
            saveData("api_url", mainFirebaseURL);
        }
        break;
    case 5:
        if (mainFirebaseKey != textToSave)
        {
           mainFirebaseKey = textToSave;
           saveData("api_key", mainFirebaseKey);
        }
        break;
    default:
        break;
    }
        
}

void printwrap(String msg, int x, int y)
{
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setFreeFont(FF1);
    uint16_t auxCount = 0;
    Serial.print("Length = "+msg.length());
    int maxchars = 20;
    if (msg.length() > maxchars or msg.length()-1 < maxchars or msg.length() < maxchars)
    {
      tft.fillRect(5, 10, 230, 15, TFT_BLACK);
      String line1 = msg;
      auxCount = line1.length()-maxchars;
      line1.remove(maxchars, auxCount);
      tft.drawString(line1, x, y);
    }
    if (msg.length() > maxchars*2 or msg.length()-1 < maxchars*2)
    {
      tft.fillRect(5, 25, 230, 15, TFT_BLACK);
      String line2 = msg;
      line2.remove(0, maxchars);
      line2.remove(maxchars, line2.length());
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString(line2, x, y+15);
    }
   if (msg.length() > maxchars*3 or msg.length()-1 < maxchars*3)
    {
      tft.fillRect(5, 40, 230, 15, TFT_BLACK);
      String line3 = msg;
      line3.remove(0, maxchars*2);
      line3.remove(maxchars, line3.length());
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString(line3, x, y+30);
    }
   if (msg.length() < maxchars*4 or msg.length()-1 < maxchars*4)
    {
      tft.fillRect(5, 55, 230, 15, TFT_BLACK);
      String line3 = msg;
      line3.remove(0, maxchars*3);
       line3.remove(maxchars, line3.length());
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString(line3, x, y+45);
    }
    
}

void printKeys(const char *msg, int x, int y){ //Problem detected!!!!!!
if (printKeysFlag == false &&  drawKeyboard == false)
{
    Serial.println("Dont print keys");
}
else if(printKeysFlag == true &&  drawKeyboard == true && loadTemplate == false){
    Serial.println("printing keys");
    tft.fillRect(0, 0+start_ypoint, 240, 96, KEYBOARD_BG);
    tft.setTextColor(TFT_BLACK, KEYBOARD_BG);
        //Horizontal Lines
    tft.drawLine(0,  0+y, 240,  0+y, TFT_BLACK);
    tft.drawLine(0, 96+y, 240, 96+y, TFT_BLACK);
    tft.drawLine(0, 24+y, 240, 24+y, TFT_BLACK);
    tft.drawLine(0, 47+y, 240, 47+y, TFT_BLACK);
    tft.drawLine(0, 72+y, 240, 72+y, TFT_BLACK);

    //Vertical Lines
    tft.drawLine(0,   0+y, 0,    96+y, TFT_BLACK);    
    tft.drawLine(23,  0+y, 23,   96+y, TFT_BLACK);    
    tft.drawLine(47,  0+y, 47,   96+y, TFT_BLACK);    
    tft.drawLine(71,  0+y, 71,   96+y, TFT_BLACK);    
    tft.drawLine(95,  0+y, 95,   96+y, TFT_BLACK);    
    tft.drawLine(119, 0+y, 119,  96+y, TFT_BLACK);    
    tft.drawLine(143, 0+y, 143,  96+y, TFT_BLACK);    
    tft.drawLine(167, 0+y, 167,  96+y, TFT_BLACK);    
    tft.drawLine(191, 0+y, 191,  96+y, TFT_BLACK);    
    tft.drawLine(215, 0+y, 215,  96+y, TFT_BLACK);    
    tft.drawLine(240, 0+y, 240,  96+y, TFT_BLACK);   
    for ( int i = 0; i < 11; i++)
    {
      String keyPrint = String(msg[i]);
       tft.drawString(keyPrint, (i*x)-17,  6+y);
    }
    for (int i = 10; i < 21; i++)
    {
       String keyPrint = String(msg[i]);
       tft.drawString(keyPrint, ((i-10)*x)-17,  28+y);
    }
    for (int i = 20; i < 31; i++)
    {
       String keyPrint = String(msg[i]);
       tft.drawString(keyPrint, ((i-20)*x)-17,  53+y);
    }
    
    for (int i = 31; i < 41; i++)
    {
       String keyPrint = String(msg[i]);
       tft.drawString(keyPrint, ((i-30)*x)-17,  77+y);
    }
    detectPress(msg);
}

}

void system_diagnostic(){
     tft.fillRect(60, 285, 250, 15, TFT_BLACK);
     tft.fillRect(97, 285, 265, 15, TFT_BLACK);
      if (WiFi.status() == WL_CONNECTED )
      {
          String ip_host = String(WiFi.localIP()[0])+"."+String(WiFi.localIP()[1])+"."+String(WiFi.localIP()[2])+"."+String(WiFi.localIP()[3]);
          tft.setTextColor(TFT_GREEN, TFT_BLACK);
          tft.setFreeFont(FF1);
          tft.drawString(ip_host, 60, 285);
      }
      else{
          tft.setTextColor(TFT_RED, TFT_BLACK);
          tft.setFreeFont(FF1);
          tft.drawString("Not connected", 60, 285);
      }

     if(signupOK == true){
        tft.setTextColor(TFT_GREEN, TFT_BLACK); 
        tft.setFreeFont(FF1);
        tft.drawString("Login OK", 100, 300);
    }else{
        tft.setTextColor(TFT_RED, TFT_BLACK); 
        tft.setFreeFont(FF1);
        tft.drawString("Login Fail", 100, 300);
      }
}

void draw_menu(){
    if (plotMenu == false){}
    else{
    Serial.println("Im in draw Menu function");
    tft.fillScreen(TFT_BLACK);
    draw_icon(icon_BWifi, 65, 70, 2, 35);
    draw_icon(icon_BFirebase, 65, 49, 14, 160);
    //=================== BUTTONS ======================================
     tft.fillRoundRect(85, 5, 140, 40, 4, BTN_COLORM); //SSID
     tft.fillRoundRect(85, 50, 140, 40, 4, BTN_COLORM); //PASSWORD
     tft.fillRoundRect(85, 95, 140, 40, 4, BTN_COLORM); //NAME DEVICE
     tft.fillRoundRect(85, 150, 140, 40, 4, BTN_COLORM);  //URL FB
     tft.fillRoundRect(85, 195, 140, 40, 4, BTN_COLORM);  //KEY FB
     tft.fillRoundRect(20, 240, 90, 30, 4, TFT_GREEN); //Save BTN
     tft.fillRoundRect(130, 240, 90, 30, 4, TFT_BLUE); //Back BTN
     
     tft.setTextColor(TEXT_COLORM, BTN_COLORM);
     tft.setFreeFont(FF1);
     tft.drawString("Set SSID", 105, 18);
     tft.drawString("Set", 139, 55);
     tft.drawString("Password", 114, 68);
     tft.drawString("Set Name", 108, 107 );

     tft.drawString("Set URL", 110, 163);
     tft.drawString("Set Key", 110, 206);
     
     tft.setTextColor(TFT_BLACK, TFT_GREEN);
     tft.drawString("Save", 40, 247);
     tft.setTextColor(TFT_BLACK, TFT_BLUE);
     tft.drawString("Back", 150, 247);

     tft.setTextColor(TFT_GREEN, TFT_BLACK);
     tft.setFreeFont(FF1);
     tft.drawString("WiFi:", 3, 285);
     tft.drawString("Firebase:", 3, 300);
     system_diagnostic(); //Check the status of the connections (Wifi and firebase)
      while (true)
      {
          uint16_t x = 0, y = 0; // To store the touch coordinates
          // Pressed will be set true is there is a valid touch on the screen
          bool pressed = tft.getTouch(&x, &y);
          if (pressed) { 
            //  onBuzzer();
              Serial.println("User press the screen");
            //  tft.fillCircle(x, y, 2, TFT_WHITE);
              if(85 < x  && x < 225 && 5< y && y < 45 ){
                    onBuzzer();
                    effect_press("Set SSID", 105, 18, BTN_COLORM);
                    optionToSave = 1;
                    drawKeyboard = true;
                    draw_keyboard(start_ypoint);
                    printKeysFlag = true;
                    printKeys(" 1234567890qwertyuiopasdfghjkl:zxcvbnm,.-", 24, start_ypoint);
                    break;
                  }
              if(85 < x  && x < 225 && 50< y && y < 90 ){
                    onBuzzer();
                    tft.setFreeFont(FF1);
                    tft.setTextColor(TFT_RED, BTN_COLORM);
                    tft.drawString("Set", 139, 55);
                    tft.drawString("Password", 114, 68);
                    delay(200);
                    tft.setTextColor(TFT_BLACK, BTN_COLORM);
                    tft.drawString("Set", 139, 55);
                    tft.drawString("Password", 114, 68);
                    optionToSave = 2;
                    drawKeyboard = true;
                    draw_keyboard(start_ypoint);
                    printKeysFlag = true;
                    printKeys(" 1234567890qwertyuiopasdfghjkl:zxcvbnm,.-", 24, start_ypoint);
                    break;
                  }
              if(85 < x  && x < 225 && 95< y && y < 135 ){
                    onBuzzer();
                    optionToSave = 3;
                    effect_press("Set Name", 108, 107, BTN_COLORM);
                    drawKeyboard = true;
                    draw_keyboard(start_ypoint);
                    printKeysFlag = true;
                    printKeys(" 1234567890qwertyuiopasdfghjkl:zxcvbnm,.-", 24, start_ypoint);
                    break;
                  }
              if(85 < x  && x < 225 && 150< y && y < 190 ){
                    onBuzzer();
                    effect_press("Set URL", 110, 163, BTN_COLORM);
                    optionToSave = 4;
                    drawKeyboard = true;
                    draw_keyboard(start_ypoint);
                    printKeysFlag = true;
                    printKeys(" 1234567890qwertyuiopasdfghjkl:zxcvbnm,.-", 24, start_ypoint);
                    break;
                  }
              if(85 < x  && x < 225 && 195< y && y < 235 ){
                    onBuzzer();
                    effect_press("Set Key", 110, 206, BTN_COLORM);
                    optionToSave = 5;
                    drawKeyboard = true;
                    draw_keyboard(start_ypoint);
                    printKeysFlag = true;
                    printKeys(" 1234567890qwertyuiopasdfghjkl:zxcvbnm,.-", 24, start_ypoint);
                    break;
                  }
              if(20 < x  && x < 110 && 240< y && y < 270 ){
                      onBuzzer();
                      connectWifi();
                      loginFirebase(mainFirebaseURL, mainFirebaseKey, true);
                      onBuzzer();
                      onBuzzer();
                      onBuzzer();
                  }
              if(130< x  && x < 220 &&  240< y && y < 270 ){
                     onBuzzer();
                     printKeysFlag = false;
                     loadTemplate = true;
                     template_load();
                   //  delay(500);
                     plotMenu = false;
                     drawKeyboard = false;
                     come_to_home = true;
                     break;
                  }
                delay(500);
        } 
      }
        
    }
}

void saveData(String param, String data){
    const char* dataSave = data.c_str();
    const char* paramSave = param.c_str();
    preferences.begin("settings", false);
    preferences.putString(paramSave, data); 
    Serial.println("Settings Saved!");
    preferences.end();
}

void effect_press(String key_press, int x, int y, int color){
        tft.setFreeFont(FF1);
        tft.setTextColor(TFT_RED, color);
        tft.drawString(key_press, x, y);
        delay(200);
        tft.setTextColor(TFT_BLACK, color);
        tft.drawString(key_press, x, y);
}

void getData(){
  preferences.begin("settings", false);
  mainWifiSSID = preferences.getString("ssid", ""); 
    delay(400);
  mainWifiPSW = preferences.getString("password", "");
    delay(400);
  mainNameDevice = preferences.getString("device", "");
    delay(400);
  mainFirebaseURL = preferences.getString("api_url", "");
    delay(400);
  mainFirebaseKey = preferences.getString("api_key", "");
    delay(400);
  Serial.println("SSID: "+mainWifiSSID+" Password:"+mainWifiPSW+" Device name:"+mainNameDevice+" Firebase URL:"+mainFirebaseURL+ " FirebaseKey:"+mainFirebaseKey);
  Serial.println("SSID: "+String(mainWifiSSID.length())+" Password:"+String(mainWifiPSW.length())+" Device name:"+String(mainNameDevice.length())+" Firebase URL:"+String(mainFirebaseURL.length())+ " FirebaseKey:"+String(mainFirebaseKey.length()));
  preferences.end();
}

void connectWifi(){
    tft.setFreeFont(FF1);
    if (changeSSID == true or changeSSIDPSW == true)
    {
        changeSSID = false;
        changeSSIDPSW = false;
        WiFi.disconnect();
    }
    tft.fillRect(60, 285, 250, 15, TFT_BLACK);
    if (WiFi.status() == WL_CONNECTED){
        tft.setTextColor(TFT_GREEN, TFT_BLACK); 
        tft.drawString(String(WiFi.localIP()[0])+"."+String(WiFi.localIP()[1])+"."+String(WiFi.localIP()[2])+"."+String(WiFi.localIP()[3]), 60, 285);
    }
    if (WiFi.status() != WL_CONNECTED)
    {
        if (mainWifiSSID.length() > 0)
         {
            WiFi.begin(mainWifiSSID.c_str(), mainWifiPSW.c_str());
            int get_timeout = 0;
                //check wi-fi is connected to wi-fi network
                while (true) {
                        if (WiFi.status() == WL_CONNECTED)
                        {
                        tft.fillRect(60, 285, 250, 15, TFT_BLACK); 
                        tft.setTextColor(TFT_GREEN, TFT_BLACK); 
                        tft.drawString(String(WiFi.localIP()[0])+"."+String(WiFi.localIP()[1])+"."+String(WiFi.localIP()[2])+"."+String(WiFi.localIP()[3]), 60, 285);
                        break;
                        }
                        tft.setTextColor(TFT_GREEN, TFT_BLACK); 
                        tft.drawString(".", 60+(get_timeout*10), 285);
                        delay(1000);
                        if (get_timeout == 15){
                            tft.fillRect(60, 285, 250, 15, TFT_BLACK); 
                            tft.setTextColor(TFT_RED, TFT_BLACK);
                            tft.setFreeFont(FF1);
                            tft.drawString("Not connected", 60, 285);
                        break;
                        }
                        get_timeout++;
                }
         }
         if (mainWifiSSID.length() == 0)
         {
            tft.fillRect(60, 285, 250, 15, TFT_BLACK); 
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.setFreeFont(FF1);
            tft.drawString("Not configured", 60, 285);
         } 
    } 
}

void loginFirebase(String apiURL, String apiKey, bool flag){
    //Authenticate to Firebase
    /* Assign the api key (required) */
      config.api_key = apiKey.c_str();
      /* Assign the RTDB URL (required) */
      config.database_url = apiURL.c_str();
      Firebase.reconnectWiFi(true);
      /* Sign up */
      bool FirebaseStatus = Firebase.signUp(&config, &auth, "", "");
      if (FirebaseStatus){
        Serial.println("ok");
        signupOK = true;
        if (flag == true)
        {
            tft.fillRect(97, 300, 265, 15, TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK); 
            tft.setFreeFont(FF1);
            tft.drawString("Login Ok", 100, 300);
        }
      }
      else{
        Serial.printf("%s\n", config.signer.signupError.message.c_str());
        signupOK = false;
        if (flag == true)
        {
            tft.fillRect(97, 300, 265, 15, TFT_BLACK);
            tft.setTextColor(TFT_RED, TFT_BLACK); 
            tft.setFreeFont(FF1);
            tft.drawString("Login Fail", 100, 300);
        } 
      }
        /* Assign the callback function for the long running token generation task */
        config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
        Firebase.begin(&config, &auth);
}

void draw_button(const uint16_t* icon, int color, int w, int h, int x, int y, String text, String text_1, String text_2){
   //Draw Button
  tft.fillRoundRect(x-15, y, 139+w, 20+h, 3, color);
  tft.setTextColor(TFT_WHITE, color);
  tft.drawLine(w+25, y+3, w+25, y+12+h, TFT_WHITE);
  tft.setFreeFont(FF1);
  tft.drawString(text,   x+w, y+10);
  tft.drawString(text_1, x+w, y+30);
  tft.drawString(text_2, x+w, y+50);

  tft.fillRect(5+10, 210, 200, 30, TFT_WHITE);
  tft.fillRect(7+10, 212, 195, 26, TFT_BLACK);

 //Draw Icon
  int row, col, buffidx=0;
  for (row=0; row<h; row++) { // For each scanline...
    for (col=0; col<w; col++) { // For each pixel...
      //To read from Flash Memory, pgm_read_XXX is required.
      //Since image is stored as uint16_t, pgm_read_word is used as it uses 16bit address
      tft.drawPixel(col+x-9, row+y+9, pgm_read_word(icon + buffidx));
      buffidx++;
    } // end pixel
  }
}

void draw_triangle(uint16_t x, uint16_t y, bool orientation){
     if(orientation == true){
        tft.fillTriangle(x, y, x-8, y+15, x+8, y+15, TFT_RED);   
      }
     else{
        tft.fillTriangle(x, y, x-8, y-15, x+8, y-15, TFT_BLUE);   
      }
  }

void draw_triangleS(uint16_t x, uint16_t y, bool orientation){
     if(orientation == true){
        tft.fillTriangle(x, y, x-5, y+10, x+5, y+10, TFT_RED);   
      }
     else{
        tft.fillTriangle(x, y, x-5, y-10, x+5, y-10, TFT_BLUE);   
      }
}

void printAndClear(uint16_t x, uint16_t y, String textToPrint, int color, uint8_t mL){
 if (mL == 3) {
      tft.fillRect(x-5, y, 78, 33, color);
      switch (textToPrint.length())
      {
          case 0:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString("666", x, y);
            break;
          case 1:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x+50, y);
              break;
          case 2:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x+30, y);
              break;
          case 3:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x, y);
              break;
          default:
            break;
      }
 }
 if (mL == 2)
 {
      tft.fillRect(x-10, y, 50, 33, color);
      switch (textToPrint.length())
      {
          case 0:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString("0", x, y);
            break;
          case 1:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x+20, y);
              break;
          case 2:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x, y);
              break;
          default:
            break;
      }
 }
  if (mL == 4)
 {
      tft.fillRect(x-2, y, 77, 33, color);
      switch (textToPrint.length())
      {
          case 0:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString("0", x, y);
            break;
          case 1:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x+53, y);
              break;
          case 2:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x+15, y);
              break;
           case 3:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x+15, y);
              break;
           case 4:
              tft.setTextColor(TFT_BLACK, color);
              tft.setFreeFont(FF19);
              tft.drawString(textToPrint, x, y);
              break;
          default:
            break;
          
      }
 }


}

void printAndClearL(uint16_t x, uint16_t y, String textToPrint, int bgColor, uint8_t mL){
 if (mL == 3){
        tft.fillRect(x-3, y, 35, 15, bgColor);
        switch (textToPrint.length())
              {
              case 0:
                  tft.setTextColor(TFT_BLACK, bgColor);
                  tft.setFreeFont(FF1);
                  tft.drawString(textToPrint, x, y);
                break;
              case 1:
                  tft.setTextColor(TFT_BLACK, bgColor);
                  tft.setFreeFont(FF1);
                  tft.drawString(textToPrint, x+16, y);
                  break;
              case 2:
                  tft.setTextColor(TFT_BLACK, bgColor);
                  tft.setFreeFont(FF1);
                  tft.drawString(textToPrint, x+8, y);
                  break;

              case 3:
                  tft.setTextColor(TFT_BLACK, bgColor);
                  tft.setFreeFont(FF1);
                  tft.drawString(textToPrint, x, y);
                  break;
              default:
                break;
              }
          }
 if (mL == 2){
            tft.fillRect(x, y, 25, 15, bgColor);
            switch (textToPrint.length())
          {
          case 0:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1);
              tft.drawString("666", x, y);
            break;
          case 1:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1);
              tft.drawString(textToPrint+5, x, y);
              break;
          case 2:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1);
              tft.drawString(textToPrint, x, y);
              break;
          default:
            break;
          }
 }

  if (mL == 4) {
            tft.fillRect(x-3, y, 45, 15, bgColor);
            switch (textToPrint.length())
          {
          case 0:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1);
              tft.drawString(textToPrint, x, y);
            break;
          case 1:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1);
              tft.drawString(textToPrint, x+15, y);
              break;
          case 2:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1); 
              tft.drawString(textToPrint, x+10, y);
              break;
          case 3:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1);
              tft.drawString(textToPrint, x+5, y);
              break;
          case 4:
              tft.setTextColor(TFT_BLACK, bgColor);
              tft.setFreeFont(FF1);
              tft.drawString(textToPrint, x, y);
              break;
          default:
            break;
          }
 }
 
}

void icon_set(){
    if (WiFi.status() == WL_CONNECTED)
    {
    draw_icon(icon_wifiOK, 38, 50, 185, 279);
    }
    if (WiFi.status() != WL_CONNECTED)
    {
          //WIFI Section
    draw_icon(icon_wifid, 40, 50, 185, 275);
    }
}