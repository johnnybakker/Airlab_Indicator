#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

//BAUD_RATE
const int BAUD_RATE = 74880;

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
IPAddress LOCAL_IP(192, 168, 4, 1);
IPAddress GATEWAY(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 255, 0);
ESP8266WebServer server(80); //Webserver instance for DISCONNECTED_MODE

//Client settings variables (CONNECTED_MODE)
char HOST[] = "uhoo.dvc-icta.nl";
const int REQUEST_INTERVAL = 30; //Interval in seconds
double RequestTimer = 0.00;
int LED_STATUS = -2;

//GPIO Pins
const int GREEN_PIN = D1;
const int YELLOW_PIN = D2;
const int RED_PIN = D3;
const int RESET_BUTTON_PIN = D7;
const int SPEAKER_PIN = D8;
const bool USE_BEEP = true;

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
void RefreshStatusLED();
int GetStatus();
void Beep();

void setup()
{
    //Start serial communication
    Serial.begin(BAUD_RATE);

    //Wait 10 miliseconds then write the first message
    delay(10);
    Serial.println("\n");

    //Set MAC_ADRESS
    MAC_ADRESS = WiFi.macAddress();

    //Set RESET_BUTTON pinmode
    pinMode(RESET_BUTTON_PIN, INPUT);

    //Set REQUEST_LED pinmode
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(YELLOW_PIN, OUTPUT);
    pinMode(RED_PIN, OUTPUT);
    pinMode(SPEAKER_PIN, OUTPUT);

    if(USE_BEEP) Beep();

    //Read registered ssid and password from EEPROM storage
    String ssid_password = ReadWifiCredentials();
    Serial.println("Stored ssid and password: " + ssid_password);

    //Set mode if wifi credentials are set
    if (ssid_password.length() > 0)
    {
        mode = CONNECTED_MODE;
    }

    //Run setup method for the selected mode
    switch (mode)
    {
    case CONNECTED_MODE:
        SetupConnectedMode();
        break;
    case DISCONNECTED_MODE:
        SetupDisconnectedMode();
        break;
    }
}

//Main loop
void loop(void)
{
    RefreshStatusLED();
    switch (mode)
    {
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
void SetupDisconnectedMode()
{
    Serial.println("Setting up disconnected mode");
    mode = DISCONNECTED_MODE;
    LED_STATUS = -2;

    //Set wifi mode
    WiFi.mode(WIFI_AP);

    Serial.print("Setting soft-AP configuration ... ");
    Serial.println(WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET) ? "Ready" : "Failed!");

    Serial.print("Setting soft-AP ... ");
    Serial.println(WiFi.softAP("Indicator " + MAC_ADRESS, "12345678") ? "Ready" : "Failed!");

    Serial.print("Soft-AP IP address = ");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, handleRoot);              // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/register", HTTP_POST, handleRegister); // Call the 'handleRoot' function when a client requests URI "/"
    server.onNotFound(handleNotFound);                 // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"

    server.begin(); // Actually start the server
    Serial.println("HTTP server started");
}

void UnSetDisconnectedMode()
{
    Serial.println("Closing web-server");
    server.close();
    Serial.println("Disconnect AP");
    WiFi.softAPdisconnect();
}

void LoopDisconnectedMode()
{
    //Listen for HTTP requests from clients
    server.handleClient();
}

void SetupConnectedMode()
{
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
    if (password.length() > 0)
    {
        WiFi.begin(ssid, password);
    }
    else
    {
        WiFi.begin(ssid);
    }
    LED_STATUS = -4;
    while (WiFi.status() != WL_CONNECTED && mode == CONNECTED_MODE)
    {
        RefreshStatusLED();
        delay(100);
        Serial.print(".");
        ListenToResetButton();
    }
    Serial.println("");
    Serial.println("Connection Successful!");
    Serial.print("My IP Address is: ");
    Serial.println(WiFi.localIP());
    LED_STATUS = GetStatus();
    if(LED_STATUS == 3 && USE_BEEP) Beep();
}

void UnSetConnectedMode()
{
    Serial.println("Disconnect from network");
    while (WiFi.isConnected())
    {
        WiFi.disconnect();
    }
}

void LoopConnectedMode()
{
    RequestTimer += (LOOP_DELAY * 0.001);
    if (RequestTimer > REQUEST_INTERVAL)
    { //Check WiFi connection status
        LED_STATUS = GetStatus();
        if(LED_STATUS == 3 && USE_BEEP) Beep();
        //Set request timer to zero
        RequestTimer = 0.00;
    }
}

void ListenToResetButton()
{
    if (digitalRead(RESET_BUTTON_PIN) == HIGH)
    {
        Serial.println("Reset to disconnected mode");
        mode = RESET_MODE;
        ClearWifiCredentials();
        UnSetConnectedMode();
        SetupDisconnectedMode();
        mode = DISCONNECTED_MODE;
    }
}

bool ClearWifiCredentials()
{
    bool result = false;
    //Start connection with EEPROM storage
    EEPROM.begin(MAX_EEPROM_BYTES);
    //Write empty characters to RAM
    unsigned int i;
    for (i = 0; i < MAX_EEPROM_BYTES; i++)
    {
        EEPROM.write(0 + i, '\0');
    }
    //Store data from RAM in EEPROM storage
    result = EEPROM.commit();
    //End connection with EEPROM storage
    EEPROM.end();
    return result;
}

bool WriteWifiCredentials(String ssid, String password)
{
    //Combine ssid with password
    String ssid_password = ssid + ";" + password;
    bool result = false;
    //Start connection with EEPROM storage
    EEPROM.begin(MAX_EEPROM_BYTES);
    if (ssid_password.length() < MAX_EEPROM_BYTES)
    {
        //Write characters of ssid and password to RAM
        unsigned int i;
        for (i = 0; i < ssid_password.length(); i++)
        {
            EEPROM.write(0 + i, ssid_password[i]);
        }
        //Write to end character to RAM
        EEPROM.write(0 + ssid_password.length(), '\0');
        //Store data from RAM in EEPROM storage
        result = EEPROM.commit();
    }
    EEPROM.end(); //End connection with EEPROM storage
    return result;
}

String ReadWifiCredentials()
{
    EEPROM.begin(MAX_EEPROM_BYTES);
    char data[MAX_EEPROM_BYTES] = "";
    int address = 0;
    unsigned char rc = EEPROM.read(0);
    do
    {
        rc = EEPROM.read(0 + address);
        data[address] = rc;
        address++;
    } while (rc != '\0' && address < MAX_EEPROM_BYTES);
    EEPROM.end();
    return String(data);
}

String GetWifiNetworkOptions()
{
    WiFi.scanDelete();
    int networksFound = WiFi.scanNetworks();
    String networksOptions = "";
    for (int i = 0; i < networksFound; i++)
    {
        String ssid = WiFi.SSID(i).c_str();
        String index = String(i);
        networksOptions += "<option value='" + ssid + "'></option>";
    }
    return networksOptions;
}

void handleRoot()
{
    String page = "<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <meta http-equiv=\"X-UA-Compatible\" content=\"ie=edge\"> <title>Indicator setup</title> <style>body,html{font-family:sans-serif;line-height:1.15;-webkit-text-size-adjust:100%;-ms-text-size-adjust:100%;-ms-overflow-style:scrollbar;-webkit-tap-highlight-color:transparent;margin:0;padding:0}div.container{width:500px;margin-left:auto;margin-right:auto;padding:15px;margin-top:20px}.input-group{position:relative;display:-webkit-box;display:-ms-flexbox;display:flex;-ms-flex-wrap:wrap;flex-wrap:wrap;-webkit-box-align:stretch;-ms-flex-align:stretch;align-items:stretch;width:100%;margin-bottom:1em}.form-control{display:block;width:100%;padding:.375rem .75rem;font-size:1rem;line-height:1.5;color:#495057;background-color:#fff;background-clip:padding-box;border:1px solid #ced4da;border-radius:.25rem;transition:border-color .15s ease-in-out,box-shadow .15s ease-in-out}.btn:not(:disabled):not(.disabled){cursor:pointer}.btn-success{color:#fff;background-color:#28a745;border-color:#28a745}.btn{width:100%;display:block;font-weight:400;text-align:center;white-space:nowrap;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;padding:.375rem .75rem;font-size:1rem;line-height:1.5;border-radius:.25rem;transition:color .15s ease-in-out,background-color .15s ease-in-out,border-color .15s ease-in-out,box-shadow .15s ease-in-out}.btn-success:hover{color:#fff;background-color:#218838;border-color:#1e7e34;text-decoration:none}</style> </head> <body> <div class=\"container\"> <h1>Indicator " + MAC_ADRESS + "</h1> <form method='POST' action='/register' name='register'> <label for=\"password\">Netwerk naam</label> <div class=\"input-group\"> <input class=\"form-control\" list=\"networks\" name=\"network\"/> <datalist id=\"networks\">" + GetWifiNetworkOptions() + "</datalist> </div><label for=\"password\">Wachtwoord:</label> <div class=\"input-group\"> <input class=\"form-control\" type='password' name='password'/> </div><input class=\"btn btn-success\" type='submit' name='submit' value='Verbinden'/> </form> </div></body></html>";
    server.send(200, "text/html", page); // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleRegister()
{
    if (server.hasArg("network") && server.hasArg("password"))
    {
        if (server.arg("network") != "")
        {
            //Define variables
            String ssid = server.arg("network");
            String password = server.arg("password");

            //Try to write wifi credentials to EEPROM storage
            if (WriteWifiCredentials(ssid, password))
            {
                server.send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <meta http-equiv=\"X-UA-Compatible\" content=\"ie=edge\"> <title>Setup succeeded</title> <style>body,html{font-family:sans-serif;line-height:1.15;-webkit-text-size-adjust:100%;-ms-text-size-adjust:100%;-ms-overflow-style:scrollbar;-webkit-tap-highlight-color:transparent;margin:0;padding:0}div.container{width:500px;margin-left:auto;margin-right:auto;padding:15px;margin-top:20px}.input-group{position:relative;display:-webkit-box;display:-ms-flexbox;display:flex;-ms-flex-wrap:wrap;flex-wrap:wrap;-webkit-box-align:stretch;-ms-flex-align:stretch;align-items:stretch;width:100%;margin-bottom:1em}.form-control{display:block;width:100%;padding:.375rem .75rem;font-size:1rem;line-height:1.5;color:#495057;background-color:#fff;background-clip:padding-box;border:1px solid #ced4da;border-radius:.25rem;transition:border-color .15s ease-in-out,box-shadow .15s ease-in-out}.btn:not(:disabled):not(.disabled){cursor:pointer}.btn-success{color:#fff;background-color:#28a745;border-color:#28a745}.btn{width:100%;display:block;font-weight:400;text-align:center;white-space:nowrap;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;padding:.375rem .75rem;font-size:1rem;line-height:1.5;border-radius:.25rem;transition:color .15s ease-in-out,background-color .15s ease-in-out,border-color .15s ease-in-out,box-shadow .15s ease-in-out}.btn-success:hover{color:#fff;background-color:#218838;border-color:#1e7e34;text-decoration:none}</style> </head> <body> <div class=\"container\"> <h1>Geslaagd!</h1> <p>De indicator zal zijn wifi netwerk sluiten en gaan proberen te verbinden met het door u geselecteerde netwerk.</p></div></body></html>");
                delay(5000);
                UnSetDisconnectedMode();
                SetupConnectedMode();
            }
            else
            {
                server.send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <meta http-equiv=\"X-UA-Compatible\" content=\"ie=edge\"> <title>Setup succeeded</title> <style>body,html{font-family:sans-serif;line-height:1.15;-webkit-text-size-adjust:100%;-ms-text-size-adjust:100%;-ms-overflow-style:scrollbar;-webkit-tap-highlight-color:transparent;margin:0;padding:0}div.container{width:500px;margin-left:auto;margin-right:auto;padding:15px;margin-top:20px}.input-group{position:relative;display:-webkit-box;display:-ms-flexbox;display:flex;-ms-flex-wrap:wrap;flex-wrap:wrap;-webkit-box-align:stretch;-ms-flex-align:stretch;align-items:stretch;width:100%;margin-bottom:1em}.form-control{display:block;width:100%;padding:.375rem .75rem;font-size:1rem;line-height:1.5;color:#495057;background-color:#fff;background-clip:padding-box;border:1px solid #ced4da;border-radius:.25rem;transition:border-color .15s ease-in-out,box-shadow .15s ease-in-out}.btn:not(:disabled):not(.disabled){cursor:pointer}.btn-success{color:#fff;background-color:#28a745;border-color:#28a745}.btn{width:100%;display:block;font-weight:400;text-align:center;white-space:nowrap;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;padding:.375rem .75rem;font-size:1rem;line-height:1.5;border-radius:.25rem;transition:color .15s ease-in-out,background-color .15s ease-in-out,border-color .15s ease-in-out,box-shadow .15s ease-in-out}.btn-success:hover{color:#fff;background-color:#218838;border-color:#1e7e34;text-decoration:none}</style> </head> <body> <div class=\"container\"> <h1>Mislukt!</h1> <p>Er is iets mis gegaan tijdens het instellen van de indicator. Probeer het opnieuw</p></div></body></html>");
            }
        }
    }
}

void handleNotFound()
{
    server.send(404, "text/html", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void RefreshStatusLED()
{
    switch (LED_STATUS)
    {
    case -4: //Connecting
        digitalWrite(GREEN_PIN, LOW);
        digitalWrite(YELLOW_PIN, LOW);
        digitalWrite(RED_PIN, LOW);
        delay(100);
        digitalWrite(RED_PIN, HIGH);
        delay(100);
        digitalWrite(RED_PIN, LOW);
        digitalWrite(YELLOW_PIN, HIGH);
        delay(100);
        digitalWrite(YELLOW_PIN, LOW);
        digitalWrite(GREEN_PIN, HIGH);
        break;
    case -3: //Internal error
        if (digitalRead(GREEN_PIN) == HIGH)
            digitalWrite(GREEN_PIN, LOW);
        if (digitalRead(YELLOW_PIN) == HIGH)
            digitalWrite(YELLOW_PIN, LOW);
        digitalWrite(RED_PIN, !digitalRead(RED_PIN));
        break;
    case -2: //Disconnected mode
        digitalWrite(YELLOW_PIN, !digitalRead(GREEN_PIN));
        digitalWrite(RED_PIN, !digitalRead(GREEN_PIN));
        digitalWrite(GREEN_PIN, !digitalRead(GREEN_PIN));
        break;
    case -1: //No device
        if (digitalRead(GREEN_PIN) == LOW)
            digitalWrite(GREEN_PIN, HIGH);
        if (digitalRead(YELLOW_PIN) == LOW)
            digitalWrite(YELLOW_PIN, HIGH);
        if (digitalRead(RED_PIN) == LOW)
            digitalWrite(RED_PIN, HIGH);
        break;
    case 0: //No status
        if (digitalRead(GREEN_PIN) == HIGH)
            digitalWrite(GREEN_PIN, LOW);
        if (digitalRead(YELLOW_PIN) == HIGH)
            digitalWrite(YELLOW_PIN, LOW);
        if (digitalRead(RED_PIN) == HIGH)
            digitalWrite(RED_PIN, LOW);
        break;
    case 1: //Good
        if (digitalRead(GREEN_PIN) == LOW)
            digitalWrite(GREEN_PIN, HIGH);
        if (digitalRead(YELLOW_PIN) == HIGH)
            digitalWrite(YELLOW_PIN, LOW);
        if (digitalRead(RED_PIN) == HIGH)
            digitalWrite(RED_PIN, LOW);
        break;
    case 2: //Warning
        if (digitalRead(GREEN_PIN) == HIGH)
            digitalWrite(GREEN_PIN, LOW);
        if (digitalRead(YELLOW_PIN) == LOW)
            digitalWrite(YELLOW_PIN, HIGH);
        if (digitalRead(RED_PIN) == HIGH)
            digitalWrite(RED_PIN, LOW);
        break;
    case 3: //Bad
        if (digitalRead(GREEN_PIN) == HIGH)
            digitalWrite(GREEN_PIN, LOW);
        if (digitalRead(YELLOW_PIN) == HIGH)
            digitalWrite(YELLOW_PIN, LOW);
        if (digitalRead(RED_PIN) == LOW)
            digitalWrite(RED_PIN, HIGH);
        break;
    }
}

int GetStatus()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        WiFiClient client;
        if (client.connect(HOST, 80))
        {
            client.print("GET /api/indicators/" + MAC_ADRESS + "/status HTTP/1.1\r\n" +
                         "Host: " + HOST + "\r\n" +
                         "Content-Type: application/json\r\n" +
                         "Connection: close\r\n\r\n");
            if (client.connected())
            {
                int timeoutCounter = 0;
                //Wait till there is data available
                while (!client.available() && timeoutCounter < 50)
                {
                    delay(100);
                    timeoutCounter++;
                    RefreshStatusLED();
                }
                if (LED_STATUS < 0)
                    LED_STATUS = 0;
                RefreshStatusLED();
                String response = "";
                while (client.available())
                {
                    Serial.println("Reading string data");
                    response += client.readString();
                }
                int lastIndex = response.lastIndexOf("\r\n");
                String body = response.substring(lastIndex + 2, response.length());
                if (body.length() > 0)
                {
                    Serial.println("Body: " + body);
                    int status = body.toInt();
                    return status;
                }
                else
                {
                    Serial.println("Body is empty");
                }
            }
            else
            {
                Serial.println("Connection lost");
            }
        }
        else
        {
            Serial.println("Could not connect to the host: " + String(HOST));
        }
        client.stop();
    }
    return -3;
}

void Beep(){
    digitalWrite(SPEAKER_PIN, HIGH);
    delay(100);
    digitalWrite(SPEAKER_PIN, LOW);
}