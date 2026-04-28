#pragma once
#include <chrono>

namespace util {

class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double>;

    Timer() : m_start(Clock::now()), m_last(Clock::now()) {}

    // Returns seconds since last call to Delta()
    double Delta() {
        auto now = Clock::now();
        double dt = Duration(now - m_last).count();
        m_last = now;
        // Clamp to avoid spiral of death on breakpoint/hitch
        return (dt > 0.1) ? 0.1 : dt;
    }

    // Returns seconds since construction
    double Elapsed() const {
        return Duration(Clock::now() - m_start).count();
    }

    void Reset() {
        m_start = Clock::now();
        m_last = Clock::now();
    }

private:
    Clock::time_point m_start;
    Clock::time_point m_last;
};

} // namespace util
