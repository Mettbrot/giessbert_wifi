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

StaticJsonDocument<26000> doc;

Plant* plants[ANALOG_CHANNELS] = {NULL};

unsigned long api_lastConnectionTime = 0;            // last time you connected to the server, in milliseconds
unsigned long api_lastEpochTime = 0;
//const unsigned long postingInterval = 10L * 1000L; // delay between updates, in milliseconds

WiFiServer webserver(80);
bool wifi_noConnection = false;

void setup()
{
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  //while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  String fv = WiFi.firmwareVersion();
  if (fv != "1.3.0")
  {
    logger.println("Please upgrade the firmware");
  }

//setup plants
plants[1] = new Plant("Cherrytomate 1", 600, 1300);
plants[2] = new Plant("Rispentomate 1", 400, 1700);

}

void loop()
{
  // attempt to connect to Wifi network:
  short wifi_retry_count = 0;
  while (wifi_status != WL_CONNECTED && wifi_retry_count < 5)
  {
    ++wifi_retry_count;
    wifi_noConnection = true;
    logger.print("Attempting to connect to SSID: ");
    logger.println(ssid);
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
    logger.println("new webserver client");
    // an http request ends with a blank line
    bool currentLineIsBlank = true;
    while (webserver_client.connected())
    {
      if (webserver_client.available())
      {
        char c = webserver_client.read();
        logger.write(c);
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
    logger.println("webserver client disonnected");

  }
  
  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  while (api_client.available())
  {
    char c = api_client.read();
    logger.write(c);
  }

  if(api_lastConnectionTime  < (double)millis() - 100000.0)
  {
    logger.println(api_lastConnectionTime);
    // send out request to weather API
    httpRequest();
  } 
  if(analogRead(A0) > 900)
  {
    digitalWrite(6, HIGH);
  }
  else
  {
    digitalWrite(6, LOW);
  }
}


// this method makes a HTTP connection to the server:
void httpRequest()
{
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  api_client.stop();

  // if there's a successful connection:
  if (api_client.connectSSL(apiserver, 443))
  {
    logger.println("connecting...");
    // send the HTTP PUT request:
    api_client.println(String("GET /data/2.5/onecall?units=metric") + String("&lat=") + String(lat) + String("&lon=") + String(lon) + String("&appid=") + String(apiKey) + String(" HTTP/1.1"));
    api_client.println(String("Host: ") + String(apiserver));
    api_client.println("User-Agent: ArduinoWiFi/1.1");
    api_client.println("Connection: close");
    api_client.println();

    // note the time that the connection was made:
    api_lastConnectionTime = millis();
  }
  else
  {
    // if you couldn't make a connection:
    logger.println("connection failed");
  }
}


void printWifiStatus()
{
  // print the SSID of the network you're attached to:
  logger.print("SSID: ");
  logger.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  logger.print("IP Address: ");
  logger.println(String(String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3]).c_str());

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  logger.print("signal strength (RSSI):");
  logger.print(rssi);
  logger.println(" dBm");
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
    webserver_client.println(analogRead(A0));
    webserver_client.println("<br />");
    webserver_client.println("<table style=\"border:0px;text-align:center;\">");
    webserver_client.println("<tr style=\"font-weight:bold\">");
    webserver_client.println("<td>Port</td><td>Plant</td><td>Water today [ml]</td><td>Water total [l]</td><td>Action</td>");
    webserver_client.println("</tr>");
    for(int i = 0; i < ANALOG_CHANNELS; ++i)
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
    Serial.println(std::strlen(logger.getLog()));
    Serial.println(logger.getLog());
    webserver_client.println("</table>");
    webserver_client.println("<textarea>");
    //webserver_client.println(logger.getLog());
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
