#include "Timer.h"

namespace utils {

    Timer::Timer()
        noexcept :
        startTime{clock::now()}
    {}

    Timer::clock::duration
    Timer::getElapsed()
        noexcept
    {
        auto now = clock::now();
        return now - startTime;
    }

} // namespace utils
