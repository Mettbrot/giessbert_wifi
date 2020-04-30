#ifndef TIMECALC_H
#define TIMECALC_H

#include <stdint.h>

/**
 *  taken from https://github.com/PaulStoffregen/Time
 */

typedef struct  { 
  uint8_t Second; 
  uint8_t Minute; 
  uint8_t Hour; 
  uint8_t Wday;   // day of week, sunday is day 1
  uint8_t Day;
  uint8_t Month; 
  uint8_t Year;   // offset from 1970; 
} tmElements_t;

/* Constants */
#define SECS_PER_MIN  ((unsigned long)(60UL))
#define MIN_PER_HOUR  ((unsigned long)(60UL))
#define SECS_PER_HOUR ((unsigned long)(SECS_PER_MIN * MIN_PER_HOUR))
#define SECS_PER_DAY  ((unsigned long)(SECS_PER_HOUR * 24UL))
#define DAYS_PER_WEEK ((unsigned long)(7UL))
#define SECS_PER_WEEK ((unsigned long)(SECS_PER_DAY * DAYS_PER_WEEK))


#define numberOfSeconds(ts) ((ts) % SECS_PER_MIN)  
#define numberOfMinutes(ts) (((ts) / SECS_PER_MIN) % SECS_PER_MIN) 
#define numberOfHours(ts) (((ts) % SECS_PER_DAY) / SECS_PER_HOUR)
#define dayOfWeek(ts) ((((ts) / SECS_PER_DAY + 4)  % DAYS_PER_WEEK)+1) // 1 = Sunday
#define elapsedDays(ts) ((ts) / SECS_PER_DAY)  // this is number of days since Jan 1 1970
#define elapsedSecsToday(ts) ((ts) % SECS_PER_DAY)   // the number of seconds since last midnight 

#define previousMidnight(ts) (((ts) / SECS_PER_DAY) * SECS_PER_DAY)  // time at the start of the given day
#define nextMidnight(ts) (previousMidnight(ts)  + SECS_PER_DAY)   // time at the end of the given day 
#define elapsedSecsThisWeek(ts) (elapsedSecsToday(ts) +  ((dayOfWeek(ts)-1) * SECS_PER_DAY))   // week starts on day 1

// leap year calculator expects year argument as years offset from 1970
#define LEAP_YEAR(Y)     ( ((1970+(Y))>0) && !((1970+(Y))%4) && ( ((1970+(Y))%100) || !((1970+(Y))%400) ) )

static  const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0
 

void breakTime(unsigned long timeInput, tmElements_t &tm);

int formattedDate(char* buf, unsigned long ts, int timezone, bool dst);


#endif //TIMECALC_H
