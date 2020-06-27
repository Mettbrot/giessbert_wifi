#include "plant.h"

#include <cmath>


Plant::Plant(const char* plantname, const unsigned long mlmin, const unsigned long mlmax, const unsigned long mlmaxpot) : _name(plantname), _daily_amount_min_ml(mlmin), _daily_amount_max_ml(mlmax), _max_per_watering(mlmaxpot)
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

unsigned long Plant::calcWaterAmount(double temp, double humidity, int clouds, unsigned long secs_p_day) const
{
  //can be called several times a day, calc watering amout today, minus water given today limited by pot size
  //main loop needs to call all plants in the evening if they had enough for today and water again if not TODO
  //main loop keeps track of timing since only one plant is watered at a time. ~ millis() - currentplant_started_millis * 1000 * mlps < calcWateramount
  unsigned long ret = dailyWaterTotal(temp, humidity, clouds, secs_p_day) - _daily_amount_ml;
  if(ret > _max_per_watering)
  {
    ret = _max_per_watering;
  }
  return ret;
}

unsigned long Plant::dailyWaterTotal(double temp, double humidity, int clouds, unsigned long secs_p_day) const
{
  //this is strictly positive
  return (unsigned long) ((double)_daily_amount_min_ml + (double)(_daily_amount_max_ml-_daily_amount_min_ml)*(0.3*(1-((double)clouds)/100.0) + 0.7*(1-humidity/100.0))*std::exp2(5*(temp/35.0 - 1.0))*((double)secs_p_day /3600.0 /13.0));
}


unsigned long Plant::getDailyWater() const
{
  return _daily_amount_ml;
}

unsigned long Plant::getTotalWater() const
{
  return _total_amount_ml;
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
