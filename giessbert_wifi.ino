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
#include <WiFi.h>
#include <ArduinoJson.h>


#include "settings.h" // char[] arrays: ssid, pass, apiKey, lat, lon


int wifi_status = WL_IDLE_STATUS;

// Initialize the Wifi client library
WiFiClient apiclient;

// server address:
char apiserver[] = "https://api.openweathermap.org/data/2.5/onecall?units=metric";

StaticJsonDocument<26000> doc;

unsigned long lastConnectionTime = 0;            // last time you connected to the server, in milliseconds
const unsigned long postingInterval = 10L * 1000L; // delay between updates, in milliseconds

WiFiServer webserver(80);
bool wifi_noConnection = false;

void setup()
{
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv != "1.1.0")
  {
    Serial.println("Please upgrade the firmware");
  }

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
    delay(10000);
  }
  if(wifi_status == WL_CONNECTED && wifi_noConnection)
  {
    // you're connected now, so print out the status:
    printWifiStatus();
    wifi_noConnection = false;
  }

  
  // listen for incoming clients
  WiFiClient webclient = webserver.available();
  if (webclient)
  {
    Serial.println("new webclient");
    // an http request ends with a blank line
    bool currentLineIsBlank = true;
    while (webclient.connected())
    {
      if (webclient.available())
      {
        char c = webclient.read();
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank)
        {
          // send a standard http response header
          webclient.println("HTTP/1.1 200 OK");
          webclient.println("Content-Type: text/html");
          webclient.println("Connection: close");  // the connection will be closed after completion of the response
          webclient.println("Refresh: 5");  // refresh the page automatically every 5 sec
          webclient.println();
          webclient.println("<!DOCTYPE HTML>");
          webclient.println("<html>");
          // output the value of each analog input pin
          for (int analogChannel = 0; analogChannel < 6; analogChannel++)
          {
            int sensorReading = analogRead(analogChannel);
            webclient.print("analog input ");
            webclient.print(analogChannel);
            webclient.print(" is ");
            webclient.print(sensorReading);
            webclient.println("<br />");
          }
          webclient.println("</html>");
          break;
        }
        if (c == '\n')
        {
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r')
        {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    webclient.stop();
    Serial.println("webclient disonnected");

  }
  
  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  while (apiclient.available())
  {
    char c = apiclient.read();
    Serial.write(c);
  }

  // send out request to weather API
  httpRequest();

}

// this method makes a HTTP connection to the server:
void httpRequest()
{
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  apiclient.stop();

  // if there's a successful connection:
  if (apiclient.connect(apiserver, 80))
  {
    Serial.println("connecting...");
    // send the HTTP PUT request:
    apiclient.println("GET /latest.txt HTTP/1.1");
    apiclient.println(String("Host: ") + String(apiserver) + String("&lat=") + String(lat) + String("&lon=") + String(lon) + String("&appid=") + String(apiKey));
    apiclient.println("User-Agent: ArduinoWiFi/1.1");
    apiclient.println("Connection: close");
    apiclient.println();

    // note the time that the connection was made:
    lastConnectionTime = millis();
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
}
