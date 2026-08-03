#pragma once

#include <cstdint>

namespace bfvr::audio
{
constexpr std::uint32_t kOpenALHrtfRouterDiagnosticsVersion = 1;

struct OpenALHrtfRouterDiagnostics
{
    std::uint32_t version = kOpenALHrtfRouterDiagnosticsVersion;
    std::uint32_t size = sizeof(OpenALHrtfRouterDiagnostics);
    std::uint32_t createContextCalls = 0;
    std::uint32_t forcedHrtfCalls = 0;
    std::uint32_t successfulContextCalls = 0;
    std::int32_t lastHrtfStatus = 0;
    std::uint32_t malformedAttributeLists = 0;
};

using GetOpenALHrtfRouterDiagnosticsFunction = int (__cdecl*)(
    OpenALHrtfRouterDiagnostics* diagnostics);
} // namespace bfvr::audio
