#include "plant.h"


Plant::Plant(const char* plantname, const unsigned long mlmin, const unsigned long mlmax) : _name(plantname), _daily_amount_min_ml(mlmin), _daily_amount_max_ml(mlmax)
{
    
}


const char* Plant::getName() const
{
    return _name.c_str();
}

unsigned long Plant::getDailyMin() const
{
  return _daily_amount_min_ml;
}

unsigned long Plant::getDailyMax() const
{
  return _daily_amount_max_ml;
}


unsigned long Plant::getDailyWater() const
{
  return _daily_amount_ml;
}

unsigned long Plant::getTotalWater() const
{
  return _daily_amount_ml;
}


void Plant::resetDailyWater()
{
  _daily_amount_ml = 0;
}


void Plant::addWater(const unsigned long ml)
{
  _daily_amount_ml += ml;
  _total_amount_ml += ml;
}
