#include "timecalc.h"
/**
 *  taken from https://github.com/PaulStoffregen/Time
 */

#include <cstdio>

#include "Arduino.h"

void breakTime(unsigned long timeInput, tmElements_t &tm){
// break the given time_t into time components
// this is a more compact version of the C library localtime function

  uint8_t year;
  uint8_t month, monthLength;
  uint32_t time;
  unsigned long days;

  time = (uint32_t)timeInput;
  tm.Second = time % 60;
  time /= 60; // now it is minutes
  tm.Minute = time % 60;
  time /= 60; // now it is hours
  tm.Hour = time % 24;
  time /= 24; // now it is days
  tm.Wday = ((time + 4) % 7) + 1;  // Sunday is day 1 
  
  year = 0;  
  days = 0;
  while((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
    year++;
  }
  tm.Year = year; // year is offset from 1970 
  
  days -= LEAP_YEAR(year) ? 366 : 365;
  time  -= days; // now it is days in this year, starting at 0
  
  days=0;
  month=0;
  monthLength=0;
  for (month=0; month<12; month++) {
    if (month==1) { // february
      if (LEAP_YEAR(year)) {
        monthLength=29;
      } else {
        monthLength=28;
      }
    } else {
      monthLength = monthDays[month];
    }
    
    if (time >= monthLength) {
      time -= monthLength;
    } else {
        break;
    }
  }
  tm.Month = month + 1;  // jan is month 1  
  tm.Day = time + 1;     // day of month
}

int formattedDate(char* buf, unsigned long ts, int timezone, bool dst)
{
  //add timezone
  ts = correctTimezoneDST(ts, timezone, dst);
  tmElements_t tm;
  breakTime(ts, tm);

  return std::sprintf(buf, "%02u.%02u.%04u %02u:%02u:%02u", tm.Day, tm.Month, 1970+tm.Year, tm.Hour, tm.Minute, tm.Second);
}

unsigned long correctTimezoneDST(unsigned long ts, int timezone, bool dst)
{
  //add timezone
  ts = ts + 3600*timezone;
  tmElements_t tm;
  breakTime(ts, tm);
  
  if(dst)
  {
    //totally wrong an rough calculation but it fits most days and is easy
    if(tm.Month > 3 && tm.Month < 10 || tm.Month == 3 && tm.Day > 25 || tm.Month == 10 && tm.Day < 25)
    {
      ts += 3600;
    }
  }
  return ts;
}
