#include <Wire.h>
#include "MAX30105.h" //sparkfun MAX3010X library
//#include <Adafruit_GFX.h>        //OLED libraries
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "heartRate.h"   
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>



const char* ssidAP = "mule-SP02";
const char* passwordAP = "";

AsyncWebServer server(80);

/////////////////////////////////////


int       numSamples;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="utf-8">
  <style>
    html {
     font-family: Arial;
     margin: 0px;
     text-align: left;
    }
    td {
      color:grey;      
      padding: 10px;
      }
    table {
      background-color:black;
      margin-top : 70px;
      margin-left: auto;
      margin-right: auto;
      }    
    body {
      background-color:black;      
      margin:0;
      padding:0;
      font-size: 2.0rem;
      }
    .field {
      font-size: 2.5rem;
      color : green;
      }
  </style>
</head>
<body>
<table>
  <tr>
    <td>朋友圈最屌血氧仪</td>
  </tr>
  <tr>
    <td>by mule</td>
  </tr>
</table>
<table>
  <tr>
    <td>血氧</td>
    <td class="field" id="spo2" align="right">%SPO2%</td>
    <td>&#37;</td>
  </tr>
  <tr>
    <td>脉搏</td>
    <td class="field" id="heartrate" align="right">%HEARTRATE%</td>
    <td>bpm</td>
  </tr>
</table>
</body>
<script>
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("spo2").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/spo2", true);
  xhttp.send();
}, 1000 ) ;
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("heartrate").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/heartrate", true);
  xhttp.send();
}, 1000 ) ;
</script>
</html>)rawliteral";




////////////////////////////////////
MAX30105 particleSensor;
///////////////////////////////////////////////////////////////////////////////////
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute;
int beatAvg;

double oxi;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); //Declaring the display name (display)
 ///////////////////////////////////////////////////////////////////////////////////

 

#define USEFIFO

String processor(const String& var){
    if(var == "SPO2"){
      return oxi > 0 ? String(oxi) : String("00.00");
      }
    else if(var == "HEARTRATE"){
      return beatAvg > 0 ? String(beatAvg) : String("00");
      }
    return String();
    }

void setup()
{
  //mule
  Serial.begin(115200);
  Serial.println();
  Serial.println("SPO2/Pulse meter");

  // ESP8266 tx power output 20.5dBm by default
  // we can lower this to reduce power supply noise caused by tx bursts
  WiFi.setOutputPower(12); 

  WiFi.softAP(ssidAP, passwordAP); 
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
     
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
    });
  server.on("/spo2", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", oxi > 0 ? String(oxi).c_str() : "00.00");
    });
  server.on("/heartrate", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", beatAvg > 0 ? String(beatAvg).c_str() : "00");
  });

  server.begin();
  //muleend
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); //Start the OLED display
  display.display();
  /////////////////////////////////////////////
  //Serial.begin(115200);
 // Serial.println("Initializing...");
 
  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30102 was not found. Please check wiring/power/solder jumper at MH-ET LIVE MAX30102 board. ");
    while (1);
  }
 
  //Setup to sense a nice looking saw tooth on the plotter
  //byte ledBrightness = 0x7F; //Options: 0=Off to 255=50mA
  byte ledBrightness = 70; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  //Options: 1 = IR only, 2 = Red + IR on MH-ET LIVE MAX30102 board
  int sampleRate = 200; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
  // Set up the wanted parameters
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
}
double avered = 0; double aveir = 0;
double sumirrms = 0;
double sumredrms = 0;
int i = 0;
int Num = 100;//calculate SpO2 by this sampling interval
 
double ESpO2 = 93.0;//initial value of estimated SpO2
double FSpO2 = 0.7; //filter factor for estimated SpO2
double frate = 0.95; //low pass filter for IR/red LED value to eliminate AC component
#define TIMETOBOOT 2000 // wait for this time(msec) to output SpO2
#define SCALE 88.0 //adjust to display heart beat and SpO2 in the same scale
#define SAMPLING 5 //if you want to see heart beat more precisely , set SAMPLING to 1
#define FINGER_ON 35000 // if red signal is lower than this , it indicates your finger is not on the sensor
#define MINIMUM_SPO2 0.0
void loop()
{
 
  uint32_t ir, red , green;
  double fred, fir;
  double SpO2 = 0; //raw SpO2 before low pass filtered
 
#ifdef USEFIFO
  particleSensor.check(); //Check the sensor, read up to 3 samples
 
  while (particleSensor.available()) {//do we have new data
#ifdef MAX30105
   red = particleSensor.getFIFORed(); //Sparkfun's MAX30105
    ir = particleSensor.getFIFOIR();  //Sparkfun's MAX30105
#else
    red = particleSensor.getFIFOIR(); //why getFOFOIR output Red data by MAX30102 on MH-ET LIVE breakout board
    ir = particleSensor.getFIFORed(); //why getFIFORed output IR data by MAX30102 on MH-ET LIVE breakout board
#endif
    i++;
    fred = (double)red;
    fir = (double)ir;
    avered = avered * frate + (double)red * (1.0 - frate);//average red level by low pass filter
    aveir = aveir * frate + (double)ir * (1.0 - frate); //average IR level by low pass filter
    sumredrms += (fred - avered) * (fred - avered); //square sum of alternate component of red level
    sumirrms += (fir - aveir) * (fir - aveir);//square sum of alternate component of IR level    
    if ((i % SAMPLING) == 0) {//slow down graph plotting speed for arduino Serial plotter by thin out
      if ( millis() > TIMETOBOOT) {
        if (ir < FINGER_ON) ESpO2 = MINIMUM_SPO2; //indicator for finger detached
        if (ESpO2 <= -1)
        {
          ESpO2 = 0;
        }

        if (ESpO2 > 100)
        {
          ESpO2 = 100;
        }

        oxi = ESpO2;
        
        //Serial.print(" Oxygen % = ");
        //Serial.println(ESpO2); //low pass filtered SpO2

      }
    }
    if ((i % Num) == 0) {
      double R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
      // Serial.println(R);
      SpO2 = -23.3 * (R - 0.4) + 100; //http://ww1.microchip.com/downloads/jp/AppNotes/00001525B_JP.pdf
      ESpO2 = FSpO2 * ESpO2 + (1.0 - FSpO2) * SpO2;//low pass filter
      //  Serial.print(SpO2);Serial.print(",");Serial.println(ESpO2);
      //Serial.print(" Oxygen2 % = ");
      //Serial.println(ESpO2); //low pass filtered SpO2
      sumredrms = 0.0; sumirrms = 0.0; i = 0;
      break;
    }
    particleSensor.nextSample(); //We're finished with this sample so move to next sample
    //Serial.println(SpO2);

       //////////////////////////////////////////////////////////////////////////////
    long irHR = particleSensor.getIR();
    if(irHR > 7000){                                           //If a finger is detected               
      display.clearDisplay();                                   //Clear the display
      //display.drawBitmap(5, 5, logo2_bmp, 24, 21, WHITE);       //Draw the first bmp picture (little heart)
      display.setTextSize(1);                                   //Near it display the average BPM you can display the BPM if you want
      display.setTextColor(WHITE); 
      display.setCursor(45,2);  
      display.setRotation(3);               
      display.println("HR"); 
      display.setRotation(0);
      display.setTextSize(3);               
      display.setCursor(20,5);                
      display.println(beatAvg); 
      display.setCursor(107,5);
      display.setTextSize(1); 
      display.print("bpm"); 

      //display.drawBitmap(5, 5, logo2_bmp, 24, 21, WHITE);       //Draw the first bmp picture (little heart)
      display.setTextSize(1);                                   //Near it display the average BPM you can display the BPM if you want
      display.setTextColor(WHITE); 
      display.setRotation(3); 
      display.setCursor(5,2);              
      display.println("SpO2");    
      display.setTextSize(3);  
      display.setRotation(0);         
      display.setCursor(20,35);                
      display.println(oxi,1); 
      display.setCursor(120,35);
      display.setTextSize(1); 
      display.print("%"); 
      display.display();
      //delay(100);
      
      
    if (checkForBeat(irHR) == true)                        //If a heart beat is detected
    {              
      display.clearDisplay();                                //Clear the display
      //display.drawBitmap(0, 0, logo3_bmp, 32, 32, WHITE);    //Draw the second picture (bigger heart)
      display.setTextSize(1);                                //And still displays the average BPM
      display.setTextColor(WHITE);  
      display.setRotation(3);            
      display.setCursor(45,2);                
      display.println("HR");  
      display.setRotation(0);
      display.setTextSize(3);              
      display.setCursor(20,5);                
      display.println(beatAvg); 
      display.setCursor(107,5);
      display.setTextSize(1); 
      display.print("bpm"); 
          

      //display.clearDisplay();                                   //Clear the display
      //display.drawBitmap(5, 5, logo2_bmp, 24, 21, WHITE);       //Draw the first bmp picture (little heart)
      display.setTextSize(1);                                   //Near it display the average BPM you can display the BPM if you want
      display.setTextColor(WHITE); 
      display.setCursor(5,2);    
      display.setRotation(3);            
      display.println("SpO2");
      display.setTextSize(3);     
      display.setRotation(0);         
      display.setCursor(20,35);                
      display.println(oxi,1); 
      display.setCursor(120,35);
      display.setTextSize(1); 
      display.print("%");
      display.display();
      //tone(3,1000);                                        //And tone the buzzer for a 100ms you can reduce it it will be better
      delay(100);
      //noTone(3);   
      //delay(100);
      //Deactivate the buzzer to have the effect of a "bip"
      //We sensed a beat!
      long delta = millis() - lastBeat;                   //Measure duration between two beats
      lastBeat = millis();
  
      beatsPerMinute = 60 / (delta / 1000.0);           //Calculating the BPM
  
      if (beatsPerMinute < 255 && beatsPerMinute > 20)               //To calculate the average we strore some values (4) then do some math to calculate the average
      {
        rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
        rateSpot %= RATE_SIZE; //Wrap variable
  
        //Take average of readings
        beatAvg = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
  
  }
  if (irHR < 7000){       //If no finger is detected it inform the user and put the average BPM to 0 or it will be stored for the next measure
     beatAvg=0;              
     beatAvg=0;
     display.clearDisplay();
     display.setTextSize(2);                    
     display.setTextColor(WHITE);             
     display.setCursor(15,7);                
     display.println("SpO2 YYDS"); 
     display.setCursor(30,35);
     display.setTextSize(3);  
     display.println("MULE");  
     display.display();
     //noTone(3);
     }
    
////////////////////////////////////////////////////////////////
  }
#endif
 
}
