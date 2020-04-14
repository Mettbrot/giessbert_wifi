#ifndef PLANT_H
#define PLANT_H

#include "Arduino.h"

class Plant
{
    private:
    String _name;
    unsigned long _daily_amount_max_ml;
    unsigned long _daily_amount_min_ml;
    unsigned long _daily_amount_ml = 0;
    unsigned long _total_amount_ml = 0;
    
    public:
    Plant(const char* plantname, const unsigned long mlmin, const unsigned long mlmax);
    const char* getName() const;
    unsigned long getDailyMin() const;
    unsigned long getDailyMax() const;
    unsigned long getDailyWater() const;
    unsigned long getTotalWater() const;
    void resetDailyWater();
    void addWater(const unsigned long ml);
    
    
};






















#endif
