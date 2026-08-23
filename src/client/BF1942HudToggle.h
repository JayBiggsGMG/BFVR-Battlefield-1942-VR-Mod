#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace bfvr
{

enum class BF1942HudSetterSignatureStatus
{
    Found,
    NotFound,
    Ambiguous,
    InvalidArgument
};

struct BF1942HudSetterSignatureResult
{
    BF1942HudSetterSignatureStatus status =
        BF1942HudSetterSignatureStatus::InvalidArgument;
    std::size_t matchOffset = 0;
    std::size_t matchCount = 0;
    std::uint32_t fieldOffset = 0;
};

// Matches a complete profiled legacy byte setter: load the byte argument,
// store it at owner+0x8BC, and return while popping that argument. Later
// command-method evidence proves this is not the active useHud field boundary;
// the match is retained only as an exact-build corroboration gate.
[[nodiscard]] BF1942HudSetterSignatureResult FindBF1942HudSetter(
    const std::uint8_t* bytes,
    std::size_t byteCount) noexcept;

// Maps an executable as a non-executable read-only image and applies the same
// section-bounded signature scan. This supports runtimes that patch the live
// setter after BF1942.exe has been loaded without trusting unverified bytes.
[[nodiscard]] BF1942HudSetterSignatureResult
FindBF1942HudSetterInExecutableFile(const wchar_t* path) noexcept;

using BF1942HudToggleLogCallback = void (*)(const wchar_t* message);

// Consumes presenter-owned monotonic edges inside the existing x86 render
// request path. It resolves the registered game.useHUD ConsoleObject and calls
// its normal execution method with one Boolean argument, without opening the
// console, editing a .con file, or changing game launch behavior. Any image,
// registry, object, or executable-target mismatch fails closed.
class BF1942HudToggle
{
public:
    bool Initialize(
        HMODULE executableModule,
        BF1942HudToggleLogCallback logCallback) noexcept;
    void Consume(LONG requestedSequence) noexcept;
    void LogSummary() const noexcept;

private:
    void WriteLog(const wchar_t* format, ...) const noexcept;

    std::uintptr_t ownerPointerAddress_ = 0;
    std::uintptr_t executableBase_ = 0;
    std::uintptr_t setterAddress_ = 0;
    std::uint32_t fieldOffset_ = 0;
    bool hudEnabled_ = true;
    LONG consumedSequence_ = 0;
    LONG appliedCount_ = 0;
    LONG rejectedCount_ = 0;
    BF1942HudToggleLogCallback logCallback_ = nullptr;
};

} // namespace bfvr
