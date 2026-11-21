// -*- mode:c++ -*-

#include <chrono>

namespace utils {

    struct Timer {

        using clock = std::chrono::steady_clock;

        clock::time_point startTime;

        Timer()
            noexcept;

        clock::duration
        getElapsed()
            noexcept;

    }; // struct Timer

} // namespace utils
