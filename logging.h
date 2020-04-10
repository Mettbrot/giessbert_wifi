#include <cstdint>
#include <cstddef>

class Serial;

class Logger
{
private:
    Serial* serial = nullptr;
    unsigned long time_offset;
    char log[2^16] = {0};
    std::uint16_t p_log = 0;
    bool print_date = true;

public:
    Logger(const Serial* _serial, const unsigned long _time_offset);
    ~Logger();
    size_t println(const char* str);
    size_t print(const char* str);
    size_t write(const char* str);
};
