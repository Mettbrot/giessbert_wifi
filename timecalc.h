#ifndef TIMECALC_H
#define TIMECALC_H


/* Constants */
#define SECS_PER_MIN  ((unsigned long)(60UL))
#define SECS_PER_HOUR ((unsigned long)(3600UL))
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


#endif //TIMECALC_H