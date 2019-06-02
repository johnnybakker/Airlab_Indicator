#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

//Main loop delay (Miliseconds)
const int LOOP_DELAY = 100;

//MAC-address of the wifi chip
String MAC_ADRESS;

//Mode constants and variables
const int DISCONNECTED_MODE = 1;
const int CONNECTED_MODE = 2;
const int RESET_MODE = 3;
int mode = DISCONNECTED_MODE;

//AP mode and ip settings (DISCONNECTED_MODE)
IPAddress LOCAL_IP(192,168,1,254);
IPAddress GATEWAY(192,168,1,1);
IPAddress SUBNET(255,255,255,0);
ESP8266WebServer server(80); //Webserver instance for DISCONNECTED_MODE

//Wifi credentials variables (CONNECTED_MODE)
const int REQUEST_INTERVAL = 30; //Interval in seconds
double RequestTimer = 0.00;

//Wifi credetials reset button PIN
const int RESET_BUTTON_PIN = D6;
const int REQUEST_LED_PIN = D1;

//Max bytes to write to EEPROM storage
const int MAX_EEPROM_BYTES = 128;

//WIFI helper methods
String GetWifiNetworkOptions();

//EEPROM communication methods
String ReadWifiCredentials();
bool WriteWifiCredentials(String ssid, String password);
bool ClearWifiCredentials();

//Http handler declatations
void handleRoot();
void handleNotFound();
void handleRegister();

//Mode handler declarations
void SetupDisconnectedMode();
void UnSetDisconnectedMode();
void SetupConnectedMode();
void UnSetConnectedMode();
void LoopDisconnectedMode();
void LoopConnectedMode();
void ListenToResetButton();

void setup() {
  //Start serial communication
  Serial.begin(115200);

  //Wait 10 miliseconds then write the first message
  delay(10);
  Serial.println("\n");

  //Set MAC_ADRESS
  MAC_ADRESS = WiFi.macAddress();

  //Set RESET_BUTTON pinmode
  pinMode(RESET_BUTTON_PIN, INPUT);

  //Set REQUEST_LED pinmode
  pinMode(REQUEST_LED_PIN, OUTPUT);
  digitalWrite(REQUEST_LED_PIN, LOW);

  //Read registered ssid and password from EEPROM storage
  String ssid_password = ReadWifiCredentials();
  Serial.println("Stored ssid and password: " + ssid_password);

  //Set mode if wifi credentials are set
  if(ssid_password.length() > 0){
    mode = CONNECTED_MODE;
  }

  //Run setup method for the selected mode
  switch(mode){
    case CONNECTED_MODE:
      SetupConnectedMode();
    break;
    case DISCONNECTED_MODE:
      SetupDisconnectedMode();
    break;
  }

  Serial.println("Setup done!");
}

//Main loop
void loop(void){
  switch(mode){
    case CONNECTED_MODE:
      LoopConnectedMode();
      ListenToResetButton();
    break;
    case DISCONNECTED_MODE:
      LoopDisconnectedMode();
    break;
  }
  delay(LOOP_DELAY);
}

//Methods

void SetupDisconnectedMode(){
  Serial.println("Setting up disconnected mode");
  mode = DISCONNECTED_MODE;

  //Set wifi mode
  WiFi.mode(WIFI_AP);

  Serial.print("Setting soft-AP configuration ... ");
  Serial.println(WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET) ? "Ready" : "Failed!");

  Serial.print("Setting soft-AP ... ");
  Serial.println(WiFi.softAP("Indicator " + MAC_ADRESS, "12345678") ? "Ready" : "Failed!");

  Serial.print("Soft-AP IP address = ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/register", HTTP_POST, handleRegister);               // Call the 'handleRoot' function when a client requests URI "/"
  server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");
}

void UnSetDisconnectedMode(){
  Serial.println("Closing web-server");
  server.close();
  Serial.println("Disconnect AP");
  WiFi.softAPdisconnect();
}

void LoopDisconnectedMode(){
  //Listen for HTTP requests from clients
  server.handleClient();
}

void SetupConnectedMode(){
  mode = CONNECTED_MODE;
  RequestTimer = 0.00;
  //Set wifi to connection mode
  Serial.println("Setting up connected mode");
  WiFi.mode(WIFI_STA);
  String credentials = ReadWifiCredentials();
  int splitterIndex = credentials.indexOf(";");
  String ssid = credentials.substring(0, splitterIndex);
  String password = credentials.substring(splitterIndex + 1, credentials.length());
  Serial.println("Trying to connect with network " + ssid);
  if(password.length() > 0){
    WiFi.begin(ssid, password);
  }else{
    WiFi.begin(ssid);
  }
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Connection Successful!");
  Serial.print("My IP Address is: ");
  Serial.println(WiFi.localIP());// Print the IP address
}

void UnSetConnectedMode(){
  Serial.println("Disconnect from network");
  WiFi.disconnect();
}

void LoopConnectedMode(){
  RequestTimer += (LOOP_DELAY * 0.001);
  if(WiFi.status() == WL_CONNECTED && RequestTimer > REQUEST_INTERVAL){ //Check WiFi connection status 
    Serial.println("Sending request"); 
    digitalWrite(REQUEST_LED_PIN, HIGH);
    HTTPClient http; //Declare an object of class HTTPClient
    http.begin("http://jsonplaceholder.typicode.com/users/1");
    int httpCode = http.GET(); //Send the request
    Serial.println(String(httpCode));
    if (httpCode > 0) { //Check the returning code
      String payload = http.getString(); //Get the request response payload
      Serial.println(payload); //Print the response payload
    }
    http.end(); //Close connection 
    RequestTimer = 0.00;
    digitalWrite(REQUEST_LED_PIN, LOW);
  }
}

void ListenToResetButton(){
  if(digitalRead(RESET_BUTTON_PIN) == HIGH){
    Serial.println("Reset to disconnected mode");
    mode = RESET_MODE;
    ClearWifiCredentials();
    SetupDisconnectedMode();
    mode = DISCONNECTED_MODE;
  }
}

bool ClearWifiCredentials(){
  bool result = false;
  //Start connection with EEPROM storage
  EEPROM.begin(MAX_EEPROM_BYTES);
  //Write empty characters to RAM
  unsigned int i;
  for(i = 0; i < MAX_EEPROM_BYTES; i++){
    EEPROM.write(0 + i, '\0');
  }
  //Store data from RAM in EEPROM storage
  result = EEPROM.commit();
  //End connection with EEPROM storage
  EEPROM.end();
  return result;
}

bool WriteWifiCredentials(String ssid, String password){
  //Combine ssid with password
  String ssid_password = ssid +";"+ password;
  bool result = false;
  //Start connection with EEPROM storage
  EEPROM.begin(MAX_EEPROM_BYTES);
  if(ssid_password.length() < MAX_EEPROM_BYTES){
    //Write characters of ssid and password to RAM
    unsigned int i;
    for(i = 0; i < ssid_password.length(); i++){
      EEPROM.write(0 + i, ssid_password[i]);
    }
    //Write to end character to RAM
    EEPROM.write(0 + ssid_password.length(),'\0');
    //Store data from RAM in EEPROM storage
    result = EEPROM.commit();
    //End connection with EEPROM storage
    EEPROM.end();
  }
  return result;
}

String ReadWifiCredentials(){
  EEPROM.begin(MAX_EEPROM_BYTES);
  char data[MAX_EEPROM_BYTES] = "";
  int address = 0;
  unsigned char rc = EEPROM.read(0);
  do
  {   
    rc = EEPROM.read(0 + address);
    data[address] = rc;
    address++;
  } while(rc != '\0' && address < MAX_EEPROM_BYTES);
  EEPROM.end();
  return String(data);
}

String GetWifiNetworkOptions(){
  WiFi.scanDelete();
  int networksFound = WiFi.scanNetworks();
  String networksOptions = "";
  for (int i = 0; i < networksFound; i++)
  {
    String ssid = WiFi.SSID(i).c_str();
    String index = String(i);
    networksOptions += "<option value='" + ssid + "'>"+ ssid +"</option>";
  }
  return networksOptions;
}

void handleRoot() {
  String page = "<!DOCTYPE html>";
  page += "<html>                                                     ";
  page += " <body>                                                    ";
  page += "   <h1>Connect to network</h1>                             ";
  page += "   <form method='POST' action='/register' name='register'> ";
  page += "     <select name='network'>"+ GetWifiNetworkOptions() +"</select> ";
  page += "     <p>Password:</p>                                      ";
  page += "     <input type='password' name='password'/></br>         ";
  page += "     <input type='submit' name='submit' value='Connect'/>  ";
  page += "   </form>                                                 ";
  page += " </body>                                                   ";
  page += "</html>                                                    ";
  server.send(200, "text/html", page);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleRegister(){
  if(server.hasArg("network") && server.hasArg("password")){
    if(server.arg("network") != ""){
      //Define variables
      String ssid = server.arg("network");
      String password = server.arg("password");
  
      //Try to write wifi credentials to EEPROM storage
      if(WriteWifiCredentials(ssid, password)){
        server.send(200, "text/html", "<!DOCTYPE html><html><body><h1>Thanks</h1><a href='/'>Terug</a></body></html>");
        UnSetDisconnectedMode();
        SetupConnectedMode();
      }
      else{
        server.send(200, "text/html", "<!DOCTYPE html><html><body><h1>Failed</h1><a href='/'>Terug</a></body></html>");
      }
    }
  }
}

void handleNotFound(){
  server.send(404, "text/html", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}