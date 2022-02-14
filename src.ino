#include <WiFi.h> //Connect to WiFi Network
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h> //Used in support of TFT Display
#include <string.h>  //used for some string handling and processing.
#include <mpu6050_esp32.h>


const uint8_t UP_WOS = 0; //example definition
const uint8_t DOWN_WOS = 1; //example...
const uint8_t UP_WS = 2; //change if you want!
const uint8_t DOWN_WS = 3;
//add more if you want!!!

const uint8_t IDLE = 0; //example definition
const uint8_t MOVING = 1; //example...
const uint8_t WAIT = 2; //change if you want!
const int UPPER_THRESHOLD = 10.4; 
const int LOWER_THRESHOLD = 9.2;

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
MPU6050 imu; //imu object called, appropriately, imu

//Some constants and some resources:
const int RESPONSE_TIMEOUT = 6000; //ms to wait for response from host
const int GETTING_PERIOD = 1000*60; //periodicity of getting time
const int MVMT_TIMEOUT = 1000*15; //button timeout in milliseconds
const uint16_t IN_BUFFER_SIZE = 1000; //size of buffer to hold HTTP request
const uint16_t OUT_BUFFER_SIZE = 1000; //size of buffer to hold HTTP response
char request_buffer[IN_BUFFER_SIZE]; //char array buffer to hold HTTP request
char response_buffer[OUT_BUFFER_SIZE]; //char array buffer to hold HTTP response
const int PUSH_PERIOD = 1000/5;
const int COLON_PERIOD = 2000;

int hours = 16;
int minutes = 06;
int seconds = 20;

bool is_am;

char network[] = "MIT";
char password[] = "";

uint8_t scanning = 0;
uint8_t channel = 1; 
byte bssid[] = {0x04, 0x95, 0xE6, 0xAE, 0xDB, 0x41}; 


const int BUTTON = 45; //pin connected to button '
const int BUTTON2 = 39;
uint8_t state;  //system state (feel free to use)
uint8_t state2;
unsigned long pulled_time = -1*GETTING_PERIOD;  //used for storing millis() readings.
unsigned long last_time;
unsigned long push_time;//button 1
unsigned long push_time2;//button 2
unsigned long colon_time;
unsigned long last_mvmt;
uint8_t displayState=1; //State 1 = HH:MM, State 2 = HH:MM:SS
uint8_t motionState=1; //State 1 = Motion Activated, State 2 = Always On

float old_acc_mag, older_acc_mag; //maybe use for remembering older values of acceleration magnitude
float acc_mag = 0;  //used for holding the magnitude of acceleration
float avg_acc_mag = 0; //used for holding the running average of acceleration magnitude

const float ZOOM = 9.81; //for display (converts readings into m/s^2)...used for visualizing only
float x, y, z; //variables for grabbing x,y,and z values
int mvmtState = IDLE;

void setup(){
  tft.init();  //init screen
  tft.setRotation(2); //adjust rotation
  tft.setTextSize(2); //default font size
  tft.fillScreen(TFT_BLACK); //fill background
  tft.setTextColor(TFT_GREEN, TFT_BLACK); //set color of font to green foreground, black background
  Serial.begin(115200); //begin serial comms
  if (scanning){
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0) {
      Serial.println("no networks found");
    } else {
      Serial.print(n);
      Serial.println(" networks found");
      for (int i = 0; i < n; ++i) {
        Serial.printf("%d: %s, Ch:%d (%ddBm) %s ", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "");
        uint8_t* cc = WiFi.BSSID(i);
        for (int k = 0; k < 6; k++) {
          Serial.print(*cc, HEX);
          if (k != 5) Serial.print(":");
          cc++;
        }
        Serial.println("");
      }
    }
  }
  delay(100); //wait a bit (100 ms)

  //if using regular connection use line below:
  WiFi.begin(network, password);
  //if using channel/mac specification for crowded bands use the following:
  //WiFi.begin(network, password, channel, bssid);
  uint8_t count = 0; //count used for Wifi check times
  Serial.print("Attempting to connect to ");
  Serial.println(network);
  while (WiFi.status() != WL_CONNECTED && count<6) {
    delay(500);
    Serial.print(".");
    count++;
  }
  delay(2000);
  if (WiFi.isConnected()) { //if we connected then print our IP, Mac, and SSID we're on
    Serial.println("CONNECTED!");
    Serial.printf("%d:%d:%d:%d (%s) (%s)\n",WiFi.localIP()[3],WiFi.localIP()[2],
                                            WiFi.localIP()[1],WiFi.localIP()[0], 
                                          WiFi.macAddress().c_str() ,WiFi.SSID().c_str());
    delay(500);
  } else { //if we failed to connect just Try again.
    Serial.println("Failed to Connect :/  Going to restart");
    Serial.println(WiFi.status());
    ESP.restart(); // restart the ESP (proper way)
  }
  if (imu.setupIMU(1)) {
    Serial.println("IMU Connected!");
  } else {
    Serial.println("IMU Not Connected :/");
    Serial.println("Restarting");
    ESP.restart(); // restart the ESP (proper way)
  }
  pinMode(BUTTON, INPUT_PULLUP); //set input pin as an input!
  pinMode(BUTTON2, INPUT_PULLUP);
  state = UP_WOS;  
  state2 = UP_WOS;
  sprintf(request_buffer,"GET http://iesc-s3.mit.edu/esp32test/currenttime HTTP/1.1\r\n");
  strcat(request_buffer,"Host: iesc-s3.mit.edu\r\n"); //add more to the end
  strcat(request_buffer,"\r\n"); //add blank line!
  do_http_GET("iesc-s3.mit.edu", request_buffer, response_buffer, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, true);
  Serial.println(response_buffer);
  parse_time();
  pulled_time = millis();  
  last_mvmt = millis();
}

void loop(){
  LCDdisplay();
  if (millis() - pulled_time > GETTING_PERIOD) {
    sprintf(request_buffer,"GET http://iesc-s3.mit.edu/esp32test/currenttime HTTP/1.1\r\n");
    strcat(request_buffer,"Host: iesc-s3.mit.edu\r\n"); //add more to the end
    strcat(request_buffer,"\r\n"); //add blank line!
    do_http_GET("iesc-s3.mit.edu", request_buffer, response_buffer, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, true);
    Serial.println(response_buffer);
    parse_time();
    pulled_time = millis();
    last_time = millis();
  }
  int bvalue = digitalRead(BUTTON);
  switch (state) {
    case UP_WOS:
      if (bvalue == 0) {
        push_time = millis();
        state = DOWN_WOS;
      }    
      break;  
    case DOWN_WOS:
      if ((millis()-push_time > PUSH_PERIOD) && bvalue == 1) {
        displayState = 2;
        state = UP_WS;
      }
      break;
    case UP_WS:
      if (bvalue == 0) {
        push_time = millis();
        state = DOWN_WS;
      }
      break;
    case DOWN_WS:
      if ((millis()-push_time > PUSH_PERIOD) && bvalue == 1) {
        displayState = 1;
        state = UP_WOS;
        // LCDdisplay();
      }
      break;
  }

  int bvalue2 = digitalRead(BUTTON2);
  switch (state2) {
    case UP_WOS:
      if (bvalue2 == 0) {
        push_time2 = millis();
        state2 = DOWN_WOS;
      }    
      break;  
    case DOWN_WOS:
      if ((millis()-push_time2 > PUSH_PERIOD) && bvalue2 == 1) {
        motionState = 2;
        state2 = UP_WS;
        // LCDdisplay();
      }
      break;
    case UP_WS:
      if (bvalue2 == 0) {
        push_time2 = millis();
        state2 = DOWN_WS;
      }
      break;
    case DOWN_WS:
      if ((millis()-push_time2 > PUSH_PERIOD) && bvalue2 == 1) {
        motionState = 1;
        state2 = UP_WOS;
      }
      break;
  }

  if (motionState == 1) {
    imu.readAccelData(imu.accelCount);
    x = ZOOM * imu.accelCount[0] * imu.aRes;
    y = ZOOM * imu.accelCount[1] * imu.aRes;
    z = ZOOM * imu.accelCount[2] * imu.aRes;
    acc_mag = sqrt(x*x + y*y + z*z);
    avg_acc_mag = (acc_mag + old_acc_mag + older_acc_mag)/3.0;
    older_acc_mag = old_acc_mag;
    old_acc_mag = acc_mag;
    switch (mvmtState) {
      case IDLE:
        if (avg_acc_mag > UPPER_THRESHOLD && (millis() - last_mvmt > 1000/3)) {
          mvmtState = MOVING;
          last_mvmt = millis();
        }
        break;
      case MOVING:
        if (avg_acc_mag < LOWER_THRESHOLD && (millis() - last_mvmt > 1000/3)) {
          last_mvmt = millis();
          mvmtState = WAIT;          
        }
      case WAIT:
        if (avg_acc_mag < UPPER_THRESHOLD && (millis() - last_mvmt > MVMT_TIMEOUT)) {
          mvmtState = IDLE;
        } else if (avg_acc_mag > UPPER_THRESHOLD) {
          mvmtState = MOVING;
          last_mvmt = millis();
        }
        break;
    }
    Serial.println(state2);
  }
}


/*------------------
 * number_fsm Function:
 * Use this to implement your finite state machine. It takes in an input (the reading from a switch), and can use
 * state as well as other variables to be your state machine.
 */

void parse_time() {
  Serial.println("Parsing Now");
  char delimiter = ' ';
  char* ptr = strtok(response_buffer, &delimiter);

  delimiter = ':';
  ptr = strtok(NULL, &delimiter);
  hours = atoi(ptr);//parse pointer
  if (hours > 12) {
    hours -= 12;
    is_am = false;
  }
  Serial.println(hours);

  ptr = strtok(NULL, &delimiter); 
  minutes = atoi(ptr);//parse pointer
  Serial.println(minutes);

  ptr = strtok(NULL, &delimiter);
  seconds = atoi(ptr);//parse pointer
  Serial.println(seconds);
  Serial.println("Done Parsing");
}

/*----------------------------------
 * char_append Function:
 * Arguments:
 *    char* buff: pointer to character array which we will append a
 *    char c: 
 *    uint16_t buff_size: size of buffer buff
 *    
 * Return value: 
 *    boolean: True if character appended, False if not appended (indicating buffer full)
 */
uint8_t char_append(char* buff, char c, uint16_t buff_size) {
  int len = strlen(buff);
  if (len>buff_size) return false;
  buff[len] = c;
  buff[len+1] = '\0';
  return true;
}

/*----------------------------------
 * do_http_GET Function:
 * Arguments:
 *    char* host: null-terminated char-array containing host to connect to
 *    char* request: null-terminated char-arry containing properly formatted HTTP GET request
 *    char* response: char-array used as output for function to contain response
 *    uint16_t response_size: size of response buffer (in bytes)
 *    uint16_t response_timeout: duration we'll wait (in ms) for a response from server
 *    uint8_t serial: used for printing debug information to terminal (true prints, false doesn't)
 * Return value:
 *    void (none)
 */
void do_http_GET(char* host, char* request, char* response, uint16_t response_size, uint16_t response_timeout, uint8_t serial){
  WiFiClient client; //instantiate a client object
  if (client.connect(host, 80)) { //try to connect to host on port 80
    if (serial) Serial.print(request);//Can do one-line if statements in C without curly braces
    client.print(request);
    memset(response, 0, response_size); //Null out (0 is the value of the null terminator '\0') entire buffer
    uint32_t count = millis();
    while (client.connected()) { //while we remain connected read out data coming back
      client.readBytesUntil('\n',response,response_size);
      if (serial) Serial.println(response);
      if (strcmp(response,"\r")==0) { //found a blank line!
        break;
      }
      memset(response, 0, response_size);
      if (millis()-count>response_timeout) break;
    }
    memset(response, 0, response_size);  
    count = millis();
    while (client.available()) { //read out remaining text (body of response)
      char_append(response,client.read(),OUT_BUFFER_SIZE);
    }
    if (serial) Serial.println(response);
    client.stop();
    if (serial) Serial.println("-----------");  
  }else{
    if (serial) Serial.println("connection failed :/");
    if (serial) Serial.println("wait 0.5 sec...");
    client.stop();
  }
}  

void LCDdisplay() {
  if (mvmtState == IDLE && motionState == 1) {
      tft.fillScreen(TFT_BLACK);
  } else {
    char output[100];
    if ((millis()-last_time)/1000 >= 1) {
      seconds += 1;
      last_time = millis();
    }
    if (seconds >= 60) {
      minutes += seconds/60;
      seconds %= 60;
      if (minutes >= 60) {
        hours += minutes/60;
        minutes %= 60;
        if (hours > 12) {
          is_am = !is_am;
          hours -= 12;
        }
      }    
    }
    if (displayState == 1) {
      if (millis() - colon_time <= COLON_PERIOD/2) {
        sprintf(output, "%d%s%02d   ", hours, ":", minutes); 
      } else {
        sprintf(output, "%d%s%02d   ", hours, " ", minutes);
        if (millis() - colon_time > COLON_PERIOD) {
          colon_time = millis();
        }
      }
      tft.setCursor(0, 0, 1);
      tft.println(output);
    } else {
      sprintf(output, "%d:%02d:%02d", hours, minutes, seconds); 
      tft.setCursor(0, 0, 1);
      tft.println(output);
    }
    if (is_am) {
      tft.setCursor(0, 30, 1);
      tft.println("PM");
    } else {
      tft.setCursor(0, 30, 1);
      tft.println("PM");
    }
  }
}      
