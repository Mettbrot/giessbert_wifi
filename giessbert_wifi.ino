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


#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h> 

#include <cstring>
#include <cstdio>

#include "settings.h" // char[] arrays: ssid, pass, apiKey, lat, lon
#include "logging.h"
#include "plant.h"
#include "timecalc.h"

#include "jsmn.h"

int wifi_status = WL_IDLE_STATUS;

#define DIGITAL_CHANNELS 8
#define SUNSET_LIGHTS true

const int timezone = +1;


// Initialize the Wifi client library
WiFiClientSecure api_client;

Logging logger(0);

// server address:
char apiserver[] = "api.openweathermap.org";
const char apiserver_fingerprint[] PROGMEM = "EE AA 58 6D 4F 1F 42 F4 18 5B 7F B0 F2 0A 4C DD 97 47 7D 99";

char api_response[2000] = {0};

const int maxPlants = DIGITAL_CHANNELS-2;

//pump characteristics
const double lps = 0.0078947; //liter per second with long small tube and valve diameter and 50cm height
const double lps_small = 0.0326; //liter per second with only small diameter
const double lps_large = 0.0517; //liter per second with only larger diameter
double lps_array[maxPlants] = {0}; //measured liter per second with actual pipe length and perforation

Plant* plants[maxPlants] = {NULL};
int plants_water_manually[maxPlants] = {0};

//mapping table
int pins[DIGITAL_CHANNELS] = {D0, D1, D2, D3, D4, D5, D6, D7};
const int pinWaterSensor = A0;
const int idxLights = 0;
const int idxPump = 1;
const int idxPlantOffset = 2;

int water_threshold = 90;

volatile bool waterAvailable = false;

bool sunset_reached_today = true; //disable lights until we have the first API call
bool sunrise_reached_today = true;

int state_watering_today = 0; //0 not watered //1 watering first time //2 watering first finished //3 watering second time // 4 watering second finished //-1 watering manually
int currently_watering_plant_plus1 = 0;
unsigned long current_watering_start_millis = 0;

bool got_plant_characteristics = false;

unsigned long api_epochOffset = 0;
unsigned long api_timezoneOffset = 0;
unsigned long api_lastCall_millis = 0;
unsigned long secs_today_in15mins = 0;
unsigned long api_interval = 30L * 1000L; // try every 30 seconds at first
unsigned long api_poweron_days = 0;

int api_parse_result = 0; //0 no header, 1 first values, 2 searching for forecast, 3. daily forecast

unsigned long api_today_sunrise = 0;
unsigned long api_today_sunset = 0;

int watering_sunrise_offset = 0;
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

const unsigned int session_id_length = 6;
const unsigned int session_id_amount = 10;
struct session_id
{
  unsigned long expiration;
  char id[session_id_length+1]; //always null terminated
};


struct session_id web_sessions[session_id_amount] = {0};

void setup()
{
  //Initialize serial and wait for port to open:
  Serial.begin(9600);

  //setup plants statically for now TODO
  got_plant_characteristics = true;
  plants[0] = new Plant("Clematis & Sonnenblume", 2, 50, 500, 300);
  lps_array[0] = lps * 0.76174371;
  //plants[1] = new Plant("6 Tomaten", 6, 80, 1200, 600);
  lps_array[1] = lps * 923.0 / 1000.0;
  plants[2] = new Plant("Zucchini", 1, 80, 1000, 700);
  lps_array[2] = lps * 0.88942307692;
  //plants[3] = new Plant("Kumquats", 1, 300, 2000, 2500); //alle 3 Tage
  plants[3] = new Plant("2 Kräuter", 2, 70, 800, 600);
  lps_array[3] = lps * 0.277536;
  plants[4] = new Plant("4 Erdbeeren", 4, 100, 600, 500);
  lps_array[4] = lps * 0.83838383838;
  plants[5] = new Plant("6 Tomaten", 6, 80, 1200, 600);
  lps_array[5] = lps * 0.82830774853;
 
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
  
}

void loop()
{
  // attempt to connect to Wifi network:
  wifi_status = WiFi.status();
  while (wifi_status != WL_CONNECTED && (!wifi_lastTry_millis || wifi_lastTry_millis + 10000 < millis()))
  {
    wifi_noConnection = true;
    //set hostname
    WiFi.mode(WIFI_STA);        //Only Station No AP, This line hides the viewing of ESP as wifi hotspot
    //WiFi.setHostname("giessbert");
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

  //calc if we need to water manually
  int manual_active = 0;
  for(int i = 0; i < maxPlants; ++i)
  {
    if(plants[i] == NULL)
    {
      continue;
    }
    manual_active += plants_water_manually[i];
  }

  //check if we can water
  if(waterAvailable)
  {
    //check if we water manually
    if(manual_active)
    {
      state_watering_today = -1;
    }
    else if(api_epochOffset && api_poweron_days)
    {
      if(state_watering_today >= 0 && state_watering_today%2 == 0)
      {
        //check if we need to water check latest state first
        if(state_watering_today == 2 && offsetMillis() > api_today_sunset+watering_sunset_offset)
        {
          //switch to 3 to start watering
          state_watering_today = 3;
        }
        else if(state_watering_today == 0 && offsetMillis() > api_today_sunrise+watering_sunrise_offset)
        {
          //switch to 1 to start watering
          state_watering_today = 1;
        }
        //TODO even more states???
      }
      //do nothing on -1 and other uneven states (we are watering, let it run through)
    }

    //this makes watering independant from starting conditions
    if(state_watering_today%2) //uneven states means we are watering
    {
      //check if we are done / switch plants
      selectCurrentPlantToWater();
    }
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
  if(elapsedSecsToday(now()+api_timezoneOffset+15*60)+75000 < secs_today_in15mins) //this is true if its midnight in 15 minutes
  {
    api_interval = 60*1000; // every minute
  }
  //save secs today in 15 minutes:
  secs_today_in15mins = elapsedSecsToday(now()+api_timezoneOffset+15*60);


  if(got_plant_characteristics && (api_lastCall_millis  < (double)millis() - (double)api_interval))
  {
    logger.println("api_r");
    // send out request to weather API
    api_lastCall_millis = millis();
    httpRequest();
    delay(500); //this is ok here
  }


  // everything after here is useless without wifi
  if(wifi_status == WL_CONNECTED)
  {
    bool api_currentLineIsBlank = true;
    bool api_receive_after_header = false;
    unsigned int api_receive_pos = 0;

    while (api_client.available())
    {
      bool found_daily = false;
      char c = api_client.read();
      if(api_parse_result) //this char is the first after the empty newline
      {
        api_response[api_receive_pos] = c;

        //check if the last 7 digits where "daily"
        char comp[] = "\"daily\"";
        if(api_receive_pos >= 6 && api_response[api_receive_pos-6] == comp[0] &&
                                   api_response[api_receive_pos-5] == comp[1] &&
                                   api_response[api_receive_pos-4] == comp[2] &&
                                   api_response[api_receive_pos-3] == comp[3] &&
                                   api_response[api_receive_pos-2] == comp[4] &&
                                   api_response[api_receive_pos-1] == comp[5] &&
                                   api_response[api_receive_pos] == comp[6])
        {      
          //this was the start of forecast data, skip to the next round to read it
          api_receive_pos = sizeof(api_response); //this will trigger the parse condition
          found_daily = true;
          logger.println("api_f");
        }
        ++api_receive_pos;
        
        //break if array is full or we are out of data
        //-1: never write last byte (always null terminate)
        if(api_receive_pos >= sizeof(api_response)-1 || !api_client.available())
        {
          //parse here:
          if(api_parse_result == 1)
          {
            logger.println("api_p1");
            //parse time offset:
            int r;
            jsmn_parser p;
            jsmntok_t t[56]; // this is enough for global data section
          
            jsmn_init(&p);
            r = jsmn_parse(&p, api_response, strlen(api_response), t, sizeof(t)/sizeof(t[0]));
  
            current_temp = atof(api_response+t[18].start);
            current_humidity = atof(api_response+t[24].start);
            current_clouds = atoi(api_response+t[30].start);
            api_timezoneOffset = atoi(api_response+t[8].start);
      
            unsigned long sunrise = atol(api_response+t[14].start);
            //roll over day on midnight and powerup:
            if(api_today_sunrise != sunrise)
            {
              //roll over day here!
              logger.println("nd");
              
              //sync internal clock:
              //calculate difference since last sync:
              unsigned long api_lastEpochOffset = api_epochOffset;
              api_epochOffset = atol(api_response+t[12].start) - (unsigned long)((double) api_lastCall_millis / 1000.0);
              //offset calculation is biased, because we get timestamp from last measurement TODO: what is the frequency of updates? ~15 minutes
              //we dont care though, if we are 15 minutes behind...
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
            logger.setOffset(api_epochOffset+api_timezoneOffset);
            api_today_sunrise = sunrise;
            api_today_sunset = atol(api_response+t[16].start);
            
            api_receive_pos = 0;
            api_parse_result = 2;
            if(found_daily)
            {
              //jump to 3 directly
              api_parse_result = 3;
            }
          }
          else if(api_parse_result == 2 && found_daily)
          {
            api_receive_pos = 0;
            api_parse_result = 3;
          }
          else if(api_parse_result == 3)
          {
            logger.println("api_p3");
            //second parsing round, starting after "daily" - get weather today & tomorrow
            //parse weather forecast:
            //no need to search for "daily", we are exactly behind it
            char* daily = std::strstr(api_response, "{"); //go to next array start
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
                jsmntok_t t[75];
                
                
                jsmn_init(&p);
                r = jsmn_parse(&p, daily+offset, strlen(daily+offset), t, sizeof(t)/sizeof(t[0]));
    
                forecast[i].sunrise = atoi(daily+offset+t[4].start);
                forecast[i].sunset = atoi(daily+offset+t[6].start);
                
                for(int j = 0; j < sizeof(t)/sizeof(t[0]); ++j)
                {
                  //temp
                  if(t[j].type == 3 && t[j].end - t[j].start == 3)
                  {
                    //could be max[temp], do strcmp:
                    char test[4] = {0};
                    memcpy(test, daily+offset+t[j].start, 3);
                    if(strcmp(test, "max") == 0)
                    {
                      //this is max[temp], read daily value
                      forecast[i].temp = atof(daily+offset+t[j+1].start);
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
            api_parse_result = 0;
          }
        }
      }
  
      if (c == '\n' && api_currentLineIsBlank)
      {
        api_parse_result = 1; //this line is still \n 
        
        //tstart of json data
        logger.println("api_i");
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

    // listen for incoming clients
    WiFiClient webserver_client = webserver.available();
    if (webserver_client)
    {
      logger.println("wc_c");
      // an http request ends with a blank line
      char curLine[200] = {0};
      int posLine = 0;
      while (webserver_client.connected())
      {
        if (webserver_client.available())
        {
          char c = webserver_client.read();
          if (c == '\n')
          {
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (!posLine)
            {
              printWebPage(webserver_client);
              break;
            }
            else
            {
              // you're starting a new line
              memset(curLine, 0, sizeof(curLine));
              posLine = 0;
            }
          }
          else if (c != '\r')
          {
            // you've gotten a character on the current line
            curLine[posLine] = c;
            ++posLine;
            
            //check requests from the web here:
            if (charStartsWith(curLine, "GET") && charEndsWith(curLine, "HTTP/1.1"))
            {
              //lets find out session id first:
              bool session_valid = false;
              char* session = strstr(curLine, "session_id=");
              if(session != NULL)
              {
                for(int i = 0; i < session_id_amount; ++i)
                {
                  if(web_sessions[i].expiration < millis())
                  {
                     web_sessions[i].expiration = 0;
                     memset(web_sessions[i].id, 0, session_id_length);
                  }
                  else
                  {
                    //this might be our session, check:
                    if(charStartsWith(session+11, web_sessions[i].id))
                    {
                      //valid and not expired, yeey
                      session_valid = true;
                    }
                  }
                }
              }
              if(session_valid)
              {
                //we can analyze this:
                char* lights = strstr(curLine, "lights=");
                if(lights != NULL)
                {
                  if(charStartsWith(lights+7, "on"))
                  {
                    logger.println("m_lon");
                    digitalWrite(pins[idxLights], LOW);
                  }
                  else if(charStartsWith(lights+7, "off"))
                  {
                    logger.println("m_loff");
                    digitalWrite(pins[idxLights], HIGH);
                  }
                }
                //find water for each possible plant:
                for(int i = 0; i < maxPlants; ++i)
                {
                  if(plants[i] == NULL)
                  {
                    continue;
                  }
                  char waterstr[10] = "water"; //10 is enough for "water999=" plus null termination
                  int waterlen = std::sprintf(waterstr, "water%i=", i);
                  char* water = strstr(curLine, waterstr);
                  if(water != NULL)
                  {
                    int amount = atoi(water+waterlen);
                    plants_water_manually[i] += amount;
                  }
                }
              }
            }
          }
        }
      }
      // give the web browser time to receive the data
      delay(1);
      // close the connection:
      webserver_client.stop();
      logger.println("wc_d");
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
  int analog = analogRead(pinWaterSensor);
  //only change on discrepancy between waterAvailable and actual measurement
  if(analog < water_threshold && waterAvailable == false)
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
  else if(analog >= water_threshold && waterAvailable == true)
  {
    logger.println("wt_f");
    waterAvailable = false;
    //also stop pump right away! TODO
    //digitalWrite(pins[idxPump], HIGH); 
    //were we watering?
    if(currently_watering_plant_plus1)
    {
      //also stop valve of this plant:
      digitalWrite(pins[idxPlantOffset+currently_watering_plant_plus1-1], HIGH);
      //and save already watered amount:
      
      double watered = (millis() - current_watering_start_millis) * lps_array[currently_watering_plant_plus1-1]; //in ml
      plants[currently_watering_plant_plus1-1]->addWater(watered);
      //substract value from manual watering
      if(state_watering_today == -1)
      {
        plants_water_manually[currently_watering_plant_plus1-1] -= (int) watered;
        if(plants_water_manually[currently_watering_plant_plus1-1] < 0)
        {
          plants_water_manually[currently_watering_plant_plus1-1] = 0;
        }
      }
    }
  }
}

void selectCurrentPlantToWater()
{
  double watered = 0;
  bool current_plant_watering_done = false;
  if(!currently_watering_plant_plus1)
  {
    current_plant_watering_done = true; //so we switch to first plant
  }
  else if(state_watering_today == -1)
  {
    watered = (millis() - current_watering_start_millis) * lps_array[currently_watering_plant_plus1-1]; //in ml
    //we are watering manually, compare to what we watered this session:
    current_plant_watering_done = (watered >= plants_water_manually[currently_watering_plant_plus1-1]);
  }
  else
  {
    watered = (millis() - current_watering_start_millis) * lps_array[currently_watering_plant_plus1-1]; //in ml
    double water_compare = plants[currently_watering_plant_plus1-1]->calcWaterAmount(forecast[0].temp, forecast[0].humidity, forecast[0].clouds, api_today_sunset - api_today_sunrise);
    //we are watering on schedule, compare to total plus this session
    current_plant_watering_done = (watered >= water_compare);
  }
  //if this is our first plant, OR if the current plant's watering is done, switch to the next one
  if(current_plant_watering_done)
  {
    if(currently_watering_plant_plus1)
    {
      //save water amount to plant:
      plants[currently_watering_plant_plus1-1]->addWater(watered);
      //optionally clear manual counter:
      if(state_watering_today == -1)
      {
        plants_water_manually[currently_watering_plant_plus1-1] = 0;
      }
    }
    //next plant
    for(int i = currently_watering_plant_plus1; i <= maxPlants; ++i) //allow to go to maxPlants to detect state after last plant
    {
      if(i == maxPlants)
      {
        currently_watering_plant_plus1 = i+1;
        break;
      }
      if(plants[i] == NULL || (state_watering_today == -1 && !plants_water_manually[i]))
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


void generate_session_id(char *s)
{
    static const char hex[] = "0123456789abcdef";

    for (int i = 0; i < session_id_length; ++i)
    {
        s[i] = hex[(rand() * millis()) % (sizeof(hex) - 1)];
    }
}


// this method makes a HTTP connection to the server:
void httpRequest()
{
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  api_client.stop();
  //reset parsing state machine
  api_parse_result = 0;

  api_client.setFingerprint(apiserver_fingerprint);
  api_client.setTimeout(10000);

  // if there's a successful connection:
  if (api_client.connect(apiserver, 443))
  {
    // send the HTTP PUT request:
    char adr[144] = {0};
    strcpy(adr+0, "GET /data/2.5/onecall?units=metric&exclude=minutely,hourly&lat=");
    int pos = strlen("GET /data/2.5/onecall?units=metric&exclude=minutely,hourly&lat=");
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
  }
  else
  {
    // if you couldn't make a connection:
    logger.println("api_f");
    //lower calling time //TODO
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

bool charStartsWith(const char* ch, const char* cmp)
{
  size_t ch_len = strlen(ch);
  size_t cmp_len = strlen(cmp);
  if(cmp_len > ch_len)
  {
    return false;
  }
  //cmp_len is smallest from here on
  
  for(int i = 0; i < cmp_len; ++i)
  {
    if(ch[i] != cmp[i])
    {
      return false;
    }
  }
  return true;
}

bool charEndsWith(const char* ch, const char* cmp)
{
  size_t ch_len = strlen(ch);
  size_t cmp_len = strlen(cmp);
  if(cmp_len > ch_len)
  {
    return false;
  }
  //cmp_len is smallest from here on
  
  for(int i = 0; i < cmp_len; ++i)
  {
    if(ch[ch_len-cmp_len+i] != cmp[i])
    {
      return false;
    }
  }
  return true;
}

void printWebPage(WiFiClient& webserver_client)
{
    int last_free_session = -1;
    char* current_session_id = NULL;
    //remove timed out sessions
    for(int i = 0; i < session_id_amount; ++i)
    {
      //TODO: this is dumb in case of an overflow of millis (do we run 50 days?)
        if(web_sessions[i].expiration < millis())
        {
             web_sessions[i].expiration = 0;
             memset(web_sessions[i].id, 0, session_id_length);
             last_free_session = i;
        }
    }
    //generate new session id for this session
    if(last_free_session >= 0)
    {
        web_sessions[last_free_session].expiration = millis() + 60*1000;
        generate_session_id(web_sessions[last_free_session].id);
        current_session_id = web_sessions[last_free_session].id;
    }
    
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
    webserver_client.print("<input type=\"hidden\" name=\"session_id\" value=\"");
    webserver_client.print(current_session_id);
    webserver_client.println("\" />");
    webserver_client.println("");
    webserver_client.println("<table style=\"border:0px\">");
    webserver_client.println("<tr>");
    webserver_client.print("<td>Last Weather Update</td><td>");
    {
      char date[25] = {0};
      formattedDate(date, offsetMillis(api_lastCall_millis)+api_timezoneOffset);
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
      formattedDate(date, api_today_sunrise+api_timezoneOffset+watering_sunrise_offset);
      webserver_client.print(date);
    }
    else if(api_poweron_days && state_watering_today == 2)
    {
      char date[25] = {0};
      formattedDate(date, api_today_sunset+api_timezoneOffset+watering_sunset_offset);
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
      webserver_client.print("</td><td><button type=\"submit\" name=\"water");
      webserver_client.print(i);
      webserver_client.print("\" value=\"");
      webserver_client.print(100*plants[i]->getNumPlants());
      webserver_client.print("\"");
      if(!waterAvailable)
      {
        webserver_client.print(" disabled=\"disabled\"");
      }
      webserver_client.println(">Water 100ml</button></td>");
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
    webserver_client.println();
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
