#include "Arduino.h"
#include "logging.h"

#include <cstddef>
#include <cstdio>
#include <cstring>


Logging::Logging(Stream* ser, const unsigned long time_offset) : _ser(ser), _time_offset(time_offset)
{
}

Logging::~Logging()
{
}


void Logging::setOffset(unsigned long offset)
{
    _time_offset = offset;
}

const char* Logging::getLog() const
{
    return _log;
}

size_t Logging::println(const char* str)
{
    size_t ret = print(str) + print("\r\n");
    _print_date = true;
    return ret;
}

size_t Logging::println(const double doubl)
{
    char number[100] = {0};
    std::sprintf(number, "%d", doubl);
    return println(number);
}

size_t Logging::print(const char* str)
{
    size_t ret = 0;
    if(_print_date)
    {
        char date[12] = {0};
        std::sprintf(date, "%u: ", _time_offset+(millis()/1000));
        ret += write(date);
        _print_date = false;
    }
    ret += write(str);
    return ret;
}


size_t Logging::print(const double doubl)
{
    char number[100] = {0};
    std::sprintf(number, "%d", doubl);
    return print(number);
}

size_t Logging::write(const char* str)
{
    size_t len = std::strlen(str);
    if((_p_log + len + 1) > ((1<<10) - 2)) //make sure last symbol is always terminating \0
    {
        _p_log = 0;
    }
    std::strcpy(_log+_p_log, str);
    _p_log += len;

    if(_ser)
    {
        return _ser->write(str);
    }
    else
    {
        return len;
    }
}


size_t Logging::write(const char c)
{
    char ch[2] = {0};
    ch[0] = c;
    return write(&ch[0]);
}
