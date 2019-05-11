#include <Time.h>
#include <TimeLib.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <DS3231.h>
#include <EEPROM.h>
#include <NTPClient.h>

//MQTT server parameters
#define mqtt_server "m14.cloudmqtt.com" //MQTT URL
#define mqtt_port 19348                      //MQTT Port
#define mqtt_user "vidaaruu" //mqtt user
#define mqtt_password "Ro2sY3zEhY9W" //mqtt password

//Valve(Relay) Pin 
#define Valve_1 D0

//Line Notify Token
#define LINE_TOKEN "kVKIYlfPSY2czv1oIp96KgdtTFjv90FesQ01qhtCJnL"

//Global vairable for NTP time 
bool h12;                       //24h/12h flag indicator
bool PM;                        //AM/PM flag indicator
struct tm* p_tm;                //Struct variable for NTP time
String time_n;      
int timezone = 7*3600;        //GMT+7 Time zone
int dst = 0;                    //Date Swing Time
bool alarmFlag = false;         //set when 1st alarm, clear when 2nd alarm
const long utcOffsetInSeconds = 7 * 3600;

//Global variable for watering timer
byte W_Hour;
byte W_Minute;
byte W_Duration;
byte W_Date;
byte W_DoW;

//WIFI connection setting
const char* ssid     = "YOKEAP_2.4G";       //Wifi Name
const char* password = "YP24162531";   //Wifi Password

//Boolean flag for toggle
bool valve = false;             //set when 1st alarm, clear when 2nd alarm
bool flagActive = false;

WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 4);
DS3231 Clock;


char temp[20];                  //variable for used as sprintf argument
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

/** RTC print out to LCD and Serial, and string return with time **/
String print_RTC(){
  bool Century;
  char chDate[20];
  char chTime[20];

    sprintf(chDate, "%02d:%02d:%02d", Clock.getDate(), Clock.getMonth(Century), Clock.getYear() + 2000);
    sprintf(chTime, "%02d:%02d:%02d", Clock.getHour(h12, PM), Clock.getMinute(), Clock.getSecond()); 

//    lcd.setCursor(6, 1);
//    lcd.print(chDate);
    lcd.setCursor(6,1);
    lcd.print(chTime);
    //Serial.print(chDate);
    //Serial.print(" | ");
    Serial.println(chTime);
    return chTime;
}

/******************** set RTC with NTP time *********************/
bool set_clock(String msg){
  boolean GotString = false;
  char InChar;
  byte Temp1, Temp2;
  char InString[20];
  byte j=0;
  int intTrying = 0;
  
   if(msg == "T.pulling") {
    configTime(timezone, dst, "time.nist.gov", "pool.ntp.org"); //Query time server.
    Serial.println("\nLoading time");
    while (!time(nullptr)) {
      Serial.print("*");
      delay(1000);
      if(intTrying > 10) {
        print_RTC();
        lcd.setCursor(9, 0);
        Serial.println("NTP FAIL");
        lcd.print("NTP FAIL");
        return false;
      }
      intTrying++;
    }

    time_t now = time(nullptr);
    tm* p_tm = localtime(&now);

    Serial.print(p_tm->tm_mday);
    Serial.print(':');
    Serial.print(p_tm->tm_min);
    Serial.print(':');
    Serial.println(p_tm->tm_sec);

    Clock.setClockMode(false);  // set to 24h
    Clock.setDoW((byte)p_tm->tm_wday);
    Clock.setDate((byte)p_tm->tm_mday);
    Clock.setMonth((byte)p_tm->tm_mon + 1);
    Clock.setYear((byte)p_tm->tm_year % 100);
    Clock.setHour((byte)p_tm->tm_hour);
    Clock.setMinute((byte)p_tm->tm_min);
    Clock.setSecond((byte)p_tm->tm_sec);

    Serial.println("RTC has been set with NTP");
    print_RTC();
    return true;
  }
  else{
    print_RTC();
  }
}

/***************** Waterting time record to EEPROM *********************/
bool RecordWateringWTime(){
  EEPROM.write(0, W_Hour);
  EEPROM.write(1, W_Minute);
  EEPROM.write(2, W_Duration);
  EEPROM.commit();
  delay(20);
  //setWateringTime();
  return true;
}

bool ReadWateringTime(){
  W_Hour = EEPROM.read(0);
  W_Minute = EEPROM.read(1);
  W_Duration = EEPROM.read(2);
  W_Date = Clock.getDate();
  W_DoW = Clock.getDoW();
  return true;
}

/************** RTC alarm set from EEPROM (Watering time) ****************/
bool setWateringTime(byte offset){
  bool h12;
  bool PM;
  char buf[20];
  /*
  //for debugging
  W_Hour = Clock.getHour(h12, PM);
  W_Minute = Clock.getMinute() + offset;
  //W_Duration = EEPROM.read(2);
  //Date = Clock.getDate();
  W_DoW = Clock.getDoW();
  */
  ReadWateringTime();
  if(Clock.getHour(h12, PM) > W_Hour) {
    if(W_DoW <= 6) W_DoW++;
    if(W_DoW >6) W_DoW = 0;
  }
  
  // set A1 to one minute past the time we just set the clock
  // on current day of week.
  Clock.setA1Time(W_DoW, W_Hour, W_Minute + offset, 00, 0x0, true, false, false);
  Clock.turnOnAlarm(1);
  //Clock.setA2Time(Date, Clock.getHour(h12, PM), Clock.getMinute()+ 1, 0x0, false, false, false);
  //Clock.turnOnAlarm(2);
  //sprintf(buf, "%02d:%02d:%02d:00", W_DoW, W_Hour, W_Minute + offset);
  sprintf(buf, "%02d:%02d:00", W_Hour, W_Minute + offset);
  lcd.setCursor(6,2);
  //lcd.setCursor(4,2);
  lcd.print(buf);
  delay(100);
  return true;
}

void setup(){
  int tryout;
  bool flagWifi;
  pinMode(Valve_1, OUTPUT);
  digitalWrite(Valve_1, 1);
  Serial.begin(115200);
  EEPROM.begin(10);

  lcd.begin();
  lcd.home();
  
  Wire.begin(D2, D1);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.print(".");
    if(tryout > 20) {
      lcd.clear();
      lcd.home();
      lcd.print("Offline");
      flagWifi = true;
      break;
    }
  }


  if(flagWifi){
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.home();
    lcd.print(WiFi.localIP());
    delay(100);
  }
  

  //Setting RTC with NTP pulling
  //lcd.setCursor(0,1);
  //lcd.print("DATE ");
  lcd.setCursor(0,1);
  lcd.print("TIME ");
  lcd.setCursor(0,2);
  lcd.print("WT: ");
  delay(10);

  //set RTC and watering time
  set_clock("T.pulling");
  //ReadWateringTime();
  setWateringTime(W_Duration);

  //External INT from RTC for alarming
  pinMode(D4, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(D4), AlarmINT, FALLING);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void Line_Notify(String message) {
  WiFiClientSecure client;

  if (!client.connect("notify-api.line.me", 443)) {
    Serial.println("connection failed");
    return;   
  }

  String req = "";
  req += "POST /api/notify HTTP/1.1\r\n";
  req += "Host: notify-api.line.me\r\n";
  req += "Authorization: Bearer " + String(LINE_TOKEN) + "\r\n";
  req += "Cache-Control: no-cache\r\n";
  req += "User-Agent: Yokeap\r\n";
  req += "Content-Type: application/x-www-form-urlencoded\r\n";
  req += "Content-Length: " + String(String("message=" + message).length()) + "\r\n";
  req += "\r\n";
  req += "message=" + message;
  // Serial.println(req);
  client.print(req);
    
  delay(20);

  // Serial.println("-------------");
  while(client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break;
    }
    //Serial.println(line);
  }
  Serial.println("-------------");
}

void reconnect(){
  while(!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    lcd.setCursor(0,3);
    lcd.print("MQTT...");
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
      lcd.print("Connected");
      client.subscribe("/ESP/REMOTE");    //MQTT Subscribed
      client.publish("/ESP/REMOTE", "YP00 is started");
      delay(1000);
      lcd.setCursor(0,3);
      lcd.print("                 ");
      delay(10);
      lcd.setCursor(0,3);
      lcd.print("Valve: ");
      delay(1000);
//      lcd.setCursor(0,3);
//      lcd.print("                 ");
//      lcd.print("System is online");
    }
    else{
      Serial.print("failed, rc=");
      Serial.print(client.state());
      lcd.setCursor(7,3);
      lcd.print("failed");
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  ESP.wdtFeed();
  time_n = print_RTC();
  if((alarmFlag) && (!flagActive)) {
    //Clock.setA2Time(Date, Clock.getHour(h12, PM), Clock.getMinute() + 1, 0x0, false, false, false);
    //Clock.turnOnAlarm(2);
    lcd.setCursor(3,2);
    lcd.print("OFF");
    setWateringTime(W_Duration);
    valveActive(true);
    flagActive = true;
  }
  if(!alarmFlag){
    digitalWrite(Valve_1, true);
    lcd.setCursor(6,3);
    lcd.print("Off");
  }
  if(flagActive){
    if(!alarmFlag) {
      lcd.setCursor(3,2);
      lcd.print("ON ");
      flagActive = false; 
      valveActive(false);
    }
  }
  delay(100);
} 

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String msg = "";
  String inString = "";
  int i=0;
  int index = 0;
  int n = 0;
  byte value[3];
  bool setFlag = false;
  while (i<length) {
    msg += (char)payload[i++];
  }
  
  Serial.println(msg);
  if (msg == "On") {  
    alarmFlag = true;
    valveActive(true);
    return;
  }
  if (msg == "Off") {  
    alarmFlag = false;
    valveActive(false);
    return;
  }

  int count = 0;
  if(msg.charAt(0) == 'S'){
    while(msg.charAt(index) != '\n'){
    if (isDigit(msg.charAt(index))){
        inString += msg.charAt(index);
        count = 0;
    }
    if ((msg.charAt(index)  == ':') || (msg.charAt(index)  == 'x')){
      value[n] = (byte)inString.toInt();
      inString = "";
      n++;
      count = 0;
    }
     index++;
     count++;
     setFlag = true;
     if(count > 1000) {
        // clear the string for new input:
        inString = "";

        //increase Thruster Position
        n++;
        setFlag = false;
        break;
      }
    }   
  }
  if(setFlag){
      setFlag = false;
      W_Hour = (byte)value[0];
      W_Minute = (byte)value[1];
      W_Duration = (byte)value[2];
      RecordWateringWTime();
      delay(20);
      setWateringTime(0);
      Serial.print("Value");
      sprintf(temp, "%02d, %02d, %02d", W_Hour, W_Minute, W_Duration);
      Serial.println(temp);
  }
}

void valveActive(bool active){
  if(active){
    digitalWrite(Valve_1, false);
    Serial.println("Valve: On");
    lcd.setCursor(6,3);
    lcd.print("On ");
    Line_Notify("Valve has been opened!");
  }
  else{
    digitalWrite(Valve_1, true);
    Serial.println("Valve: Off");
    lcd.setCursor(6,3);
    lcd.print("Off");
    Line_Notify("Valve has been closed!"); 
  }
  
}

void AlarmINT(){
  Clock.clearAlarm1();
  alarmFlag = !alarmFlag;
}
