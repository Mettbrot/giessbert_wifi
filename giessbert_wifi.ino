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


#include "settings.h" // char[] arrays: ssid, pass, apiKey, lat, lon

int wifi_status = WL_IDLE_STATUS;

#define DIGITAL_CHANNELS 8

// Initialize the Wifi client library
WiFiSSLClient api_client;

// server address:
char apiserver[] = "www.google.com";

char api_response[8000] = {0};


unsigned long api_lastConnectionTime = 0; //TODO           // last time you connected to the server, in milliseconds
unsigned long api_epochOffset = 0;
unsigned long api_lastCall_millis = 0;
unsigned long secs_today_in15mins = 0;
unsigned long api_interval = 30L * 1000L; // try every 30 seconds at first
unsigned long api_poweron_days = 0;

int api_parse_result = 0;

WiFiServer webserver(80);
bool wifi_noConnection = false;
unsigned long wifi_lastTry_millis = 0;


void setup()
{
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }


}

void loop()
{
  // attempt to connect to Wifi network:
  wifi_status = WiFi.status();
  while (wifi_status != WL_CONNECTED && (!wifi_lastTry_millis || wifi_lastTry_millis + 10000 < millis()))
  {
    wifi_noConnection = true;
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network.
    wifi_status = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    wifi_lastTry_millis = millis();
  }
  if(wifi_status == WL_CONNECTED && wifi_noConnection)
  {
    // you're connected now, so print out the status:
    printWifiStatus();
    wifi_noConnection = false;
    //begin listening
    webserver.begin();
  }

  // everything after here is useless without wifi
  if(wifi_status == WL_CONNECTED)
  {
    bool api_currentLineIsBlank = true;
    int pos = 0;
    bool first_read = true;
    while (api_client.available())
    {
      if(first_read)
      {
        //this is the first time, log event
        Serial.println("api incomming");
        first_read = false;
      }
      char c = api_client.read();
      Serial.print(c);
      if(api_parse_result) //this char is the first after the empty newline
      {
        if(pos >= sizeof(api_response))
        {
          break;
        }
        api_response[pos] = c;
        ++pos;
      }
  
      if (c == '\n' && api_currentLineIsBlank)
      {
        api_parse_result = 1; //this line is still \n 
      }
      if (c == '\n')
      {
        // you're starting a new line
        api_currentLineIsBlank = true;
      }
      else if (c != '\r')
      {
        // you've gotten a character on the current line
        api_currentLineIsBlank = false;
      }
    }
    //terminate with null byte
    if(pos >= sizeof(api_response))
    {
      pos = sizeof(api_response) - 1;
    }
    api_response[pos] = 0x0;
    
    if(api_parse_result)
    {
      Serial.print("api parse turn ");
      Serial.println(api_parse_result);
      //parse here
      ++api_parse_result;
    }

    if(api_parse_result && !api_client.available())
    {
      Serial.println("disconnecting from server");
      api_client.stop();
      api_parse_result = 0;
    }
    
    // listen for incoming clients
    WiFiClient webserver_client = webserver.available();
    if (webserver_client)
    {
      Serial.println("client connected");
      // an http request ends with a blank line
      bool currentLineBlank = true;
      while (webserver_client.connected())
      {
        if (webserver_client.available())
        {
          char c = webserver_client.read();
          if (c == '\n')
          {
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLineBlank)
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
              webserver_client.println("<h1>Website</h1>");
              webserver_client.println("</html>");
              break;
            }
            else
            {
              // you're starting a new line
              currentLineBlank = true;
            }
          }
          else if (c != '\r')
          {
            // you've gotten a character on the current line
            currentLineBlank = false;
            
          }
 
        }
      }  
      // close the connection:
      webserver_client.stop();
      Serial.println("client disconnected");
    }
    

    if(api_lastConnectionTime  < (double)millis() - (double)api_interval)
    {
      Serial.println("api requesting");
      // send out request to weather API
      api_lastCall_millis = millis();
      httpRequest();
    }
    
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
    Serial.println("connecting...");
    // send the HTTP PUT request:
    api_client.println("GET / HTTP/1.1");
    api_client.println("Host: www.google.com");
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
  Serial.println("wf_c");
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
