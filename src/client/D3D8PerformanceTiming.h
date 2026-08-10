#pragma once

#include "client/D3D8StereoProbeReporting.h"

#include <cstdint>

namespace bfvr::d3d8probe
{

class ScopedPerformanceAccumulator
{
public:
    explicit ScopedPerformanceAccumulator(
        std::int64_t& accumulator,
        bool enabled = true) noexcept
        : accumulator_(accumulator),
          started_(enabled ? ReadPerformanceCounter() : 0),
          active_(enabled)
    {
    }

    ~ScopedPerformanceAccumulator()
    {
        Stop();
    }

    ScopedPerformanceAccumulator(const ScopedPerformanceAccumulator&) = delete;
    ScopedPerformanceAccumulator& operator=(const ScopedPerformanceAccumulator&) = delete;

    void Stop() noexcept
    {
        if (!active_)
        {
            return;
        }
        accumulator_ += ReadPerformanceCounter() - started_;
        active_ = false;
    }

private:
    std::int64_t& accumulator_;
    std::int64_t started_ = 0;
    bool active_ = false;
};

} // namespace bfvr::d3d8probe
