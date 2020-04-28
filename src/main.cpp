#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <OneWire.h>
#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "file_driver.h"
#include <time.h>

OneWire  ds(13);
// Initialize Wifi connection to the router
char ssid[] = "zaima";              // your network SSID (name)
char password[] = "12345678";       // your network key

// Initialize Telegram BOT
#define BOTtoken "1049433601:AAGnNSDCjH5-sxqjQs0FapGEfOpw9L1CeRo"  //token of TestBOT

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

int Bot_mtbs = 1000; //mean time between scan messages
long Bot_lasttime;   //last time messages' scan has been done
bool Start = false;

const int ledPin = 33;
int ledStatus = 0;
float tempSol =0, tempPV = 0;

#define I2C_SCL 4
#define I2C_SDA 5
Adafruit_ADS1115 ads1115(0x48);  /* Use this for the 16-bit version */
float Plage = 2.048;

#define SDCS 15
File csvFile;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
struct tm timeinfo;

void printLocalTime()
{
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

bool isMoreDataAvailable(){
  return csvFile.available();
}

byte getNextByte(){
  return csvFile.read();
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    if (text == "/ledon") {
      digitalWrite(ledPin, HIGH);   // turn the LED on (HIGH is the voltage level)
      ledStatus = 1;
      bot.sendMessage(chat_id, "Led is ON", "");
    }

    if (text == "/ledoff") {
      ledStatus = 0;
      digitalWrite(ledPin, LOW);    // turn the LED off (LOW is the voltage level)
      bot.sendMessage(chat_id, "Led is OFF", "");
    }
    String str_vl="Temp DS18B20 SOl: " + String(tempSol);
    if (text == "/tempSol") {
      ledStatus = 0;
      //digitalWrite(ledPin, LOW);    // turn the LED off (LOW is the voltage level)
      bot.sendMessage(chat_id, str_vl, "");
    }
    str_vl="Temp DS18B20 PV: " + String(tempPV);
    if (text == "/tempPV") {
      ledStatus = 0;
      //digitalWrite(ledPin, LOW);    // turn the LED off (LOW is the voltage level)
      bot.sendMessage(chat_id, str_vl, "");
    }
    if(text == "/csv") {
      bot.sendMultipartFormDataToTelegram(
        "sendDocument", "file", "hist.csv", 
        "document/csv", chat_id, csvFile.size(),
        isMoreDataAvailable, 
        getNextByte);
    }
    
    if (text == "/status") {
      if(ledStatus){
        bot.sendMessage(chat_id, "Led is ON", "");
      } else {
        bot.sendMessage(chat_id, "Led is OFF", "");
      }
    }

    if (text == "/start") {
      String welcome = "Welcome to Universal Arduino Telegram Bot library, " + from_name + ".\n";
      welcome += "This is Flash Led Bot example.\n\n";
      welcome += "/ledon : to switch the Led ON\n";
      welcome += "/ledoff : to switch the Led OFF\n";
      welcome += "/status : Returns current status of LED\n";
      bot.sendMessage(chat_id, welcome, "Markdown");
    }
  }
}

void read_temp()
{
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius, fahrenheit;
  
  if ( !ds.search(addr)) {
    Serial.println("No more addresses.");
    Serial.println();
    ds.reset_search();
    delay(250);
    return;
  }
  
  Serial.print("ROM =");
  for( i = 0; i < 8; i++) {
    Serial.write(' ');
    Serial.print(addr[i], HEX);
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return;
  }
  Serial.println();
 
  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
      Serial.println("  Chip = DS18S20");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
      Serial.println("  Chip = DS18B20");
      type_s = 0;
      break;
    case 0x22:
      Serial.println("  Chip = DS1822");
      type_s = 0;
      break;
    default:
      Serial.println("Device is not a DS18x20 family device.");
      return;
  } 

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  Serial.print("  Data = ");
  Serial.print(present, HEX);
  Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" CRC=");
  Serial.print(OneWire::crc8(data, 8), HEX);
  Serial.println();

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.print(" Celsius, ");
  Serial.print(fahrenheit);
  Serial.println(" Fahrenheit");
  // if(addr[7]==0xCA) tempSol=celsius;
  // else if(addr[7]!=0xCA) tempPV=celsius;
  if(addr[7]==0x9C) tempSol=celsius;
  else if(addr[7]!=0x9C) tempPV=celsius;
}

void connect() {
  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
void setup() {
  Serial.begin(115200);
  connect();

  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  
  Wire.begin(I2C_SDA, I2C_SCL);
  ads1115.setGain(GAIN_FOUR);

  pinMode(ledPin, OUTPUT); // initialize digital ledPin as an output.
  digitalWrite(ledPin, LOW); // initialize pin as off

  if(!SD.begin(SDCS)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);

  if(!SD.exists("/data.csv")) {
    writeFile(SD, "/data.csv", "date, tempPV,tempSol,ensoleilment");
  }
}

float readLum() {
  int16_t adc0;
  float adc0Tension;
  float adc0Enso;
  
  adc0 = ads1115.readADC_SingleEnded(0);
  //adc0Tension = pow(2,16);

  adc0Tension = (1000.0*adc0*2.0*Plage)/(2.0*pow(2,16));
  adc0Enso = adc0Tension*10.0;
  
  Serial.print("AIN0: "); 
  Serial.println(adc0);
  Serial.print("Tension: "); 
  Serial.print(adc0Tension);
  Serial.println(" mV");
  Serial.print("Ensoleillement: "); 
  Serial.print(adc0Enso);
  Serial.println(" W/m2");
  Serial.println( );

  return adc0Enso;
}

int time_old, time_now;
char data[50];

void loop() {
  if (millis() > Bot_lasttime + Bot_mtbs)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while(numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    Bot_lasttime = millis();
  }
   time_now=millis();
  if(time_now-time_old > 5000)
  {
    time_old=time_now;
    for(uint8_t i = 0; i < 3; i++) {
      read_temp();
    }
    getLocalTime(&timeinfo);
    sprintf(data, "%d:%d:%d %d/%d/%d,%.2f,%.2f,%.2f", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, 
                                                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                                                  tempSol, tempPV, readLum());
    appendFile(SD, "/data.csv", data);
    readFile(SD, "/data.csv");
  }
}