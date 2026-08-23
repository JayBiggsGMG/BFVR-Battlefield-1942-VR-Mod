#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace bfvr
{

enum class BF1942FrameLimiterSignatureStatus
{
    Found,
    NotFound,
    Ambiguous,
    MismatchedOwnerPointers,
    InvalidArgument
};

struct BF1942FrameLimiterSignatureResult
{
    BF1942FrameLimiterSignatureStatus status =
        BF1942FrameLimiterSignatureStatus::InvalidArgument;
    std::uintptr_t ownerPointerAddress = 0;
    std::size_t matchOffset = 0;
    std::size_t matchCount = 0;
};

// Pure byte-pattern search used by both the live process path and unit tests.
// The two absolute-address operands are deliberately wildcarded so relocated
// executables and compatible BF1942 builds can be accepted without hashes.
[[nodiscard]] BF1942FrameLimiterSignatureResult
FindBF1942FrameLimiterOwnerPointer(
    const std::uint8_t* bytes,
    std::size_t byteCount) noexcept;

// Resolves that same owner-pointer operand from committed executable sections
// in the live BF1942 image. Other guarded process-local settings may reuse it.
[[nodiscard]] BF1942FrameLimiterSignatureResult
FindBF1942FrameLimiterOwnerPointerInExecutableImage(
    HMODULE executableModule) noexcept;

enum class BF1942FrameLimiterOverrideStatus
{
    NotRequested,
    Applied,
    AlreadyUnlimited,
    AlreadyApplied,
    SignatureNotFound,
    SignatureAmbiguous,
    SignatureOwnerMismatch,
    InvalidExecutableImage,
    OwnerPointerUnreadable,
    OwnerUnavailable,
    ValueFieldUnwritable,
    UnexpectedCurrentValue,
    WriteVerificationFailed
};

struct BF1942FrameLimiterOverrideResult
{
    BF1942FrameLimiterOverrideStatus status =
        BF1942FrameLimiterOverrideStatus::NotRequested;
    std::uintptr_t ownerPointerAddress = 0;
    std::uintptr_t valueAddress = 0;
    float previousValue = 0.0f;
};

void RequestBF1942FrameLimiterOverride() noexcept;

// Applies renderer.lockFPS -1 directly to BF1942's process-local renderer
// state. It never edits VideoDefault.con, so ordinary non-BFVR launches retain
// the user's normal setting.
[[nodiscard]] BF1942FrameLimiterOverrideResult
ApplyRequestedBF1942FrameLimiterOverride(
    HMODULE executableModule,
    bool verifyEvenIfAlreadyApplied = false) noexcept;

[[nodiscard]] const wchar_t* DescribeBF1942FrameLimiterOverrideStatus(
    BF1942FrameLimiterOverrideStatus status) noexcept;

} // namespace bfvr
