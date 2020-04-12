#ifndef LOGGING_H
#define LOGGING_H

#include <cstdint>
#include <cstddef>

#include "Arduino.h"

class Logging
{
private:
    unsigned long _time_offset;
    char _log[2^16] = {0};
    std::uint16_t _p_log = 0;
    bool _print_date = true;

public:
    Logging(const unsigned long time_offset);
    ~Logging();
    size_t println(const char* str);
    size_t print(const char* str);
    size_t write(const char* str);
};

#endif
