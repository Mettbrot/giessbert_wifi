/*
* @file giessbert_wifi.ino
* @author Moritz Haseloff
* @date 05 Apr 2020
* @brief Sketch for internet enabled version of gießbert. Gets weather info from
* the net and adjusts watering accordingly
*
* ideas taken from "Repeating Wifi Web Client" from arduino.cc
* @details
*/

#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h>

#include <cstring>

#include "settings.h" // char[] arrays: ssid, pass, apiKey, lat, lon
#include "logging.h"
#include "plant.h"

int wifi_status = WL_IDLE_STATUS;

#define ANALOG_CHANNELS 8

// Initialize the Wifi client library
WiFiClient api_client;

Logging logger(&Serial, 3000);


// server address:
char apiserver[] = "api.openweathermap.org";

//StaticJsonDocument<26000> doc;
char* const api_response = static_cast<char*>(malloc(20000));

//pump characteristics
const double lps = 0.0326; //liter per second
const double lps_large = 0.0517; //liter per second with only larger diameter

#define DIGITAL_CHANNELS 8
const int maxPlants = DIGITAL_CHANNELS-2;

Plant* plants[maxPlants] = {NULL};

unsigned long api_lastConnectionTime = 0;            // last time you connected to the server, in milliseconds
unsigned long api_lastEpochTime = 0;
//const unsigned long postingInterval = 10L * 1000L; // delay between updates, in milliseconds

WiFiServer webserver(80);
bool wifi_noConnection = false;

bool test = false;
volatile bool waterAvailable = true;

const int pinWaterSensor = 1;
const int pinPump = 2;
const int pinPlantOffset = 3;

void setup()
{
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  //while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  //setup plants
  plants[1] = new Plant("Cherrytomate 1", 600, 1300);
  plants[2] = new Plant("Rispentomate 1", 400, 1700);

  pinMode(LED_BUILTIN, OUTPUT); 
  pinMode(pinWaterSensor, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinWaterSensor), disableEnablePump, CHANGE);
  pinMode(pinPump, OUTPUT);
  digitalWrite(pinPump, HIGH);

  for(int i = 0; i < maxPlants; ++i)
  {
    if(plants[i] == NULL)
    {
      continue;
    }
    pinMode(pinPlantOffset+i, OUTPUT);
    digitalWrite(pinPlantOffset+i, HIGH);
  }
  
  memset(api_response, 0, 20000);
}

void loop()
{
  // attempt to connect to Wifi network:
  short wifi_retry_count = 0;
  while (wifi_status != WL_CONNECTED && wifi_retry_count < 5)
  {
    ++wifi_retry_count;
    wifi_noConnection = true;
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifi_status = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    delay(10000); // TODO remove this!
  }
  if(wifi_status == WL_CONNECTED && wifi_noConnection)
  {
    // you're connected now, so print out the status:
    printWifiStatus();
    wifi_noConnection = false;
    //begin listening
    webserver.begin();
  }

  
  // listen for incoming clients
  WiFiClient webserver_client = webserver.available();
  if (webserver_client)
  {
    logger.println("wc_c");
    // an http request ends with a blank line
    bool currentLineIsBlank = true;
    while (webserver_client.connected())
    {
      if (webserver_client.available())
      {
        char c = webserver_client.read();
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank)
        {
          printWebPage(webserver_client);
          break;
        }
        if (c == '\n')
        {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r')
        {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    webserver_client.stop();
    logger.println("wc_d");

  }
  

  if(api_client.available())
  {
    logger.println("api_i");
  }
  int pos = 0;
  while (api_client.available())
  {
    char c = api_client.read();
    if(pos >= 20000)
    {
      break;
    }
    api_response[pos] = c;
    ++pos;
    Serial.write(c);
  }
  

  if(api_lastConnectionTime  < (double)millis() - 50000.0)
  {
    logger.println("api_r");
    // send out request to weather API
    httpRequest();
  }

}

void disableEnablePump()
{
  if(digitalRead(pinWaterSensor) == LOW)
  {
    waterAvailable = true;
  }
  else
  {
    waterAvailable = false;
    //also stop pump right away!
    digitalWrite(pinPump, HIGH);
  }
}



// this method makes a HTTP connection to the server:
void httpRequest()
{
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  memset(api_response, 0, 20000);
  api_client.stop();

  // if there's a successful connection:
  if (api_client.connectSSL(apiserver, 443))
  {
    Serial.println("connecting...");
    // send the HTTP PUT request:
    char adr[120] = {0};
    strcpy(adr+0, "GET /data/2.5/onecall?units=metric&lat=");
    int pos = strlen("GET /data/2.5/onecall?units=metric&lat=");
    strcpy(adr+pos, lat);
    pos += strlen(lat);
    strcpy(adr+pos, "&lon=");
    pos += strlen("&lon=");
    strcpy(adr+pos, lon);
    pos += strlen(lon);
    strcpy(adr+pos, "&appid=");
    pos += strlen("&appid=");
    strcpy(adr+pos, apiKey);
    pos += strlen(apiKey);
    strcpy(adr+pos, " HTTP/1.1");
    Serial.println(adr);

    char serv[] = "Host: api.openweathermap.org";

    api_client.println(adr);
    api_client.println(serv);
    api_client.println("User-Agent: ArduinoWiFi/1.1");
    api_client.println("Connection: close");
    api_client.println();

    // note the time that the connection was made:
    api_lastConnectionTime = millis();
  }
  else
  {
    // if you couldn't make a connection:
    Serial.println("connection failed");
  }
}


void printWifiStatus()
{
  // print the SSID of the network you're attached to:
  logger.println("wf_c");
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
}

void printWebPage(WiFiClient& webserver_client)
{
    // send a standard http response header
    webserver_client.println("HTTP/1.1 200 OK");
    webserver_client.println("Content-Type: text/html");
    webserver_client.println("Connection: close");  // the connection will be closed after completion of the response
    webserver_client.println();

    webserver_client.println("<!DOCTYPE HTML>");
    webserver_client.println("<html>");
    webserver_client.println("<head>");
    webserver_client.println("<meta charset=\"utf-8\">");
    webserver_client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
    webserver_client.println("</head>");
    webserver_client.println("<h1>Gießbert 4.0</h1>");
    webserver_client.println("<form method=\"get\" action=\"\">");
    webserver_client.println("");
    webserver_client.println("<table style=\"border:0px\">");
    webserver_client.println("<tr>");
    webserver_client.println("<td>Date & Time</td><td>09.04.2020 17:36</td>");
    webserver_client.println("</tr>");
    webserver_client.println("<tr>");
    webserver_client.println("<td>Weather today</td><td>25°C, clouds 56%, humidity 34%</td>");
    webserver_client.println("</tr>");
    webserver_client.println("<tr>");
    webserver_client.println("<td>Weather tomorrow</td><td>20°C, clouds 2%, humidity 69%</td>");
    webserver_client.println("</tr>");
    webserver_client.println("<tr>");
    webserver_client.print("<td>WiFi Status </td><td>");
    // print the received signal strength:
    long rssi = WiFi.RSSI();
    webserver_client.print("signal strength (RSSI):");
    webserver_client.print(rssi);
    webserver_client.println(" dBm</td>");
    webserver_client.println("</tr>");
    webserver_client.println("<tr>");
    webserver_client.println("<td>Lights </td><td><button type=\"submit\" name=\"lights\" value=\"on\">ON</button><button type=\"submit\" name=\"lights\" value=\"off\">OFF</button></td>");
    webserver_client.println("</tr>");
    webserver_client.println("</table>");
    webserver_client.println("<br />");
    if(!waterAvailable)
    {
      webserver_client.println("<h2 style=\"color: red\">Water is empty!</h2>");
    }
    webserver_client.println("<table style=\"border:0px;text-align:center;\">");
    webserver_client.println("<tr style=\"font-weight:bold\">");
    webserver_client.println("<td>Port</td><td>Plant</td><td>Water today [ml]</td><td>Water total [l]</td><td>Action</td>");
    webserver_client.println("</tr>");
    for(int i = 0; i < maxPlants; ++i)
    {
      if(plants[i] == NULL)
      {
        continue;
      }
      webserver_client.println("<tr>");
      webserver_client.print("<td>");
      webserver_client.print(i);
      webserver_client.print("</td><td>");
      webserver_client.print(plants[i]->getName());
      webserver_client.print("</td><td>");
      webserver_client.print(plants[i]->getDailyWater());
      webserver_client.print("</td><td>");
      webserver_client.print(plants[i]->getTotalWater());
      webserver_client.print("</td><td><button type=\"submit\" name=\"water200\" value=\"");
      webserver_client.print(i);
      webserver_client.println("\">Water 200ml</button></td>");
      webserver_client.println("</tr>");
    }
    webserver_client.println("</table>");
    webserver_client.println("<textarea>");
    webserver_client.println(logger.getLog());
    webserver_client.println("</textarea>");
    webserver_client.println("<textarea>");
    //feed big data slowly to the bus:
    for(int i = 0; i <= strlen(api_response)/2048; ++i)
    {
      char tmp[2049] = {0};
      strncpy(tmp,  api_response+(i*2048), 2048);
      webserver_client.print(tmp);
    }
    webserver_client.println();
    webserver_client.println("</textarea>");
    webserver_client.println("</form>");
    webserver_client.println("</html>");
    /*
  // output the value of each analog input pin
  for (int analogChannel = 0; analogChannel < 6; analogChannel++)
  {
    int sensorReading = analogRead(analogChannel);
    webserver_client.print("analog input ");
    webserver_client.print(analogChannel);
    webserver_client.print(" is ");
    webserver_client.print(sensorReading);
    webserver_client.println("<br />");
  }
  */
}
