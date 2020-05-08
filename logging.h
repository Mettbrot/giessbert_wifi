#ifndef LOGGING_H
#define LOGGING_H

#include <cstdint>
#include <cstddef>

#include "Arduino.h"

class Logging
{
private:
    Stream* _ser;
    unsigned long _time_offset;
    char _log[1<<10] = {0};
    std::uint16_t _p_log = 0;
    bool _print_date = true;

public:
    Logging(Stream* ser, const unsigned long time_offset);
    ~Logging();
    void setOffset(const unsigned long offset);
    const char* getLog() const;
    size_t println(const char* str);
    size_t println(const double doubl);
    size_t print(const char* str);
    size_t print(const double doubl);
    size_t write(const char c);
    size_t write(const char* str);
};

#endif
