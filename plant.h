#ifndef PLANT_H
#define PLANT_H

#include "Arduino.h"

class Plant
{
    private:
    String _name;
    unsigned long _daily_amount_max_ml;
    unsigned long _daily_amount_min_ml;
    unsigned long _max_per_watering;
    unsigned long _daily_amount_ml = 0;
    unsigned long _total_amount_ml = 0;
    
    public:
    Plant(const char* plantname, const unsigned long mlmin, const unsigned long mlmax, const unsigned long mlmaxpot);
    const char* getName() const;
    unsigned long getDailyMin() const;
    unsigned long getDailyMax() const;
    unsigned long calcWaterAmout(double temp, double humidity, int clouds, unsigned long secs_p_day) const;
    unsigned long getDailyWater() const;
    unsigned long getTotalWater() const;
    void resetDailyWater();
    void addWater(const unsigned long ml);
    
    
};

#endif //PLANT_H
