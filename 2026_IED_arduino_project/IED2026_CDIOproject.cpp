#include <SoftwareSerial.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>

#define motor_LF 3
#define motor_LB 5
#define motor_RB 6
#define motor_RF 9
#define IR1Pin A0 // for left
#define IR2Pin A1 // for right
#define DHTPIN 2
#define PIR 4
#define ESP_RX 12
#define ESP_TX 13

#define DHTTYPE DHT11
const char* ssid = "replace with ssid";
const char* password = "password";
const char* writeAPIKey = "APIKey";

// ========== VALUES/THRESHOLDS ==========
#define LIGHT_LOW 50
#define LIGHT_HIGH 1000
#define LowHumi 40
#define HighHumi 70

// ========== OBJECTS ==========
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);
SoftwareSerial esp8266(ESP_RX, ESP_TX);

byte smiley[8] = {0b00000, 0b00000, 0b01010, 0b00000, 0b00000, 0b10001, 0b01110, 0b00000};
byte sad[8] = {0b00000, 0b00000, 0b01010, 0b00000, 0b00000, 0b01110, 0b10001, 0b00000};
byte Sun_1[8] = {0b00000, 0b01000, 0b00100, 0b10010, 0b01000, 0b00001, 0b11011, 0b00000};
byte Sun_2[8] = {0b00000, 0b10000, 0b10001, 0b10010, 0b00100, 0b10000, 0b11011, 0b00000};
byte too_warm1[8] = {0b10000, 0b01000, 0b00100, 0b10010, 0b01000, 0b00001, 0b11011, 0b00000};
byte too_warm2[8] = {0b10000, 0b10000, 0b10001, 0b10010, 0b00100, 0b10000, 0b11011, 0b00000};
byte snowflake[8] = {0b00100, 0b10101, 0b01110, 0b11111, 0b01110, 0b10101, 0b00100, 0b00000};
byte X[8] = {0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b00000, 0b00000};

// ========== VARIABLES ==========
bool isAutoMode = false;
bool PIRstop;
int controller_speed = 0;
int controller_direction = 0;
uint16_t lightLevel = 0;
unsigned long lastSensorRead = 0;
unsigned long lastThingSpeakUpdate = 0;
float t;
float h;
unsigned long TempTime = 0;
unsigned long HumiTime = 0;
int value;

// ========== ERROR TRACKING ==========
int dhtFailCount = 0;
int tslFailCount = 0;
bool safeMode = false;

// ========== BUFFER FOR ESP DATA ==========
String espBuffer = "";

// ========== THINGSPEAK STATE MACHINE ==========
int tsState = 0;
unsigned long tsStateTime = 0;
String tsHttpRequest = "";

// ========== NON-BLOCKING LCD DISPLAY TIMERS ==========
unsigned long lcdDisplayStart = 0;
int lcdDisplayStep = 0;
bool lcdDisplayActive = false;


void setup() {
  Serial.begin(9600);
  esp8266.begin(9600);
  Serial.println(F("\n=== Bot Starting ==="));

  pinMode(IR1Pin, INPUT);
  pinMode(IR2Pin, INPUT);
  pinMode(motor_RB, OUTPUT);
  pinMode(motor_RF, OUTPUT);
  pinMode(motor_LF, OUTPUT);
  pinMode(motor_LB, OUTPUT);
  pinMode(PIR, INPUT);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Initializing..."));

  lcd.createChar(0, smiley);
  lcd.createChar(1, sad);
  lcd.createChar(2, too_warm1);
  lcd.createChar(3, too_warm2);
  lcd.createChar(4, Sun_1);
  lcd.createChar(5, Sun_2);
  lcd.createChar(6, snowflake);
  lcd.createChar(7, X);

  dht.begin();

  if (!tsl.begin()) {
    Serial.println(F("TSL2561 not found!"));
    tslFailCount = 5;
  } else {
    Serial.println(F("TSL2561 detected!"));
    tsl.enableAutoRange(true);
    tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);
  }

  initializeESP8266();

  lcd.clear();
  lcd.print(F("Ready!"));
  delay(1000);

  Serial.println(F("\n=== WAITING FOR COMMANDS ==="));
}

void loop() {
  checkESP8266Data();
  processThingSpeak();

  if (millis() - lastSensorRead >= 1000) {
    lastSensorRead = millis();
    readlight();

    value = digitalRead(PIR);
    if(value == HIGH){
      PIRstop = true;
      //brake();
      Pir_LCD();
    }
    else{
      PIRstop = false;
      temp_humi();
    }

  }

  if (millis() - lastThingSpeakUpdate >= 15000) {
    if (tsState == 0) {
      lastThingSpeakUpdate = millis();
      startThingSpeak();
    }
  }

  if (!PIRstop) {
   if (isAutoMode) {
     autoMode();
   } 
   else {
     manualMode();
   }
  }
  else {
    brake();
  }
}

// ========== ESP8266 ==========
String getESPResponse(int timeout) {
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (esp8266.available()) {
      resp += (char)esp8266.read();
    }
  }
  return resp;
}

void clearESPBuffer() {
  delay(500);
  while (esp8266.available()) esp8266.read();
}

void initializeESP8266() {
  Serial.println(F("\n=== ESP8266 INIT ===\n"));

  // 1. Reset
  Serial.println(F("1. Reset..."));
  esp8266.println(F("AT+RST"));
  delay(5000);
  while (esp8266.available()) esp8266.read();
  delay(1000);
  while (esp8266.available()) esp8266.read();

  // 2. Test AT
  Serial.println(F("2. Test AT..."));
  esp8266.println(F("AT"));
  String resp = getESPResponse(3000);
  if (resp.indexOf(F("OK")) == -1) {
    Serial.println(F("FAIL: No AT response"));
    safeMode = true;
    return;
  }
  Serial.println(F("OK"));
  clearESPBuffer();

  // 3. Set station mode
  Serial.println(F("3. Station mode..."));
  esp8266.println(F("AT+CWMODE=1"));
  resp = getESPResponse(2000);
  Serial.println(F("OK"));
  clearESPBuffer();
  delay(1000);

  // 4. Connect to WiFi
  Serial.println(F("4. Connecting WiFi..."));
  lcd.clear();
  lcd.print(F("Connecting..."));

  String connectCmd = "AT+CWJAP=\"";
  connectCmd += ssid;
  connectCmd += "\",\"";
  connectCmd += password;
  connectCmd += "\"";

  esp8266.println(connectCmd);

  // Wait up to 20s, watching for WIFI GOT IP
  unsigned long startTime = millis();
  resp = "";
  bool connected = false;

  while (millis() - startTime < 20000) {
    while (esp8266.available()) {
      char c = esp8266.read();
      resp += c;
      Serial.write(c);
    }
    if (resp.indexOf(F("WIFI GOT IP")) != -1) {
      connected = true;
      break;
    }
    if (resp.indexOf(F("FAIL")) != -1) {
      break;
    }
    delay(100);
  }

  if (!connected) {
    Serial.println(F("\nFAIL: WiFi not connected"));
    Serial.println(F("Check: Hotspot ON, 2.4GHz, SSID, password"));
    lcd.clear();
    lcd.print(F("WiFi Failed!"));
    safeMode = true;
    return;
  }

  Serial.println(F("\nWiFi connected!"));
  delay(1000);
  while (esp8266.available()) esp8266.read();

  // 5. Get IP
  Serial.println(F("5. Get IP..."));
  esp8266.println(F("AT+CIFSR"));
  resp = getESPResponse(3000);
  Serial.println(resp);

  int staipIndex = resp.indexOf(F("STAIP,\""));
  if (staipIndex >= 0) {
    int startQuote = staipIndex + 7;
    int endQuote = resp.indexOf("\"", startQuote);
    String ip = resp.substring(startQuote, endQuote);
    Serial.println(F("\n*** YOUR IP ***"));
    Serial.println(ip);
    Serial.println(F("***~~~~~~~~~~~~"));
    lcd.clear();
    lcd.print(F("WiFi OK!"));
    lcd.setCursor(0, 1);
    lcd.print(ip);
  }
  clearESPBuffer();

  // 6. Enable MUX
  Serial.println(F("6. Enable MUX..."));
  esp8266.println(F("AT+CIPMUX=1"));
  delay(1000);
  clearESPBuffer();

  // 7. Start UDP
  Serial.println(F("7. Start UDP..."));
  esp8266.println(F("AT+CIPSTART=0,\"UDP\",\"0.0.0.0\",0,8888,2"));
  delay(2000);
  clearESPBuffer();

  Serial.println(F("\n=== READY! ===\n"));
}

void checkESP8266Data() {
  while (esp8266.available()) {
    char c = esp8266.read();
    espBuffer += c;
  }

  while (true) {
    int ipdIndex = espBuffer.indexOf(F("+IPD"));
    if (ipdIndex == -1) break;

    int colonIndex = espBuffer.indexOf(":", ipdIndex);
    if (colonIndex == -1) break;

    int commaIndex = espBuffer.indexOf(",", ipdIndex);
    int secondCommaIndex = espBuffer.indexOf(",", commaIndex + 1);
    int dataLength = espBuffer.substring(secondCommaIndex + 1, colonIndex).toInt();

    int dataStart = colonIndex + 1;
    int dataEnd = dataStart + dataLength;

    if (dataEnd > (int)espBuffer.length()) break;

    String command = espBuffer.substring(dataStart, dataEnd);
    command.trim();

    Serial.print(F("CMD: "));
    Serial.println(command);

    handleCommand(command);

    espBuffer = espBuffer.substring(dataEnd);
  }

  if (espBuffer.length() > 100) {
    espBuffer = "";
  }
}

// ========== HANDLE COMMANDS ==========
void handleCommand(String cmd) {
  if (cmd == F("TOGGLE")) {
    isAutoMode = !isAutoMode;
    controller_speed = 0;
    controller_direction = 0;
    brake();
    Serial.println(isAutoMode ? F("-> AUTO") : F("-> MANUAL"));
    lcd.clear();
    lcd.print(isAutoMode ? F("AUTO") : F("MANUAL"));
    delay(500);
  }
  else if (cmd.startsWith(F("DIR:"))) {
    controller_direction = cmd.substring(4).toInt();
  }
  else if (cmd.startsWith(F("SPD:"))) {
    controller_speed = cmd.substring(4).toInt();
  }
}

// ========== SENSORS ==========
void readlight() {
  sensors_event_t event;
  tsl.getEvent(&event);
  if (event.light > 0 && event.light < 40000) {
    lightLevel = event.light;
    tslFailCount = 0;
  } else {
    tslFailCount++;
  }
}

String categorizeLightLevel() {
  if (lightLevel < LIGHT_LOW) return F("Low");
  else if (lightLevel > LIGHT_HIGH) return F("VBright");
  else return F("Normal");
}

void temp_humi(){
    t=dht.readTemperature(); 
    h=dht.readHumidity();

    if(isnan(t)||isnan(h)){
      if (!lcdDisplayActive) {
        lcdDisplayActive = true;
        lcdDisplayStart = millis();
        lcdDisplayStep = 0;
      }
      
      unsigned long currentTime = millis();
      unsigned long elapsed = currentTime - lcdDisplayStart;
      
      if (lcdDisplayStep == 0 && elapsed < 1000) {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print(" Sensor Error");
        cross();
      }
      else if (lcdDisplayStep == 0 && elapsed >= 1000) {
        lcdDisplayActive = false;
      }
    }
    else{
      temp_humi_lcd();
      unsigned long currentTime = millis();
    
      // Read temperature every 1 second
      if (currentTime - TempTime >= 1000) {
      temperature();
      TempTime = currentTime;
      }

      // Read humidity every 2 seconds
     if (currentTime - HumiTime >= 2000) {
        humidity();

        HumiTime = currentTime;
      }
    }

}

void temperature()
  {
      if (!lcdDisplayActive) {
        lcdDisplayActive = true;
        lcdDisplayStart = millis();
        lcdDisplayStep = 0;
      }
      
      unsigned long currentTime = millis();
      unsigned long elapsed = currentTime - lcdDisplayStart;
      
      if ((t<23)||(t>35))
      {
        if (lcdDisplayStep == 0 && elapsed < 1000) {
          lcd.clear();
          lcd.setCursor(0,0);                                       
          lcd.print(" Bad Weather");
          drawSad();
        }
        else if (lcdDisplayStep == 0 && elapsed >= 1000) {
          lcdDisplayActive = false;
        }
      }
      if (t<23)
      {
        if (lcdDisplayStep == 0 && elapsed < 1000) {
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("  Freezing!");
          drawSnowflake();
        }
        else if (lcdDisplayStep == 0 && elapsed >= 1000) {
          lcdDisplayActive = false;
        }

      }
      else if(t>35)
      {
        if (lcdDisplayStep == 0 && elapsed < 1000) {
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("  Too Warm !");
          Too_Sunny();
        }
        else if (lcdDisplayStep == 0 && elapsed >= 1000) {
          lcdDisplayActive = false;
        }
      }
      else
      {
        if (lcdDisplayStep == 0 && elapsed < 1000) {
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("  Sunny!");
          drawSmiley();
        }
        else if (lcdDisplayStep == 0 && elapsed >= 1000) {
          lcdDisplayActive = false;
        }
      }
}

void humidity()
  {
    if (!lcdDisplayActive) {
      lcdDisplayActive = true;
      lcdDisplayStart = millis();
      lcdDisplayStep = 0;
    }
    
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - lcdDisplayStart;
    
    if (h>HighHumi)
    {
      if (lcdDisplayStep == 0 && elapsed < 1000) {
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print(" HIGH HUMI");
      }
      else if (lcdDisplayStep == 0 && elapsed >= 1000) {
        lcdDisplayStep = 1;
        lcdDisplayStart = millis();
      }
      else if (lcdDisplayStep == 1 && elapsed < 1000) {
        lcd.write(byte(1));
      }
      else if (lcdDisplayStep == 1 && elapsed >= 1000) {
        lcdDisplayStep = 2;
        lcdDisplayStart = millis();
      }
      else if (lcdDisplayStep == 2 && elapsed < 1000) {
        lcd.print("NEED MONITORING!");
      }
      else if (lcdDisplayStep == 2 && elapsed >= 1000) {
        lcdDisplayActive = false;
      }
    }
    else if (h<LowHumi){
      if (lcdDisplayStep == 0 && elapsed < 1000) {
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print(" LOW HUMI");
        lcd.write(byte(1));
      }
      else if (lcdDisplayStep == 0 && elapsed >= 1000) {
        lcdDisplayStep = 1;
        lcdDisplayStart = millis();
      }
      else if (lcdDisplayStep == 1 && elapsed < 1000) {
        lcd.print("  Watering...");
      }
      else if (lcdDisplayStep == 1 && elapsed >= 1000) {
        lcdDisplayActive = false;
      }

    } else {
      if (lcdDisplayStep == 0 && elapsed < 1000) {
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print(" OPTIMAL HUMI");
      }
      else if (lcdDisplayStep == 0 && elapsed >= 1000) {
        lcdDisplayStep = 1;
        lcdDisplayStart = millis();
      }
      else if (lcdDisplayStep == 1 && elapsed < 1000) {
        lcd.write(byte(0));
      }
      else if (lcdDisplayStep == 1 && elapsed >= 1000) {
        lcdDisplayActive = false;
      }
    }
  
}


// ========== LCD ==========
/*void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("T:"));
  lcd.print((int)temperature);
  lcd.print(F("C H:"));
  lcd.print((int)humidity);
  lcd.print(F("%"));

  lcd.setCursor(0, 1);
  lcd.print(isAutoMode ? F("AUTO") : F("MANUAL"));
  lcd.print(F(" L:"));
  lcd.print(categorizeLightLevel().substring(0, 3));
} */

void temp_humi_lcd()
  {
  Serial.print("Temp: ");
  Serial.print(t);
  Serial.print(" C\t");
  Serial.print("Hum: ");
  Serial.print(h);
  Serial.println(" %");

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(t);
  lcd.print(" C ");
  lcd.setCursor(0,1); 
  lcd.print(h);
  lcd.print(" %   ");
}

void Pir_LCD(){
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Break !!!"); 
    lcd.setCursor(0,1);
    lcd.print(" Blocked!!");
}

void drawSmiley()
  {
  lcd.setCursor(10, 0);
  lcd.write(byte(0));
  }

void drawSad()
  { 
  lcd.setCursor(14, 0);
  lcd.write(byte(1));
  }

void Sunny()
  {
  lcd.setCursor(11, 0);
  lcd.write(byte(2));
  lcd.write(byte(3));
  }


void Too_Sunny()
  {
  lcd.setCursor(14, 0);
  lcd.write(byte(4));
  lcd.write(byte(5));
  }

void drawSnowflake()
  {
  lcd.setCursor(12, 0);
  lcd.write(byte(6));
  }

void cross() {
  lcd.setCursor(15, 0);
  lcd.write(byte(7));
}

// ========== THINGSPEAK ==========
void startThingSpeak() {
  if (safeMode) return;

  tsHttpRequest = "GET /update?api_key=";
  tsHttpRequest += writeAPIKey;
  tsHttpRequest += "&field1=";
  tsHttpRequest += String(t);
  tsHttpRequest += "&field2=";
  tsHttpRequest += String(h);
  tsHttpRequest += "&field3=";
  tsHttpRequest += String(lightLevel);
  tsHttpRequest += " HTTP/1.1\r\nHost: api.thingspeak.com\r\n\r\n";

  tsState = 1;
  tsStateTime = millis();
  Serial.println(F("ThingSpeak: starting..."));
}

void processThingSpeak() {
  if (tsState == 0) return;

  unsigned long now = millis();

  switch (tsState) {
    case 1:
      esp8266.println(F("AT+CIPSTART=1,\"TCP\",\"api.thingspeak.com\",80"));
      tsState = 2;
      tsStateTime = now;
      break;

    case 2:
      if (now - tsStateTime >= 2000) {
        while (esp8266.available()) esp8266.read();
        tsState = 3;
        tsStateTime = now;
      }
      break;

    case 3:
      {
        String cipSend = "AT+CIPSEND=1,";
        cipSend += String(tsHttpRequest.length());
        esp8266.println(cipSend);
        tsState = 4;
        tsStateTime = now;
      }
      break;

    case 4:
      if (now - tsStateTime >= 500) {
        while (esp8266.available()) esp8266.read();
        tsState = 5;
        tsStateTime = now;
      }
      break;

    case 5:
      esp8266.print(tsHttpRequest);
      tsState = 6;
      tsStateTime = now;
      break;

    case 6:
      if (now - tsStateTime >= 3000) {
        while (esp8266.available()) esp8266.read();
        tsState = 7;
        tsStateTime = now;
      }
      break;

    case 7:
      esp8266.println(F("AT+CIPCLOSE=1"));
      tsState = 8;
      tsStateTime = now;
      break;

    case 8:
      if (now - tsStateTime >= 1000) {
        while (esp8266.available()) esp8266.read();
        tsState = 0;
        Serial.println(F("ThingSpeak: sent"));
      }
      break;
  }
}

// ========== MOTORS ==========
void Full_speed_forward() {
  Serial.println("Forward at slow speed");
  
  digitalWrite(motor_RB, LOW);
  analogWrite(motor_RF, 110);
  digitalWrite(motor_LB, LOW);
  analogWrite(motor_LF, 110);
}

void Turn_right() {
  digitalWrite(motor_RB, LOW);
  analogWrite(motor_RF, 0);
  digitalWrite(motor_LB, LOW);
  analogWrite(motor_LF, 110); // motor A faster (left)

  delay(100);
}

void Turn_left() {
  digitalWrite(motor_LB, LOW);
  analogWrite(motor_LF, 0);
  digitalWrite(motor_RB, LOW);
  analogWrite(motor_RF, 110);  // motor B faster (right)
}

void brake() {
  Serial.println("Brake");

  digitalWrite(motor_RB, HIGH);
  digitalWrite(motor_RF, HIGH);
  digitalWrite(motor_LB, HIGH);
  digitalWrite(motor_LF, HIGH);
}

void setMotors(int leftSpeed, int rightSpeed) {
  if (leftSpeed > 0) {
    digitalWrite(motor_LB, LOW);
    analogWrite(motor_LF, leftSpeed);
  } else if (leftSpeed < 0) {
    digitalWrite(motor_LF, LOW);
    analogWrite(motor_LB, -leftSpeed);
  } else {
    digitalWrite(motor_LF, LOW);
    digitalWrite(motor_LB, LOW);
  }

  if (rightSpeed > 0) {
    digitalWrite(motor_RB, LOW);
    analogWrite(motor_RF, rightSpeed);
  } else if (rightSpeed < 0) {
    digitalWrite(motor_RF, LOW);
    analogWrite(motor_RB, -rightSpeed);
  } else {
    digitalWrite(motor_RF, LOW);
    digitalWrite(motor_RB, LOW);
  }
}

void manualMode() {
  if (controller_speed == 0) {
    brake();
    return;
  }

  int leftSpeed = controller_speed;
  int rightSpeed = controller_speed;

  if (controller_direction < 0) {
    leftSpeed = controller_speed * (100 + controller_direction) / 100;
  } else if (controller_direction > 0) {
    rightSpeed = controller_speed * (100 - controller_direction) / 100;
  }

  setMotors(leftSpeed, rightSpeed);
}

void autoMode() {
  // 0 - white, 1 - black

  if (digitalRead(IR1Pin) == 0 && digitalRead(IR2Pin) == 0 ) {
    Full_speed_forward();
  }
  else if (digitalRead(IR2Pin) == 1) {                                                                                          
    Turn_left();
  }
  else if (digitalRead(IR1Pin) == 1) {
    Turn_right();
  }
}