/**************************************************************************** 
 *  ThermoX v0.30.0 HVAC Thermostat
 *  
 *  Compares ambient and desired temperatures, and runs heat or A/C as needed.  
 *  Hysteresis levels for both Summer and Winter are independently adjustable. 
 *  Sensor can be corrected +/- 5 degrees. All user preferences are saved, and 
 *  reloaded on startup.
 *  
 *  HOME setting can be triggered in IFTTT by your cell phone location channel,  
 *  causing an action on the Webhooks channel. Webhooks parameters are as follows:
 *       URL: http://blynk-cloud.com:8080/YOUR_TOKEN/pin/V31
 *       Method: PUT
 *       Content Type: application/json
 *       Body: ["1"]    
 *  Make an identical IFTTT recipe for AWAY but use ["0"] for the body parmeter.
 *  
 *  Color coded DESIRED TEMPERATURE widget: red/blue/green for heat/cool/off
 *  modes, respectively. 
 *    
 *  PERCEIVED TEMPERATURE mode augments actual temperature when Summer humidity is high.
 *  
 *  Added 15 minutes ON "pulse" mode.
 *  
 *  New minimum and maximum temperature settings override "away" mode (my plants were 
 *  dying). Now, even in AWAY mode, HVAC will come on if min/max limits are exceded.
 *  
 *  Added native Alexa control (HUE emulation). 
 *    -   "Turn ON" activates deactivates system halt, and runs pulse mode. 
 *    -   "Turn OFF" activates cancels pulse, or activates system halt if no pulse was running.     
 *    -   "Turn UP/DOWN" chages temperature 2 degrees.
 *    -   "Set ThermoX to ##" sets a new desired temperature
 *  
 *  An independent "Alexa, set temperature to ##" changes desired temperature setting.
 * 
 *  Automatically reconnects to last working wifi. If unavailable, it creates an access 
 *  point ("ThermoX") to which you can connect and share locl wifi credentials.
 *  
 *  Use any ESP8266, a relay on GPIO 0, and DHT11 temperature sensor on GPIO 2. 
 *  OTA updates are only possible on devices with >512K memory.
 *  
 * Oled
*****************************************************************************
*/
#include <ESP8266WiFi.h>        //https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h> // must be included here so that Arduino library object file references work
#include <RtcDS1307.h>
RtcDS1307<TwoWire> Rtc(Wire);
#include <OLED_I2C.h>
OLED  myOLED(SDA, SCL); // Remember to add the RESET pin if your display requires it... NodeMCU Board (D2,D1 ) Pin
extern uint8_t logo[];
#include <BlynkSimpleEsp8266.h> //https://github.com/blynkkk/blynk-library
#include <WiFiManager.h>        //https://github.com/tzapu/WiFiManager
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <Espalexa.h>           //https://github.com/Aircoookie/Espalexa
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"                //https://github.com/adafruit/DHT-sensor-library
#define DHTPIN 14     // Digital pin connected to the DHT sensor Node MCU D5
#define DHTTYPE    DHT11     // DHT 11
//#define DHTTYPE    DHT22     // DHT 22 (AM2302)
//#define DHTTYPE    DHT21     // DHT 21 (AM2301)
#define UpdateFrequency 8000    //How often a new temperature will be read
#define MenuTimeOut 10000       //Menu timeout from inactivity
#define LongPress 300           //How long SETTINGS button needs to be pressed to enter menu
#define RelayPin  0              // Arduino Wemos D1 (D8 pin ) NodeMcu (D3 Pin)
#define OFF 0                   // These just make the code easier to read
#define ON 1
#define OLED_RESET 0  // GPIO0

Adafruit_SSD1306 display(OLED_RESET);
//WiFi and Blynk connection variables
String myHostname = "KombiX";
char auth[] = "YourAuthToken"; // Blynk token "YourAuthToken"

//Set up as a native Alexa device (Hue emulation)
char Device1[] = "ThermoX";     // ON/OFF switch name in the Alexa app
EspalexaDevice* espalexaPointer;
Espalexa espalexa;

// Blynk color palette
const String BLYNK_BLUE =    "#04C0F8";
const String BLYNK_RED   =   "#D3435C";
const String BLYNK_GREEN  =  "#23C48E";
String TARIH;
String SAAT;
const long interval = 1000;
unsigned long previousMillis = 0; 
unsigned long TpreviousMillis = 0; 
const long Tkesme = 2500;
unsigned long Tonceki = 0;
unsigned long eskiZaman=0;
unsigned long yeniZaman;
unsigned long eskiZaman1=0;
unsigned long yeniZaman1;

// Timer for temperature updates
BlynkTimer timer;

//Thermostat variables
int TempDes = 21;             //Desired temperature setting
int PreviousTempDes;
int TempAct = 21;             //Actual temperature, as measured by the DHT11 sensor
int BadRead = 0;              //Counts consecutive failed readings of the DHT11 sensor
float LastRead = 21;          // Previous temperature reading
int Humidity = 10; 
int TempMin = 10;             // Minimum allowable temperature, even if in "away" mode
int TempMax = 32;             // Maximum allowable temperature, even if in "away" mode

// Preference variables
int Hysteresis_W = 2;         //Summer and Winter hysteresis levels
int Hysteresis_S = 2;
int TempCorrection = 0;       //Used to adjust readings, if the sensor needs calibration
boolean UsePerceivedTemp = false; // Use humidity-adjusted perceived temperature, instead of actual temperature
long PulseTime = 15 * 60 * 1000; // Amount of time for a "pulse" manual run of the system (15 minutes)

// Current condition variables
boolean Winter = true; 
boolean Home = true;
boolean ManualRun = false;    // used to run fan, overriding thermostat algorithm
boolean ManualStop = false;   // used to stop fan, overriding thermostat algorithm
int MenuItem = 0;             // Settings menu selection variable
boolean ButtonPressed = false;// Settings button state
boolean LongHold = false;     // Flag showoing a long hold detected on the SETTINGS button
int ButtonTimer;              // Timer for detecting long press of Settings button
String Response = "";         // Text output to SETTINGS value widget
boolean FanState = OFF;       // Is the fan on or off?
int MenuTimer;                // Timer for resetting SETTINGS menu after a timeout has elapsed
#define countof(a) (sizeof(a) / sizeof(a[0]))
DHT dht(DHTPIN, DHTTYPE);
void setup() {
  //Initialize the fan relay. Mine is "off" when the relay is set HIGH.
  pinMode(RelayPin,OUTPUT); 
  digitalWrite(RelayPin,HIGH);
   Rtc.Begin();
   RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
   printDateTime(compiled);
     RtcDateTime now = Rtc.GetDateTime();
    Serial.println();
   display.begin(SSD1306_SWITCHCAPVCC, 0x3C); 
  // Create an access point if no wifi credentials are stored
  WiFi.hostname(myHostname);
  WiFiManager wifi;
  wifi.autoConnect("ThermoX"); 
  Blynk.config(auth);
    dht.begin(); //Start temperature sensor and wait for initialization
  delay(1000);
  Serial.begin(115200);
    //Load any saved settings from the EEPROM
  EEPROM.begin(20);  
  GetPresets();
  PreviousTempDes = TempDes; 
   MenuReset();
  ArduinoOTA.begin();
  // Espalexa initialization. Parameters: (device name, callback function, device type, initial value)
  espalexaPointer = new EspalexaDevice(Device1, AlexaCommands, EspalexaDeviceType::dimmable, TempDes * 2.55); 
  espalexa.addDevice(espalexaPointer);
  espalexa.begin();

  timer.setInterval(UpdateFrequency, TempUpdate); // Update temp reading and relay state
  timer.setInterval(30000L, OtherUpdates);        // Refreshes non-urgent dashboard info
}


void loop() {
 // int okunan_deger = DHT11_Sensorum.read(DHT11_pini);
// DHT Temp and Hum Terminal Print
//read temperature and humidity
  float t = dht.readTemperature();
  float h = dht.readHumidity();
    if (isnan(h) || isnan(t)) {
    Serial.println("Nem Is? Sens?r? Okunamad?");
  }
 myOLED.rotateDisplay(true);
RtcDateTime now = Rtc.GetDateTime();
printDateTime(now);
Tarihi();
istenendurum();
isinemyaz();


// delay(200);
  Blynk.run();
  timer.run();
  ArduinoOTA.handle();
  espalexa.loop();
  
}


//*********************** Thermostat Functions **********************************

// This is the decision algorithm for turning the HVAC on and off
void TempUpdate (){
 // float ReadF = dht.readTemperature(true); //Get a new reading from the temp sensor
  int ReadF = dht.readTemperature()  ;
  if (isnan(ReadF)) {
    BadRead++;
    return;
  }

  // Use perceived temperature instead of actual temperature for Summer cooling
  if(UsePerceivedTemp == true && !Winter && ReadF > 21){
    // Because perceived temp swings can be large, augment by only a fraction of
    // a degree per read. Changes are slowed, and more samples inform the average.
    if(ReadF > LastRead + 0.5){
      ReadF = LastRead + 0.5;  
    }
    else if(ReadF < LastRead - 0.5){
      ReadF = LastRead - 0.5;
    }
    // Simplified "feels like" temperature formula
    ReadF = ((Humidity * .02 * (ReadF - 21)) + ReadF);
  }

  //To compensate for the DH11's inaccuracy, the temperature is averaged
  //withprevious read, and any change is limited to 1 degree at a time. 
  int TempAvg = int((ReadF + LastRead + (2 * TempCorrection))/2);
  if (TempAvg > TempAct){
    TempAct += 1;
  }
  else if (TempAvg < TempAct){
    TempAct -= 1;
  }

  LastRead = ReadF;
  BadRead = 0;        // Reset counter for failed sensor reads
  

//************Display OLED ( Ekrana Yaz..)
  // display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Logo adafruit
 // display.display();
//  delay(2000);



  Blynk.virtualWrite(V0,TempAct); //Report the corrected temperature in app
  Blynk.virtualWrite(V30,(int(dht.readHumidity()))); //Report the corrected temperature in app


  // Decision algorithm for running HVAC
  if (!ManualRun && !ManualStop){   // Make sure it's not in one of the manual modes
    // If I'm home, run the algorithm
    if (Home){
      if (Winter){
        //If I'm home, it's Winter, and the temp is too low, turn the relay ON
        if (TempAct < TempDes){
          Fan(ON);
        }
        //Turn it off when the space is heated to the desired temp + a few degrees
        else if (TempAct >= (TempDes + Hysteresis_W)) {
          Fan(OFF);
        }
      }
      else if (!Winter){
        //If I'm home, it's Summer, and the temp is too high, turn the relay ON
        if (TempAct > TempDes){
          Fan(ON);
        }
        //Turn it off when the space is cooled to the desired temp - a few degrees
        else if (TempAct <= (TempDes - Hysteresis_S)){
          Fan(OFF);
        }
     }
    }
    // If I'm not home...
    else {
      // Turn on the HVAC if the temperature outside of seasonal the minimum / maximum limits
      if((Winter && TempAct < TempMin) || (!Winter && TempAct > TempMax)){
        Fan(ON);
      }
      // Otherwise, turn it off
      else{
        Fan(OFF);
      }   
    }
  }
}
void printDateTime(const RtcDateTime& dt)
{
    char datestring[11];

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u"),
           
            dt.Day(),
             dt.Month(),
            dt.Year());
    Serial.println(datestring);
    char timestring[9];

    snprintf_P(timestring, 
            countof(timestring),
            PSTR("%02u:%02u:%02u"),
           
             dt.Hour(),
            dt.Minute(),
            dt.Second());
    Serial.println(timestring);
    TARIH = datestring ;
    SAAT =  timestring;
   }

void istenendurum()
{
    yeniZaman = millis(); 
if(yeniZaman-eskiZaman > 3500){
   eskiZaman = yeniZaman;
  
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 1);
    
    display.println("ISTENEN :");
     display.println(" ");
    display.setTextSize(2);
    display.print(TempDes) ;
     display.print(" ");
     display.print("Derece");
    display.setTextSize(1);
    display.setCursor(1, 35);
    display.setTextSize(1);
    display.println("MEVCUT:");
   //  display.println(" ");
     display.setTextSize(2);
     display.print(int(dht.readTemperature()));
     display.print(" ");
     display.print("Derece");
  
 myOLED.drawBitmap(0, 16, logo, 48, 48);
 myOLED.update();
    display.display();
    Serial.print("Nem (%): ");
Serial.println(dht.readHumidity());
Serial.print("Sicaklik : ");
Serial.print(dht.readTemperature());
Serial.println(" Derece ");
//delay(4000);

 } 
}
void isinemyaz()
{
   yeniZaman1 = millis(); 
if(yeniZaman1-eskiZaman1 > 6500){
   eskiZaman1 = yeniZaman1;
  
   // display temperature
   display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("SICAKLIK: ");
  display.setTextSize(2);
  display.setCursor(0,10);
  display.print(dht.readTemperature());
  display.print(" ");
  display.setTextSize(1);
  display.cp437(true);
  display.write(167);
  display.setTextSize(2);
  display.print("C ");
 
  // display humidity
  display.setTextSize(1);
  display.setCursor(0, 35);
  display.print("NEM ORANI: ");
  display.setTextSize(2);
  display.setCursor(0, 45);
  display.print(dht.readHumidity());
  display.print(" %"); 
    display.display(); 
}
}

void Tarihi()
{
   unsigned long Tsayac = millis();  // if d?ng?s? ile 2 saniye bekliyoruz. 2 saniyede bir veriler ekrana yazd?r?lacak.
if (Tsayac - Tonceki >= Tkesme) {
    Tonceki = Tsayac;
 Serial.println(TARIH);   // Serial Print
 Serial.println(SAAT);
 display.clearDisplay();  // Oled I2C 128x64 Display 
 display.setTextSize(2);
 display.setCursor(0, 0);
   display.println(TARIH);
   display.setCursor(15, 18);
   display.println(SAAT);
    display.setCursor(23, 40);
  display.setTextSize(1);  
   display.println("basersoft.com" );
     display.setCursor(25, 55);
    display.println("Adullah BASER" );
   display.display();
   //delay(2000);
}
}

// Turn the HVAC ON or OFF
void Fan(boolean RunFan){
  FanState = RunFan;

  // Set the proper color for the Desired Temp gauge and ON/OFF LED
  //(red = heating, blue = cooling, fan off = normal widget color
  if (Winter && FanState){
      Blynk.setProperty(V0, "color", BLYNK_RED);
      Blynk.setProperty(V32, "color", BLYNK_RED);
      Blynk.virtualWrite(V32, "KOMB? ?ALI?IYOR");  // BLYNK App Display Role Value
   
      display.clearDisplay();
       display.setTextSize(3);
      display.setTextColor(WHITE);
      display.setCursor(18, 1);
      display.println("KOMBI");
       display.setCursor(23, 30);
      display.println("ACIK");
      display.display();
      //delay(200);

    }
    else if (!Winter && FanState){
      Blynk.setProperty(V0, "color", BLYNK_BLUE);
      Blynk.setProperty(V32, "color", BLYNK_BLUE);
      Blynk.virtualWrite(V32, "KIL?MALAR A?ILDI"); // BLYNK App Display Role Value
      display.clearDisplay();
      display.setTextSize(3);
      display.setTextColor(WHITE);
      display.setCursor(10, 1);
      display.println("EVDEN");
      display.setCursor(5, 26);
      display.println("UZAKTA");
      display.display();
      //delay(200);  
    }
    else{
      // Return widgets to their "off" state color, depending on theme
        Blynk.setProperty(V0, "color", BLYNK_GREEN);  
         Blynk.setProperty(V32, "color", BLYNK_GREEN);
      Blynk.virtualWrite(V32, "   KOMB? KAPALI");  // BLYNK App Display Role Value
      
      display.clearDisplay();
      display.setTextSize(3);
      display.setTextColor(WHITE);
      display.setCursor(14, 1);
      display.println("KOMBI");
       display.setCursor(8, 28);
      display.println("KAPALI"); // Off Value
      display.display();

    }
  digitalWrite(RelayPin,!FanState); // Relay turns fan on with LOW input, off with HIGH
}


// Ends manual pulse mode
void KillManual(){
  Fan(OFF);
  ManualRun = false;
}


//Temperature slider. Make the desired temperature gauge in Blynk reflect slider changes.
BLYNK_WRITE(V3){
  TempDes = param.asInt();
  Blynk.virtualWrite(V1,TempDes);
  ManualStop = false;      //New temperature setting ends any manual stop
  if(espalexaPointer != nullptr){     //Update espalexa "brightness" value
    espalexaPointer->setPercent(TempDes); 
  }
}

// Updates dashboard information on the Blynk app
void OtherUpdates(){
  Blynk.virtualWrite(V29,Home * 1023); // Update "home" LED on dashboard
  Blynk.virtualWrite(V1,TempDes);      //Update desired temp on the dashboard
   
   // Notify when the temperature sensor fails repeatedly, and turn off the fan.
   if(MenuItem == 0 && !ButtonPressed){
     if (BadRead > 10){
       Blynk.virtualWrite(V10, String("<<< SENSOR MALFUNCTION >>>"));
       BadRead = 0;
       if (!ManualRun){ //Manual mode supersedes a malfunction condition
        Fan(OFF);
       }
     }
     // Clear notification when sensor reads correctly again
     else{
      MenuReset();
     }
   }
   
   if (TempDes != PreviousTempDes){   //update the EEPROM if desired temperature had changed.
    EEPROM.write(3,TempDes);
    EEPROM.commit();
    PreviousTempDes = TempDes;  
   }

  // To stabilize perceived temperature calculation, only update humidity readings between fan cycles
  if(FanState == OFF){
 //   float ReadH = dht.readHumidity();          // Read humidity (percent)
    float ReadH =dht.readTemperature();
    // Only update hmidity if it's a good read from the sensor. To mitigate any
    // instability, average with previous reading, change by only 1% per reading
    if(!(isnan(ReadH))){
      int HumidityAvg = (ReadH + Humidity) / 2;
      if (HumidityAvg > Humidity){
        Humidity += 1;
      }
      if (HumidityAvg < Humidity){
        Humidity -=1;
      }
    }
    
     Blynk.virtualWrite(V2, Humidity);
     
  }   
}


//************************ External Changes (Alexa, IFTTT) ************************************
// Alexa native device (shows up in Alexa app as a Hue device)
void AlexaCommands(EspalexaDevice* espalexaPointer) { 
  if(espalexaPointer == nullptr) return;

  //Retrieve numeric value, and show in Blynk settings tab
  int AlexaPercent = espalexaPointer->getPercent();
  Response = "Alexa temp: ";
  Response += AlexaPercent;
  Blynk.virtualWrite(V10,Response);
  MenuTimer = timer.setTimeout(MenuTimeOut, MenuReset);
  
  // "Alexa, Turn OFF" ends manual run, or applies Manual stop if not running
  if(AlexaPercent == OFF){
    Fan(OFF);
    if(ManualRun){
      ManualRun = false;
    }
    else{
      ManualStop = true;
    }
  } 
  // "Alexa, turn ON," set level (temperature), or augment
  else{
    // If the fan is already ON, use imcomming level for temperature setting
    if(FanState == ON){
      //"Alexa, turn UP..." triggers an unusually big change. Incremnent 2 degrees.
      if(AlexaPercent > TempDes + 10){   
        TempDes += 2;
      }
      //"Alexa, turn DOWN..." triggers an unusually big change. Decrement 2 degrees.
      else if(AlexaPercent < TempDes - 10){   
        TempDes -= 2;
      }
      //"Alexa, set ThermoX to ##" triggers a reasonable change. Use as desired temperature.
      else if(AlexaPercent >= TempMin && AlexaPercent <= TempMax){
        TempDes = AlexaPercent;
      }  
      Blynk.virtualWrite(V1, TempDes);
      Blynk.virtualWrite(V3, TempDes);
      if(espalexaPointer != nullptr){     //Update espalexa "brightness" value
        espalexaPointer->setPercent(TempDes); 
      }  
    }
    // Otherwise, it was a "Turn ON" command, so run a pulse cycle
    else{
      ManualRun = true;
      Fan(ON);
      timer.setTimeout(PulseTime, KillManual);
    }
  }
}

//Get location (home or away) from the IFTTT iOS location and Maker channels
BLYNK_WRITE(V31)
{   
  Home = param.asInt(); 
  if (Home){ //Turn the HOME LED widget on or off
    Blynk.virtualWrite(V29,1023);
  }
  else Blynk.virtualWrite(V29,0);
}


//************************** Settings Menu Functions *******************************

// Dashboard SETTINGS button. Press-and-hold to enter menu. Short press for next item.
BLYNK_WRITE(V4) {    
  // When the SETTINGS button is pressed, start a timer to check for a long press
  if(param.asInt()){
    ButtonTimer = timer.setTimeout(300, LongHoldDetect);
    ButtonPressed = true;
  }
   
  // Button has been released
  else {
    timer.deleteTimer(ButtonTimer);   // Kill current long button hold detection
    ButtonPressed = false;        // Reset the button press flag
      
    // If the long hold function wasn't just called, it's a short press. Avance the menu.
    if (!LongHold && MenuItem != 0){    
      NextMenuItem(); // Advance to next menu item
    }
    // Reset the long press flag
    LongHold = false;
  }
}

// Checks for long press condition on SETTINGS button
void LongHoldDetect(){
  // If the button is still depressed, it's a long hold
  if (ButtonPressed && LongHold == false){  
    // Enter or exit the SETTINGS menu, if it was a long press 
    LongHold = true;      // Flag prevents repeated tripping of long hold
    if (MenuItem == 0){
      MenuTimer = timer.setTimeout(MenuTimeOut, MenuReset);
      NextMenuItem(); // Enter the SETTINGS menu    
    }
    else{
      MenuReset(); // Exit the SETTINGS menu
    }
  }
}


//Cycles through the Settings Menu in the Labeled Value widget
void NextMenuItem(){
  timer.restartTimer(MenuTimer);
   
  MenuItem += 1;
  if (MenuItem > 8){
    MenuItem = 1;
  }
    
  switch(MenuItem){
      case 1:
        if (ManualRun){
          Response = "CANCEL PULSE?";
        }
        else{
          Response = "15 MIN PULSE?";
        }
        break;

      case 2:
        if (UsePerceivedTemp){
          Response = "USE ACTUAL TEMP?";
        }
        else Response = "USE PERCEIVED TEMP?";
        break;

      case 3:
        if (ManualStop){
          Response = "END SYSTEM HALT?";
        }
        else{
          Response = "HALT SYSTEM?";
        }
        break;
        
     case 4:
      if (Home){
        Response = "LOCATION: HOME";
      }
      else Response = "LOCATION: AWAY";
      break;


    case 5:
      if (Winter){
        Response = "MODE : WINTER";
      }
      else Response = "MODE : SUMMER";
      break;

    case 6:
      if (Winter){
        Response = "HYSTERESIS: ";
        Response +=  Hysteresis_W;
        Response += " DEG";   
      }
      else{
        Response = "HYSTERESIS: ";
        Response += Hysteresis_S;
        Response += " DEG";
      }
      break;

    case 7:
      Response = "TEMP CORRECTION: ";
      Response += TempCorrection;
      Response += " DEGREES";
      break;

    case 8:
      if(Winter){
        if(TempMin < 10 || TempMin > 32){
          Response = "SET MINIMUM TEMP?";
        }
        else{
          Response = "MINIMUM TEMP: ";
          Response += TempMin;
        }
      }
      else{
        if(TempMin < 10 || TempMin > 32){
          Response = "SET MAXIMUM TEMP?";
        }
        else{
          Response = "MAXIMUM TEMP: ";
          Response += TempMax;
        }
      }
      break;
  }
  Blynk.virtualWrite(V10,Response);
}


//Dashboard MODIFY button. Executes change of selected menu item 
BLYNK_WRITE(V5){   
  if (MenuItem > 0 && param.asInt()){ 
    timer.restartTimer(MenuTimer);
       
    switch(MenuItem){

      //Forced 15 minute run
      case 1:
        if (ManualRun){
          ManualRun = false;
          Response = "15 MIN PULSE?";
        }
        else{
          ManualRun = true;
          ManualStop = false;
          Fan(ON);
          Response = "PULSE: ON";
          timer.setTimeout(PulseTime, KillManual);
        }   
        break;

      //User perceived temperature instead of actual
      case 2:
        if (UsePerceivedTemp){
          Response = "ACTUAL TEMP MODE";
          UsePerceivedTemp = false;
          EEPROM.write(5,0);
        }
        else {
          Response = "PERCEIVED TEMP MODE";
          UsePerceivedTemp = true;
          EEPROM.write(5,1);
        }
        if(UsePerceivedTemp){
          Blynk.setProperty(V0, "label", "             Perceived Temperature");
        }
        else{
          Blynk.setProperty(V0, "label", "               Actual Temperature");
        } 
        break; 

      //Turn system off
      case 3:
        if (ManualStop){
          ManualStop = false;
          Response = "HALT SYSTEM?";
        }
        else {
          ManualStop = true;
          ManualRun = false;
          Fan(0);
          Response = "SYSTEM HALTED";
        }
        break;

       //Change location manually
      case 4:
        if (Home){
          Home = false;
          Response = "LOCATION : AWAY";
        }
        else {
          Home = true;
          Response = "LOCATION : HOME";
        }
        break;
        
      //Change season
      case 5:
        if (Winter){
          Response = "MODE : SUMMER";
          Winter = false;
          EEPROM.write(4,0);
        }
        else {
          Response = "MODE : WINTER";
          Winter = true;
          EEPROM.write(4,1);
        } 
        break;
        
      //Change hysteresis level of currently selected season
      case 6:
        if (Winter){
          Hysteresis_W += 1;
          if (Hysteresis_W > 6){
            Hysteresis_W = 1;
          }
          EEPROM.write(1,(Hysteresis_W));
          Response = "WINTER HYSTERESIS: ";
          Response += Hysteresis_W;
          Response += " DEG";
        }
        else{
          Hysteresis_S += 1;
          if (Hysteresis_S > 6){
            Hysteresis_S = 1;
          }
          EEPROM.write(2,(Hysteresis_S));
          Response = "YAZ HYSTERESIS: ";
          Response += Hysteresis_S;
          Response += " DEG";
          }
        break;

      // Correct faulty DHT11 readings
      case 7:
        TempCorrection +=1;
        if (TempCorrection > 5){
          TempCorrection = -5;
        }
        EEPROM.write(0, TempCorrection);
        Response = "TEMP CORRECTION: ";
        Response += TempCorrection;
        Response += " DEG";
        break;

      //Change minimum Winter temperature or maximum Summer temperature
      case 8:
        if(Winter){       // Winter minimum temperature
          TempMin += 2;
          if(TempMin > 68){
            TempMin = 10;
          }
          Response = "MINIMUM TEMP: ";
          Response += TempMin;
          EEPROM.write(7,(TempMin));
        }
        else{            // Summer maximum temperature
          TempMin += 2;
          if(TempMax > 32){
            TempMax = 78;
          }
          Response = "MAXIMUM TEMP: ";
          Response += TempMax;
          EEPROM.write(8,(TempMax));
       }
    }
    EEPROM.commit();
    Blynk.virtualWrite(V10, Response);
  }
}

// Reset the Menu at startup or after timing out from inactivity
void MenuReset(){
  MenuItem = 0;
  Blynk.virtualWrite(V10, String("HOLD 2 SEC FOR MENU"));
}


//**************************** Miscellaneous *********************************
//Retrieves saved values from EEPROM
void GetPresets(){
  TempCorrection = EEPROM.read(0);
  if ((TempCorrection < -5) || (TempCorrection > 5)){
    TempCorrection = 0;
    EEPROM.write(0, 0);
  }

  UsePerceivedTemp = EEPROM.read(5);
  if(UsePerceivedTemp){
    Blynk.setProperty(V0, "label", "             Perceived Temperature");
  }
  else{
    Blynk.setProperty(V0, "label", "               Actual Temperature");
  }

  Winter = EEPROM.read(4);
  Hysteresis_W = EEPROM.read(1);
  Hysteresis_S = EEPROM.read(2);

  if (!(Hysteresis_W >= 1) && !(Hysteresis_W <= 6)){
      Hysteresis_W = 2;
      EEPROM.write(1, Hysteresis_W);
  }
  if (!(Hysteresis_W >= 1) && !(Hysteresis_W <= 6)){
      Hysteresis_S = 2;
      EEPROM.write(2, Hysteresis_S);
  }
  
  TempDes = EEPROM.read(3);
  if (!(TempDes >= 10) && !(TempDes <= 32)){
    TempDes = 21;
    EEPROM.write(3, 21);
  }
  if(espalexaPointer != nullptr){ 
    espalexaPointer->setPercent(TempDes); 
  }

  TempMin = EEPROM.read(7);
  if(!(TempMin >= 10 && TempMin <= 32)){
    TempMin = 10;
    EEPROM.write(7, TempMin);
  }
  TempMax = EEPROM.read(8);
  if(!(TempMax >= 10 && TempMax <= 32)){
    TempMax = 32;
    EEPROM.write(8, TempMax);
  }
  EEPROM.commit();
}