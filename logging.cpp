#include "logging.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

Logger::Logger(const Serial* _serial, const unsigned long _time_offset) : serial(_serial), time_offset(_time_offset)
{
}

Logger::~Logger()
{
}

size_t Logger::println(const char* str)
{
    size_t ret = print(str) + print("\r\n");
    print_date = true;
    return ret;
}

size_t Logger::print(const char* str)
{
    size_t ret = 0;
    if(print_date)
    {
        char date[12];
        std::sprintf(date, "%u: ", time_offset+(millis()/1000))
        ret += write(date);
        print_date = false;
    }
    ret += write(str);
    return ret;
}

size_t Logger::write(const char* str)
{
    size_t len = std::strlen(str);
    if((p_log + len + 1) > (2^16 - 2)) //make sure last symbol is alway terminalting \0
    {
        p_log = 0;
    }
    std::strcpy(log+p_log, str);

    if(serial)
    {
        return serial->write(str);
    }
    else
    {
        return len;
    }
}
