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

#include <cstring>

#include "settings.h" // char[] arrays: ssid, pass, apiKey, lat, lon
#include "logging.h"
#include "plant.h"
#include "timecalc.h"

#include "jsmn.h"

int wifi_status = WL_IDLE_STATUS;

#define DIGITAL_CHANNELS 8
#define SUNSET_LIGHTS true
#define DST true

const int timezone = +1;


// Initialize the Wifi client library
WiFiClient api_client;

Logging logger(&Serial, 0);


// server address:
char apiserver[] = "api.openweathermap.org";

//StaticJsonDocument<26000> doc;
char api_response[16000] = {0};

//pump characteristics
const double lps = 0.0326; //liter per second
const double lps_large = 0.0517; //liter per second with only larger diameter

const int maxPlants = DIGITAL_CHANNELS-2;

Plant* plants[maxPlants] = {NULL};

//mapping table
int pins[DIGITAL_CHANNELS] = {5, 4, 3, 2, 1, 0, A6, A5};
const int pinWaterSensor = A1;
const int idxLights = 0;
const int idxPump = 1;
const int idxPlantOffset = 2;

int water_threshold = 90;

volatile bool waterAvailable = false;

bool sunset_reached_today = true; //disable lights until we have the first API call
bool sunrise_reached_today = true;

int state_watering_today = 0; //0 not watered //1 watered once //2 ... 
int currently_watering_plant_plus1 = 0;
unsigned long current_watering_start_millis = 0;

bool got_plant_characteristics = false;

unsigned long api_lastConnectionTime = 0; //TODO           // last time you connected to the server, in milliseconds
unsigned long api_epochOffset = 0;
unsigned long api_lastCall_millis = 0;
unsigned long secs_today_in15mins = 0;
unsigned long api_interval = 30L * 1000L; // try every 30 seconds at first
unsigned long api_poweron_days = 0;

bool api_parse_result = false;
unsigned long api_today_sunrise = 0;
unsigned long api_today_sunset = 0;

int watering_sunrise_offset = 6*3600;
int watering_sunset_offset = 0;

//MAX 7
const unsigned int days_forecast = 2;

struct weather 
{
  double temp;
  double humidity;
  int clouds;
  unsigned long sunrise;
  unsigned long sunset;
};

struct weather forecast[days_forecast] = {0};

double current_temp = 0;
double current_humidity = 0;
int current_clouds = 0;

WiFiServer webserver(80);
bool wifi_noConnection = false;
unsigned long wifi_lastTry_millis = 0;
unsigned long water_lastRead_millis = 0;


void setup()
{
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  //while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  //setup plants statically for now TODO
  got_plant_characteristics = true;
  plants[1] = new Plant("Cherrytomate 1", 200, 1300, 1500);
  plants[3] = new Plant("Rispentomate 1", 300, 1700, 1500);
  plants[4] = new Plant("Kumquats", 300, 1800, 2500);
 
  pinMode(pinWaterSensor, INPUT_PULLUP);
  //attachInterrupt(digitalPinToInterrupt(pinWaterSensor), disableEnablePump, CHANGE);
  pinMode(pins[idxPump], OUTPUT);
  digitalWrite(pins[idxPump], HIGH);
  pinMode(pins[idxLights], OUTPUT);
  digitalWrite(pins[idxLights], HIGH);

  for(int i = 0; i < maxPlants; ++i)
  {
    if(plants[i] == NULL)
    {
      continue;
    }
    pinMode(pins[idxPlantOffset+i], OUTPUT);
    digitalWrite(pins[idxPlantOffset+i], HIGH);
  }
  
  //memset(api_response, 0, sizeof(api_response));
}

void loop()
{
  // attempt to connect to Wifi network:
  short wifi_retry_count = 0;
  wifi_status = WiFi.status();
  while (wifi_status != WL_CONNECTED && wifi_retry_count < 5 && (!wifi_lastTry_millis || wifi_lastTry_millis + 10000 < millis()))
  {
    ++wifi_retry_count;
    wifi_noConnection = true;
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
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

  //check if we can water
  if(waterAvailable && api_epochOffset && api_poweron_days)
  {
    //check if we need to water
    if(state_watering_today == 0 && offsetMillis() > api_today_sunrise+watering_sunrise_offset)
    {
      //start watering
      selectCurrentPlantToWater();
    }
    else if(state_watering_today == 1 && offsetMillis() > api_today_sunset+watering_sunset_offset)
    {
      //same as before
      selectCurrentPlantToWater();
    }
    //TODO even more states???
  }

  //check if we are done watering for now
  if(currently_watering_plant_plus1 == maxPlants+1)
  {
    //we watered all plants
    currently_watering_plant_plus1 = 0;
    ++state_watering_today;
    logger.print("w_d");
    logger.println(state_watering_today);
    //disable pump
    digitalWrite(pins[idxPump], HIGH);
    //disable all valves
    for(int i = 0; i < maxPlants; ++i)
    {
      if(plants[i] == NULL)
      {
        continue;
      }
      digitalWrite(pins[idxPlantOffset+i], HIGH);
    }
  }

  //watering variables were set, turn on valves and pump
  if(waterAvailable && currently_watering_plant_plus1)
  {
    //turn on pump and one valve
    digitalWrite(pins[idxPump], LOW);
    digitalWrite(pins[idxPlantOffset+currently_watering_plant_plus1-1], LOW);

    for(int i = 0; i < maxPlants; ++i)
    {
      if(plants[i] == NULL)
      {
        continue;
      }
      if(i == currently_watering_plant_plus1-1)
      {
        continue;
      }
      //disable all other valves
      digitalWrite(pins[idxPlantOffset+i], HIGH);
    }
  }
  else
  {
    //for safety on every loop
    digitalWrite(pins[idxPump], HIGH);
  }
          

  //if its sunset turn on the lights:
  if(SUNSET_LIGHTS && !sunset_reached_today && now() > api_today_sunset)
  {
    digitalWrite(pins[idxLights], LOW);
    //only do this once, we may want to turn them off manually
    sunset_reached_today = true;
  }
  
  //15 minutes before the day is over (on our timing) we shorten the api calling interval to get an accurate reading on the time
  if(elapsedSecsToday(correctTimezoneDST(now()+15*60, 1, true))+75000 < secs_today_in15mins) //this is true if its midnight in 15 minutes
  {
    api_interval = 60*1000; // every minute
  }
  //save secs today in 15 minutes:
  secs_today_in15mins = elapsedSecsToday(correctTimezoneDST(now()+15*60, 1, true));

  // everything after here is useless without wifi
  if(wifi_status == WL_CONNECTED)
  {
    // listen for incoming clients
    WiFiClient webserver_client = webserver.available();
    if (webserver_client)
    {
      logger.println("wc_c");
      // an http request ends with a blank line
      String currentLine = "";
      while (webserver_client.connected())
      {
        if (webserver_client.available())
        {
          char c = webserver_client.read();
          if (c == '\n')
          {
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0)
            {
              printWebPage(webserver_client);
              break;
            }
            else
            {
              // you're starting a new line
              currentLine = "";
            }
          }
          else if (c != '\r')
          {
            // you've gotten a character on the current line
            currentLine += c;
            
            //check requests from the web here:
            if (currentLine.startsWith("GET") && currentLine.endsWith("HTTP/1.1"))
            {
              //we can analyze this:
              int lights = currentLine.indexOf("lights=");
              int water200 = currentLine.indexOf("water200=");
              if(lights != -1)
              {
                String str = extractToNextDelimiter(currentLine.substring(lights+7));
                if(str == "on")
                {
                  logger.println("m_lon");
                  digitalWrite(pins[idxLights], LOW);
                }
                else if(str == "off")
                {
                  logger.println("m_loff");
                  digitalWrite(pins[idxLights], HIGH);
                }
              }
              if(water200 != -1)
              {
                String str = extractToNextDelimiter(currentLine.substring(water200+9));
              }
            }
          }
 
        }
      }  
      // close the connection:
      webserver_client.stop();
      logger.println("wc_d");
    }
    

    bool api_currentLineIsBlank = true;
    int pos = 0;
    bool first_read = true;
    while (api_client.available())
    {
      if(first_read)
      {
        //this is the first time, log event
        logger.println("api_i");
        first_read = false;
      }
      char c = api_client.read();
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
        api_parse_result = true; //this line is still \n 
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
      logger.println("api_p");
      if (!api_client.connected())
      {
          Serial.println("disconnecting from server.");
          api_client.stop();
      }
      if(!got_plant_characteristics)
      {
        //this is the characteristics call, parse differently:
      }
      else
      {
        //api has been called recently, parse newest weather data
        {
            //parse time offset:
            int r;
            jsmn_parser p;
            jsmntok_t t[56]; // this is enough for global data section
          
            jsmn_init(&p);
            r = jsmn_parse(&p, api_response, strlen(api_response), t, sizeof(t)/sizeof(t[0]));
        
        
            current_temp = atof(api_response+t[16].start);
            current_humidity = atof(api_response+t[22].start);
            current_clouds = atoi(api_response+t[28].start);
      
            unsigned long sunrise = atol(api_response+t[12].start);
            //roll over day on midnight and powerup:
            if(api_today_sunrise != sunrise)
            {
              //roll over day here!
              logger.println("nd");
              
              //sync internal clock:
              //calculate difference since last sync:
              long unsigned api_lastEpochOffset = api_epochOffset;
              api_epochOffset = atol(api_response+t[10].start) - (unsigned long)((double) api_lastCall_millis / 1000.0);
              //offset calculation is biased, because we get timestamp from last measurement TODO: what is the frequency of updates? ~15 minutes
              //we dont care though, if we are 15 minutes behind...
              logger.setOffset(api_epochOffset);
              logger.print("diff: ");
              double diff = (double)api_epochOffset - (double)api_lastEpochOffset;
              logger.println(diff);

              if(!api_lastEpochOffset)
              {
                //if api_offset was zero before this newday, this was poweron. Set to 0
                api_poweron_days = 0;
              }
              else
              {
                ++api_poweron_days;
              }
      
              //we have our newday, set interval back to normal:
              api_interval = 3600 * 1000;
      
              //reset daily water counter for all plants
              for(int i = 0; i < maxPlants; ++i)
              {
                if(plants[i] == NULL)
                {
                  continue;
                }
                plants[i]->resetDailyWater();
              }
              state_watering_today = 0;
              //turn off lights
              digitalWrite(pins[idxLights], HIGH);
              sunset_reached_today = false;
              sunrise_reached_today = false;
            }
            api_today_sunrise = sunrise;
            api_today_sunset = atol(api_response+t[14].start);
        }
        //second parsing round, starting at "daily" - get weather today & tomorrow
        //parse weather forecast:
        //search for "daily"
        char* daily = std::strstr(api_response, "\"daily\"");
        daily = std::strstr(daily, "{"); //go to next array start
        if(daily)
        {
          unsigned int offset = 0;
          for(int i = 0; i < days_forecast; ++i)
          {
            if(strlen(daily) <= offset)
            {
              break;
            }
            int r;
            jsmn_parser p;
            jsmntok_t t[66];
            
            
            jsmn_init(&p);
            r = jsmn_parse(&p, daily+offset, strlen(daily+offset), t, sizeof(t)/sizeof(t[0]));

            forecast[i].sunrise = atoi(daily+offset+t[4].start);
            forecast[i].sunset = atoi(daily+offset+t[6].start);
            
            for(int j = 0; j < sizeof(t)/sizeof(t[0]); ++j)
            {
              //temp
              if(t[j].type == 3 && t[j].end - t[j].start == 4)
              {
                //could be temp, do strcmp:
                char test[5] = {0};
                memcpy(test, daily+offset+t[j].start, 4);
                if(strcmp(test, "temp") == 0)
                {
                  //this is temp, read daily value
                  forecast[i].temp = atof(daily+offset+t[j+3].start);
                }
              }
              //humidity
              else if(t[j].type == 3 && t[j].end - t[j].start == 8)
              {
                //could be humidity, do strcmp:
                char test[9] = {0};
                memcpy(test, daily+offset+t[j].start, 8);
                if(strcmp(test, "humidity") == 0)
                {
                  //this is temp, read daily value
                  forecast[i].humidity = atof(daily+offset+t[j+1].start);
                }
              }
              //clouds
              else if(t[j].type == 3 && t[j].end - t[j].start == 6)
              {
                //could be clouds, do strcmp:
                char test[7] = {0};
                memcpy(test, daily+offset+t[j].start, 6);
                if(strcmp(test, "clouds") == 0)
                {
                  //this is clouds, read daily value
                  forecast[i].clouds = atoi(daily+offset+t[j+1].start);
                }
              }
              //stop if we are past this day
              if(t[j].start > t[0].end)
              {
                break;
              }
            }
            offset += t[0].end; //next read starts at next day object
          }
        }
        api_parse_result = false;
      }
    }

    if(got_plant_characteristics && (api_lastConnectionTime  < (double)millis() - (double)api_interval))
    {
      logger.println("api_r");
      // send out request to weather API
      api_lastCall_millis = millis();
      httpRequest();
    }
    
  }
  
  //check water status every 100ms:
  if(water_lastRead_millis + 100 < millis())
  {
    disableEnablePump();
    water_lastRead_millis = millis();
  }
    
}

void disableEnablePump()
{
  //only change on discrepancy between waterAvailable and actual measurement
  if(analogRead(pinWaterSensor) < water_threshold && waterAvailable == false)
  {
    logger.println("wt_t");
    waterAvailable = true;
    //were we watering?
    if(currently_watering_plant_plus1)
    {
      //reset millis of current watering, and continue
      current_watering_start_millis = millis();
    }
  }
  else if(analogRead(pinWaterSensor) >= water_threshold && waterAvailable == true)
  {
    logger.println("wt_f");
    waterAvailable = false;
    //also stop pump right away!
    digitalWrite(pins[idxPump], HIGH);
    //were we watering?
    if(currently_watering_plant_plus1)
    {
      //also stop valve of this plant:
      digitalWrite(pins[idxPlantOffset+currently_watering_plant_plus1-1], HIGH);
      //and save already watered amount:
      
      double watered = (millis() - current_watering_start_millis) * lps; //in ml
      plants[currently_watering_plant_plus1-1]->addWater(watered);
    }
  }
}

void selectCurrentPlantToWater()
{
  double watered = (millis() - current_watering_start_millis) * lps; //in ml
  //if this is our first plant, OR if the current plant's watering is done, switch to the next one
  if(!currently_watering_plant_plus1 || watered >= plants[currently_watering_plant_plus1-1]->calcWaterAmout(forecast[0].temp, forecast[0].humidity, forecast[0].clouds, api_today_sunset - api_today_sunrise))
  {
    if(currently_watering_plant_plus1)
    {
      //save water amount to plant:
      plants[currently_watering_plant_plus1-1]->addWater(watered);
    }
    //next plant
    for(int i = currently_watering_plant_plus1; i <= maxPlants; ++i) //allow to go to maxPlants to detect state after last plant
    {
      if(plants[i] == NULL)
      {
        continue;
      }            
      //start watering of next plant:
      current_watering_start_millis = millis();
      currently_watering_plant_plus1 = i+1;
      break; //break if we found a plant
    }
  }
}

unsigned long offsetMillis(unsigned long mil)
{
  return api_epochOffset + (unsigned long)((double) mil / 1000.0);
}

unsigned long offsetMillis()
{
  return offsetMillis(millis());
}

unsigned long now()
{
  return offsetMillis();
}

// this method makes a HTTP connection to the server:
void httpRequest()
{
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  //memset(api_response, 0, sizeof(api_response));
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

String extractToNextDelimiter(String str)
{
  //cut off at & or blank whichever comes first
  int idxand = str.indexOf("&");
  int idxblank = str.indexOf(" ");
  int idx = -1;
  
  if(idxand != -1 && idxblank != -1)
  {
    idx = idxand < idxblank ? idxand : idxblank;
  }
  else if(idxand != -1) //idxblank is -1
  {
    idx = idxand;
  }
  else if(idxblank != -1) //idxand is -1
  {
    idx = idxblank;
  }
  else // both are -1
  {
    return str;
  }
  return str.substring(0, idx);
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
    webserver_client.print("<td>Last Weather Update</td><td>");
    {
      char date[25] = {0};
      formattedDate(date, offsetMillis(api_lastCall_millis), 1, true);
      webserver_client.print(date);
    }
    webserver_client.println("</td>");
    webserver_client.println("</tr>");
    webserver_client.println("<tr>");
    webserver_client.print("<td>Poweron days</td><td>");
    webserver_client.print(api_poweron_days);
    webserver_client.println("</td>");
    webserver_client.println("</tr>");
    webserver_client.println("<tr>");
    webserver_client.print("<td>Next watering today</td><td>");
    if(api_poweron_days && state_watering_today == 0)
    {
      char date[25] = {0};
      formattedDate(date, api_today_sunrise+watering_sunrise_offset, 1, true);
      webserver_client.print(date);
    }
    else if(api_poweron_days && state_watering_today == 1)
    {
      char date[25] = {0};
      formattedDate(date, api_today_sunset+watering_sunset_offset, 1, true);
      webserver_client.print(date);
    }
    else
    {
      webserver_client.print("-");
    }
    webserver_client.println("</td>");
    webserver_client.println("</tr>");
    webserver_client.println("<tr>");
    webserver_client.print("<td>Weather current</td><td>");
    webserver_client.print(current_temp);
    webserver_client.print("°C, clouds ");
    webserver_client.print(current_clouds);
    webserver_client.print("%, humidity ");
    webserver_client.print(current_humidity);
    webserver_client.println("%</td>");
    webserver_client.println("</tr>");
    for(int i = 0; i < days_forecast; ++i)
    {
      webserver_client.println("<tr>");
      webserver_client.print("<td>Weather ");
      if(i == 0)
      {
        webserver_client.print("today");
      }
      else
      {
        for(int j = 1; j < i; ++j)
        {
           webserver_client.print("the day after ");
        }
        webserver_client.print("tomorrow");
      }
      webserver_client.print("</td><td>");
      webserver_client.print(forecast[i].temp);
      webserver_client.print("°C, clouds ");
      webserver_client.print(forecast[i].clouds);
      webserver_client.print("%, humidity ");
      webserver_client.print(forecast[i].humidity);
      webserver_client.print("%</td>");
      webserver_client.println("</tr>");
    }
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
    webserver_client.println("<td>Port</td><td>Plant</td><td>Planned today [ml]</td><td>Watered today [ml]</td><td>Watered total [l]</td><td>Action</td>");
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
      webserver_client.print(plants[i]->dailyWaterTotal(forecast[0].temp, forecast[0].humidity, forecast[0].clouds, api_today_sunset - api_today_sunrise));
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
    webserver_client.println("<textarea rows=\"20\" cols=\"40\">");
    webserver_client.println(logger.getLog());
    webserver_client.println("</textarea>");
    /*
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
    */
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
