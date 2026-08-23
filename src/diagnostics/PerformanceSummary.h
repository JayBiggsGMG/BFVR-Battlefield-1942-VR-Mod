#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <string_view>

namespace bfvr::diagnostics
{
inline bool ReadTargetedPerformanceSummaryEnabled() noexcept
{
    std::array<wchar_t, 16> value = {};
    const DWORD length = GetEnvironmentVariableW(
        L"BFVR_PERFORMANCE_SUMMARY",
        value.data(),
        static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size())
    {
        return false;
    }
    const std::wstring_view setting(value.data(), length);
    return setting == L"1" || _wcsicmp(value.data(), L"on") == 0 ||
        _wcsicmp(value.data(), L"true") == 0;
}

inline bool ReadAggregatePerformanceEnabled() noexcept
{
    if (ReadTargetedPerformanceSummaryEnabled())
    {
        return true;
    }
    std::array<wchar_t, 16> value = {};
    const DWORD length = GetEnvironmentVariableW(
        L"BFVR_DIAGNOSTICS",
        value.data(),
        static_cast<DWORD>(value.size()));
    return !((length == 3 && _wcsicmp(value.data(), L"off") == 0) ||
        (length == 1 && value[0] == L'0'));
}

inline bool IsPerformanceSummaryMessage(std::wstring_view message) noexcept
{
    constexpr std::wstring_view prefix = L"BFVR_PERFORMANCE_SUMMARY ";
    return message.size() >= prefix.size() &&
        message.substr(0, prefix.size()) == prefix;
}

inline std::int64_t ReadPerformanceCounter() noexcept
{
    LARGE_INTEGER counter = {};
    return QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

struct PerformanceAggregate
{
    void Add(std::int64_t elapsedTicks) noexcept
    {
        if (elapsedTicks < 0)
        {
            return;
        }
        totalTicks += elapsedTicks;
        maximumTicks = (std::max)(maximumTicks, elapsedTicks);
        ++count;
    }

    [[nodiscard]] double AverageMilliseconds(
        double millisecondsPerTick) const noexcept
    {
        return count == 0
            ? 0.0
            : static_cast<double>(totalTicks) * millisecondsPerTick /
                static_cast<double>(count);
    }

    [[nodiscard]] double MaximumMilliseconds(
        double millisecondsPerTick) const noexcept
    {
        return static_cast<double>(maximumTicks) * millisecondsPerTick;
    }

    std::int64_t totalTicks = 0;
    std::int64_t maximumTicks = 0;
    std::uint64_t count = 0;
};
} // namespace bfvr::diagnostics
