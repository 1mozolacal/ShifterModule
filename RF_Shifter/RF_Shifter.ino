#include <SPI.h>
#include <WiFi101.h>
#include <SD.h>
#include "arduino_secrets.h" 

// defines for setting up the timings between solenoids. See diagram:
//
//     |clutch solenoid on                   |clutch solenoid off
//     |_____________________________________|
//     |                                     |
// ____|                                     |______
//     |   |shift solenoid on            |shift solenoid off
//     |   |_____________________________|   |
//     |   |                             |   |
// ____|___|     <- SHIFT_x_TIME ->      |___|_______
//     |   |                             |   |
//     |   |                             |   |
//   ->|   |<- CLUTCH_SHIFT_ON_DELAY     |   |
//     |   |                           ->|   |<- SHIFT_OFF_CLUTCH_DELAY

#define MIN_VALUE_OF_DELAYS 0
#define MAX_VALUE_OF_DELAYS 500

//default values
#define SHIFT_UP_START_DELAY 70
#define CLUTCH_SHIFT_ON_DELAY 60
#define SHIFT_UP_TIME 150
#define SHIFT_DOWN_TIME 150
#define SHIFT_OFF_CLUTCH_DELAY 0

// solenoid control output pins
int shiftUpSolenoid = 2;
int shiftDownSolenoid = 1;
int clutchSolenoid = 0;
// output to ECU that will tell it to cut spart (upshift)
int ecuCutSpk = 3;

// button inputs for shifting
int shiftUpBtn = 11;
int shiftDownBtn = 12;

// variables used for storing the shifting state.
// prevents multiple shifts happening if driver keeps shifting btns pressed
// this way, driver has to release btn and then press for a second shift to occur
bool shiftedUp = false, shiftedDown = false;

//led on pin 13
//int led = 13;addded lower down

//debounce stuff
int lastUpShiftState;
int lastDownShiftState;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
//----------------------SD varibles
boolean useCard = false;
String logFileName = "";//the name of the log file for the current session

//-----------------------wifi stuff build off of Tom Igoe simpleWebServer example
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;                // your network key Index number (needed only for WEP)

int led =  LED_BUILTIN;
int status = WL_IDLE_STATUS;
WiFiServer server(80);

static int numberOfInputs = 5;
int inputValues[5];

boolean wifiSetup = true;
long lastLoopTime = 0;

//has 6 total seconds in setup delay
void setup() {

  // sets up all required pins as inputs or outputs
  pinMode(clutchSolenoid, OUTPUT);
  pinMode(shiftUpSolenoid, OUTPUT);
  pinMode(shiftDownSolenoid, OUTPUT);
  digitalWrite(clutchSolenoid, HIGH);
  digitalWrite(shiftUpSolenoid, HIGH);
  digitalWrite(shiftDownSolenoid, HIGH);
  
  pinMode(shiftUpBtn, INPUT);
  pinMode(shiftDownBtn, INPUT);

  //set up led connected to pin 13 to visualize when the shift button has been activiated by user
  
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);

  //set up for ecu cut spark signal for up shift CHECK IF THE PINS ARE ACTIVE LOW FOR THE ecuCutSpk, make necessary adjustments
  
  pinMode(ecuCutSpk, OUTPUT);
  digitalWrite(ecuCutSpk, LOW);

  Serial.begin(9600);
  

  digitalWrite(clutchSolenoid, LOW);
  delay(2000);//SETUP DELAY
  digitalWrite(clutchSolenoid, HIGH);


  //-------SD stuff
  //Set up of SD car
  delay(3000);//SETUP DELAY
  if (!SD.begin(4)) {
    Serial.println("Card failed, or not present");
  } else{
    Serial.println("card initialized.");
    int number = readLogNumThenIncrease();
    String preNum = "log-";
    String dot = ".txt";
    logFileName = preNum + number + dot;
    useCard = true;
    readSettings();
  }
  
  //------Wifi stuff
  
  //inits int array
  inputValues[0] = SHIFT_UP_START_DELAY;//SHIFT_UP_START_DELAY
  inputValues[1] = CLUTCH_SHIFT_ON_DELAY;//CLUTCH_SHIFT_ON_DELAY
  inputValues[2] = SHIFT_UP_TIME;//SHIFT_UP_TIME
  inputValues[3] = SHIFT_DOWN_TIME;//SHIFT_DOWN_TIME
  inputValues[4] = SHIFT_OFF_CLUTCH_DELAY;//SHIFT_OFF_CLUTCH_DELAY
  /*for(int i =0; i < numberOfInputs; i++){
    inputValues[i] = "";
  }*/
  //Serial.begin(9600); is done above
  Serial.println("Access Point Web Server");
  pinMode(led, OUTPUT);      // set the LED pin mode
  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("***********WARNING*************");
    Serial.println("WiFi shield not present");
    wifiSetup = false;//don't run any wifi stuff
  }else{
    WiFi.config(IPAddress(4,9,9,2) );//if this is not ran default is 192.168.1.1
    Serial.print("Creating access point named: ");// print the network name (SSID);
    Serial.println(ssid);
    // Create open network. Change this line if you want to create an WEP network:
    delay(1000);
    status = WiFi.beginAP(ssid);
    
    if (status != WL_AP_LISTENING) {
      Serial.println("*********WARNING*********");
      Serial.println("Creating access point failed");
      Serial.println( status );
      // don't continue
      wifiSetup = false;
    }else{
    delay(1000); // wait 1 seconds for connection: SETUP DELAY
    server.begin();  // start the web server on port 80
    printWiFiStatus();// you're connected now, so print out the status
    }//end of if-else for faiture to create access point
  }//end of if-eles for no shield chekc
}

void shiftDown() {
  digitalWrite(clutchSolenoid, LOW);
  delay(inputValues[1]);//CLUTCH_SHIFT_ON_DELAY
  digitalWrite(shiftDownSolenoid, LOW);
  digitalWrite(led, HIGH);
  delay(inputValues[3]);//SHIFT_DOWN_TIME
  digitalWrite(shiftDownSolenoid, HIGH);
  digitalWrite(led, LOW);
  delay(inputValues[4]);//SHIFT_OFF_CLUTCH_DELAY
  digitalWrite(clutchSolenoid, HIGH);

  writeLog("Shift down finished");
}

void shiftUp() {
  //ecu signal to cut spark
  digitalWrite(ecuCutSpk, HIGH);
  delay(SHIFT_UP_START_DELAY);
  digitalWrite(ecuCutSpk, LOW);
  
  //up shift code
  digitalWrite(shiftUpSolenoid, LOW);
  digitalWrite(led, HIGH);
  delay(inputValues[2]);//SHIFT_UP_TIME
  digitalWrite(shiftUpSolenoid, HIGH);
  digitalWrite(led, LOW);

  writeLog("Shift up finished");
}


void loop() {

  int shiftDownState = digitalRead(shiftDownBtn);
  int shiftUpState = digitalRead(shiftUpBtn);
  
  // if user pressed shift down button for the first time
  //if((millis() - lastDebounceTime) > debounceDelay) {
    if(shiftDownState == HIGH && !shiftedDown) {
      Serial.println("down");

      shiftDown();
      lastDebounceTime = millis();
      Serial.println("down");

    
      // remember that we already did a shift so that we don't repeat
      // it next loop without the user re-pressing the button first
      shiftedDown = true;
    } else if(shiftDownState == LOW && shiftedDown) {
      // else if user released the button
      shiftedDown = false;
      lastDebounceTime = millis();
    }
  
   // if user pressed shift up button for the first time
    if(shiftUpState == HIGH && !shiftedUp) {
      Serial.println("up");
      shiftUp();
      lastDebounceTime = millis();
      Serial.println("up");
          
  
      // remember that we already did a shift so that we don't repeat
      // it next loop without the user re-pressing the button first
      shiftedUp = true;
    } else if(shiftUpState == LOW && shiftedUp) {
      // else if user released the button
      shiftedUp = false;
      lastDebounceTime = millis();
    }
  
    
  

  //Serial.print("Running ");
  //Serial.println( millis() - lastLoopTime );
  lastLoopTime = millis();
  
  if(wifiSetup){
    checkForClientAndRead();
  }
}

void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  // print where to go in a browser:
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);

}

void printMacAddress(byte mac[]) {
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
    if (i > 0) {
      Serial.print(":");
    }
  }
  Serial.println();
}


void checkForClientAndRead(){
  // compare the previous status to the current status
  if (status != WiFi.status()) {
    // it has changed update the variable
    status = WiFi.status();

    if (status == WL_AP_CONNECTED) {
      byte remoteMac[6];

      // a device has connected to the AP
      Serial.print("Device connected to AP, MAC address: ");
      WiFi.APClientMacAddress(remoteMac);
      printMacAddress(remoteMac);
    } else {
      // a device has disconnected from the AP, and we are back in listening mode
      Serial.println("Device disconnected from AP");
    }
  }
  
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    boolean saveLine = false;
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        //Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // the content of the HTTP response follows the header:
            //client.print("Click <a href=\"/H\">here</a> turn the LED on<br>");
            //client.print("Click <a href=\"/L\">here</a> turn the LED off<br>");
            //client.print("<input type=\"text\" name=\"LastName\" value=\"Mouse\"><br>");
            //client.print("<input type=\"submit\" value=\"Submit\">");
            client.print("When inputing value make sure that there is no spaces &,= or H <br>");
            client.print("Card enabled: " + String(useCard) );
            client.print("<form action=\"/Z\" method=\"get\">");
            client.print("SHIFT_UP_START_DELAY: <input type=\"text\" name=\"a\" value="+String(inputValues[0])+"><br>");
            client.print("CLUTCH_SHIFT_ON_DELAY: <input type=\"text\" name=\"b\" value="+String(inputValues[1])+"><br>");
            client.print("SHIFT_UP_TIME: <input type=\"text\" name=\"b\" value="+String(inputValues[2])+"><br>");
            client.print("SHIFT_DOWN_TIME: <input type=\"text\" name=\"b\" value="+String(inputValues[3])+"><br>");
            client.print("SHIFT_OFF_CLUTCH_DELAY: <input type=\"text\" name=\"b\" value="+String(inputValues[4])+"><br>");
            client.print("<input type=\"submit\" value=\"Submit\"></form>");

            //for testing the connection
            client.print("<br><br>Turn on board light(on MKR1000) for testing<br>");
            client.print("Click <a href=\"/H\">here</a> turn the LED on<br>");
            client.print("Click <a href=\"/L\">here</a> turn the LED off<br>");
            
            // The HTTP response ends with another blank line:
            client.println();
            // break out of the while loop:
            break;
          }
          else {      // if you got a newline, then clear currentLine:
            if(saveLine){
              saveLine = false;
              Serial.println("______________IMPORTANT LINE__________");
              Serial.println(currentLine);
              Serial.println("--------------------------------------"); 
              parseInfo(currentLine); 
            }
            currentLine = "";
            
            
          }
        }
        else if (c != '\r') {    // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // Check to see if the client request was "GET /H" or "GET /L":
        if(currentLine.endsWith("GET ") ){
          saveLine=true;
        }
        if (currentLine.endsWith("GET /H")) {
          digitalWrite(led, HIGH);               // GET /H turns the LED on
        }
        if (currentLine.endsWith("GET /L")) {
          digitalWrite(led, LOW);                // GET /L turns the LED off
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }
}//end of check for client

void parseInfo(String data){
  int stage = 0;//0 not started, 1=read key, 2=read value
  String value = "";
  int valueToInput = 0;
  for(int i = 0; i < data.length(); i++){
    if(stage ==0){
      if( data.charAt(i) == '?' ){
        stage = 1;
      }
      continue;
    }
    char currentChar = data.charAt(i);
    if( currentChar == '=' ){
      stage = 2;
      value = "";
    }else if( currentChar == '&' ){
      stage ==1;
      setIntFromString(valueToInput,value);
      valueToInput++;
      if(valueToInput == numberOfInputs){ break; }
    }else if( currentChar == ' ' || currentChar =='H'){
      setIntFromString(valueToInput,value);
      break;
    }
    else if(stage ==2){
      value += currentChar;
    }
  }//end of for loop


  /*//printlns for testing
  Serial.println("Values after reader are:");
  for(int i = 0 ; i < numberOfInputs; i++){
    Serial.println( inputValues[i] );
  }*/
  saveSettings();//saves the changes to the SD card
  
}

//This has check to keep the number between set range
void setIntFromString(int index, String value){
  int intVal = value.toInt();
  if(intVal == 0){
    return;
  }else if( intVal == -1){//-1 means 0;
    inputValues[index] = 0;
  } else{
    if(intVal<MIN_VALUE_OF_DELAYS){
      intVal = MIN_VALUE_OF_DELAYS;
    }
    if(intVal>MAX_VALUE_OF_DELAYS){
      intVal=MAX_VALUE_OF_DELAYS;
    }
    inputValues[index] = intVal;
  }
}




//SD functions
//zero is return if file not available or if there is a non number at the begining
//if there is a letter inbetween the number it will stop reading the number at the letter
int getLogNumber(){
  if(!useCard){
    return 0;
  }
  File file = SD.open("logs/number.txt");
  return readPosIntFromFile(file);
}

int readLogNumThenIncrease(){
  if(!useCard){
    return 0;
  }
  int id = getLogNumber();
  SD.remove("logs/number.txt");
  File numberFile = SD.open("logs/number.txt", FILE_WRITE);
  if(numberFile){
    numberFile.println( id+1 );
    numberFile.close();
  }else {
    Serial.println("not open in readLogNumThenIncrease");
  }
  return id;
}

void writeLog(String action){
  if(!useCard){
    return;
  }
  File logFile = SD.open("logs/"+logFileName, FILE_WRITE);
  if(logFile){
    String between = " at time ";
    String mil = (String) millis();
    String writeString = action + between + mil;
    logFile.println( writeString);
    logFile.close();
    Serial.println(writeString);
  }else{
    Serial.println("can't log");
  }
}


void saveSettings(){
  if(!useCard){
    return;
  }
  String space = " ";
  String saveString = inputValues[0] + space +inputValues[1]+space+inputValues[2]+space+inputValues[3]+space+inputValues[4] ;
  SD.remove("config.txt");
  File file = SD.open("config.txt", FILE_WRITE);
  file.print(saveString);
  file.close();
}

//reads the values of the sd cards
void readSettings(){
  if(!useCard){
    return;
  }
  File file = SD.open("config.txt");
  for(int i = 0; i < numberOfInputs ; i++){
    int nextNum = readPosIntFromFile(file);
    inputValues[i] = nextNum;
  }//end of for
  
}

//read for a postive number stop as soon a a non integer character is reached
//if no number present then zero is returned
int readPosIntFromFile(File file){
  boolean notNumber =  false;
  int decodedNum = 0;
  while(file.available() && !notNumber){
    int charVal = file.read();
    if(charVal>=48 && charVal <=57){
      decodedNum = decodedNum*10 + charVal-48;
    } else {
      notNumber = true;
    }
  }
  return decodedNum;
  
}




