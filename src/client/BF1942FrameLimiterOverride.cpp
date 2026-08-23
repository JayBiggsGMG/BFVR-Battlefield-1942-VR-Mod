#include "client/BF1942FrameLimiterOverride.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace bfvr
{
namespace
{
constexpr std::size_t kFirstOwnerPointerOffset = 22;
constexpr std::size_t kSecondOwnerPointerOffset = 53;
constexpr std::size_t kOwnerPointerSize = sizeof(std::uint32_t);
constexpr std::size_t kFrameLimiterValueOffset = 0x17C;
constexpr float kUnlimitedFrameRate = -1.0f;

// Exact 0x0043C380 lockFps getter/setter body. The owner-pointer operands at
// offsets 22..25 and 53..56 are ignored by SignatureMatchesAt.
constexpr std::array<std::uint8_t, 66> kFrameLimiterSetterSignature = {
    0x8B, 0x41, 0x1C, 0x85, 0xC0, 0x75, 0x1C,
    0xC6, 0x81, 0xA0, 0x00, 0x00, 0x00, 0x01,
    0x8D, 0x81, 0xA4, 0x00, 0x00, 0x00,
    0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x81, 0x7C, 0x01, 0x00, 0x00,
    0xD9, 0x18, 0xC3,
    0x83, 0xF8, 0x01, 0x75, 0x17,
    0x8B, 0x51, 0x20, 0xD9, 0x02,
    0xC6, 0x81, 0xA0, 0x00, 0x00, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0x00,
    0xD9, 0x98, 0x7C, 0x01, 0x00, 0x00,
    0x33, 0xC0, 0xC3};

volatile LONG g_frameLimiterOverrideRequested = 0;
volatile LONG g_frameLimiterOverrideApplied = 0;
volatile LONG g_cachedFrameLimiterOwnerPointerAddress = 0;

bool IsWildcardOffset(std::size_t offset) noexcept
{
    return (offset >= kFirstOwnerPointerOffset &&
            offset < kFirstOwnerPointerOffset + kOwnerPointerSize) ||
        (offset >= kSecondOwnerPointerOffset &&
         offset < kSecondOwnerPointerOffset + kOwnerPointerSize);
}

bool SignatureMatchesAt(const std::uint8_t* candidate) noexcept
{
    for (std::size_t index = 0;
         index < kFrameLimiterSetterSignature.size();
         ++index)
    {
        if (!IsWildcardOffset(index) &&
            candidate[index] != kFrameLimiterSetterSignature[index])
        {
            return false;
        }
    }
    return true;
}

std::uint32_t ReadEmbeddedAddress(
    const std::uint8_t* candidate,
    std::size_t offset) noexcept
{
    std::uint32_t address = 0;
    std::memcpy(&address, candidate + offset, sizeof(address));
    return address;
}

bool IsReadableProtection(DWORD protection) noexcept
{
    const DWORD baseProtection = protection & 0xFF;
    return baseProtection == PAGE_READONLY ||
        baseProtection == PAGE_READWRITE ||
        baseProtection == PAGE_WRITECOPY ||
        baseProtection == PAGE_EXECUTE_READ ||
        baseProtection == PAGE_EXECUTE_READWRITE ||
        baseProtection == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritableProtection(DWORD protection) noexcept
{
    const DWORD baseProtection = protection & 0xFF;
    return baseProtection == PAGE_READWRITE ||
        baseProtection == PAGE_WRITECOPY ||
        baseProtection == PAGE_EXECUTE_READWRITE ||
        baseProtection == PAGE_EXECUTE_WRITECOPY;
}

bool IsRangeAccessible(
    std::uintptr_t address,
    std::size_t size,
    bool requireWrite) noexcept
{
    if (address == 0 || size == 0 ||
        address > std::numeric_limits<std::uintptr_t>::max() - size)
    {
        return false;
    }

    MEMORY_BASIC_INFORMATION information = {};
    if (VirtualQuery(
            reinterpret_cast<const void*>(address),
            &information,
            sizeof(information)) != sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
    {
        return false;
    }

    const std::uintptr_t regionStart =
        reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    if (regionStart >
        std::numeric_limits<std::uintptr_t>::max() - information.RegionSize)
    {
        return false;
    }
    const std::uintptr_t regionEnd = regionStart + information.RegionSize;
    const std::uintptr_t requestedEnd = address + size;
    if (address < regionStart || requestedEnd > regionEnd)
    {
        return false;
    }

    return requireWrite
        ? IsWritableProtection(information.Protect)
        : IsReadableProtection(information.Protect);
}

BF1942FrameLimiterSignatureResult FindInExecutableImage(
    HMODULE executableModule) noexcept
{
    BF1942FrameLimiterSignatureResult combined = {};
    combined.status = BF1942FrameLimiterSignatureStatus::NotFound;
    if (executableModule == nullptr)
    {
        combined.status = BF1942FrameLimiterSignatureStatus::InvalidArgument;
        return combined;
    }

    const auto* const imageBase =
        reinterpret_cast<const std::uint8_t*>(executableModule);
    if (!IsRangeAccessible(
            reinterpret_cast<std::uintptr_t>(imageBase),
            sizeof(IMAGE_DOS_HEADER),
            false))
    {
        combined.status = BF1942FrameLimiterSignatureStatus::InvalidArgument;
        return combined;
    }

    const auto* const dosHeader =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE ||
        dosHeader->e_lfanew <= 0)
    {
        combined.status = BF1942FrameLimiterSignatureStatus::InvalidArgument;
        return combined;
    }

    const std::uintptr_t ntAddress =
        reinterpret_cast<std::uintptr_t>(imageBase) +
        static_cast<std::uintptr_t>(dosHeader->e_lfanew);
    if (!IsRangeAccessible(ntAddress, sizeof(IMAGE_NT_HEADERS32), false))
    {
        combined.status = BF1942FrameLimiterSignatureStatus::InvalidArgument;
        return combined;
    }

    const auto* const ntHeaders =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(ntAddress);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE ||
        ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        ntHeaders->OptionalHeader.SizeOfImage == 0)
    {
        combined.status = BF1942FrameLimiterSignatureStatus::InvalidArgument;
        return combined;
    }

    const auto* const firstSection = IMAGE_FIRST_SECTION(ntHeaders);
    const std::size_t sectionTableSize =
        static_cast<std::size_t>(ntHeaders->FileHeader.NumberOfSections) *
        sizeof(IMAGE_SECTION_HEADER);
    if (!IsRangeAccessible(
            reinterpret_cast<std::uintptr_t>(firstSection),
            sectionTableSize,
            false))
    {
        combined.status = BF1942FrameLimiterSignatureStatus::InvalidArgument;
        return combined;
    }

    for (WORD index = 0;
         index < ntHeaders->FileHeader.NumberOfSections;
         ++index)
    {
        const IMAGE_SECTION_HEADER& section = firstSection[index];
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
            section.VirtualAddress >= ntHeaders->OptionalHeader.SizeOfImage)
        {
            continue;
        }

        const std::size_t available =
            ntHeaders->OptionalHeader.SizeOfImage - section.VirtualAddress;
        const std::size_t requested = std::max<std::size_t>(
            section.Misc.VirtualSize,
            section.SizeOfRawData);
        const std::size_t sectionSize = std::min(requested, available);
        if (sectionSize < kFrameLimiterSetterSignature.size())
        {
            continue;
        }

        const std::uintptr_t sectionStart =
            reinterpret_cast<std::uintptr_t>(imageBase) +
            section.VirtualAddress;
        const std::uintptr_t sectionEnd = sectionStart + sectionSize;
        std::uintptr_t cursor = sectionStart;
        while (cursor < sectionEnd)
        {
            MEMORY_BASIC_INFORMATION information = {};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(cursor),
                    &information,
                    sizeof(information)) != sizeof(information))
            {
                break;
            }

            const std::uintptr_t regionStart =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            if (regionStart >
                std::numeric_limits<std::uintptr_t>::max() -
                    information.RegionSize)
            {
                break;
            }
            const std::uintptr_t regionEnd =
                regionStart + information.RegionSize;
            const std::uintptr_t readableStart =
                std::max(cursor, regionStart);
            const std::uintptr_t readableEnd =
                std::min(sectionEnd, regionEnd);
            if (readableEnd <= cursor)
            {
                break;
            }

            const std::size_t readableSize =
                readableEnd - readableStart;
            if (information.State == MEM_COMMIT &&
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
                IsReadableProtection(information.Protect) &&
                readableSize >= kFrameLimiterSetterSignature.size())
            {
                const BF1942FrameLimiterSignatureResult regionResult =
                    FindBF1942FrameLimiterOwnerPointer(
                        reinterpret_cast<const std::uint8_t*>(readableStart),
                        readableSize);
                if (regionResult.matchCount != 0)
                {
                    if (regionResult.status !=
                            BF1942FrameLimiterSignatureStatus::Found ||
                        combined.matchCount != 0 ||
                        regionResult.matchCount != 1)
                    {
                        combined.status = regionResult.status ==
                                BF1942FrameLimiterSignatureStatus::
                                    MismatchedOwnerPointers
                            ? BF1942FrameLimiterSignatureStatus::
                                MismatchedOwnerPointers
                            : BF1942FrameLimiterSignatureStatus::Ambiguous;
                        combined.matchCount += regionResult.matchCount;
                        combined.ownerPointerAddress = 0;
                        return combined;
                    }

                    combined = regionResult;
                    combined.matchOffset +=
                        readableStart -
                        reinterpret_cast<std::uintptr_t>(imageBase);
                }
            }

            cursor = readableEnd;
        }
    }

    return combined;
}

BF1942FrameLimiterOverrideStatus MapSignatureFailure(
    BF1942FrameLimiterSignatureStatus status) noexcept
{
    switch (status)
    {
    case BF1942FrameLimiterSignatureStatus::NotFound:
        return BF1942FrameLimiterOverrideStatus::SignatureNotFound;
    case BF1942FrameLimiterSignatureStatus::Ambiguous:
        return BF1942FrameLimiterOverrideStatus::SignatureAmbiguous;
    case BF1942FrameLimiterSignatureStatus::MismatchedOwnerPointers:
        return BF1942FrameLimiterOverrideStatus::SignatureOwnerMismatch;
    case BF1942FrameLimiterSignatureStatus::InvalidArgument:
        return BF1942FrameLimiterOverrideStatus::InvalidExecutableImage;
    case BF1942FrameLimiterSignatureStatus::Found:
        break;
    }
    return BF1942FrameLimiterOverrideStatus::InvalidExecutableImage;
}
} // namespace

BF1942FrameLimiterSignatureResult FindBF1942FrameLimiterOwnerPointer(
    const std::uint8_t* bytes,
    std::size_t byteCount) noexcept
{
    BF1942FrameLimiterSignatureResult result = {};
    if (bytes == nullptr)
    {
        result.status = BF1942FrameLimiterSignatureStatus::InvalidArgument;
        return result;
    }
    if (byteCount < kFrameLimiterSetterSignature.size())
    {
        result.status = BF1942FrameLimiterSignatureStatus::NotFound;
        return result;
    }

    std::uint32_t firstAddress = 0;
    std::uint32_t secondAddress = 0;
    for (std::size_t offset = 0;
         offset <= byteCount - kFrameLimiterSetterSignature.size();
         ++offset)
    {
        const std::uint8_t* const candidate = bytes + offset;
        if (!SignatureMatchesAt(candidate))
        {
            continue;
        }

        ++result.matchCount;
        if (result.matchCount == 1)
        {
            result.matchOffset = offset;
            firstAddress = ReadEmbeddedAddress(
                candidate,
                kFirstOwnerPointerOffset);
            secondAddress = ReadEmbeddedAddress(
                candidate,
                kSecondOwnerPointerOffset);
        }
    }

    if (result.matchCount == 0)
    {
        result.status = BF1942FrameLimiterSignatureStatus::NotFound;
    }
    else if (result.matchCount != 1)
    {
        result.status = BF1942FrameLimiterSignatureStatus::Ambiguous;
    }
    else if (firstAddress == 0 || firstAddress != secondAddress)
    {
        result.status =
            BF1942FrameLimiterSignatureStatus::MismatchedOwnerPointers;
    }
    else
    {
        result.status = BF1942FrameLimiterSignatureStatus::Found;
        result.ownerPointerAddress = firstAddress;
    }
    return result;
}

BF1942FrameLimiterSignatureResult
FindBF1942FrameLimiterOwnerPointerInExecutableImage(
    HMODULE executableModule) noexcept
{
    return FindInExecutableImage(executableModule);
}

void RequestBF1942FrameLimiterOverride() noexcept
{
    InterlockedExchange(&g_frameLimiterOverrideRequested, 1);
}

BF1942FrameLimiterOverrideResult ApplyRequestedBF1942FrameLimiterOverride(
    HMODULE executableModule,
    bool verifyEvenIfAlreadyApplied) noexcept
{
    BF1942FrameLimiterOverrideResult result = {};
    if (InterlockedCompareExchange(
            &g_frameLimiterOverrideRequested,
            0,
            0) == 0)
    {
        result.status = BF1942FrameLimiterOverrideStatus::NotRequested;
        return result;
    }
    if (!verifyEvenIfAlreadyApplied &&
        InterlockedCompareExchange(&g_frameLimiterOverrideApplied, 0, 0) != 0)
    {
        result.status = BF1942FrameLimiterOverrideStatus::AlreadyApplied;
        return result;
    }

    BF1942FrameLimiterSignatureResult signature = {};
    const LONG cachedOwnerPointerAddress = InterlockedCompareExchange(
        &g_cachedFrameLimiterOwnerPointerAddress,
        0,
        0);
    if (cachedOwnerPointerAddress != 0)
    {
        signature.status = BF1942FrameLimiterSignatureStatus::Found;
        signature.ownerPointerAddress =
            static_cast<std::uint32_t>(cachedOwnerPointerAddress);
        signature.matchCount = 1;
    }
    else
    {
        signature = FindInExecutableImage(executableModule);
        if (signature.status == BF1942FrameLimiterSignatureStatus::Found)
        {
            InterlockedCompareExchange(
                &g_cachedFrameLimiterOwnerPointerAddress,
                static_cast<LONG>(signature.ownerPointerAddress),
                0);
        }
    }
    if (signature.status != BF1942FrameLimiterSignatureStatus::Found)
    {
        result.status = MapSignatureFailure(signature.status);
        return result;
    }
    result.ownerPointerAddress = signature.ownerPointerAddress;

    if (!IsRangeAccessible(
            signature.ownerPointerAddress,
            sizeof(std::uint32_t),
            false))
    {
        result.status =
            BF1942FrameLimiterOverrideStatus::OwnerPointerUnreadable;
        return result;
    }

    std::uint32_t ownerAddress = 0;
    std::memcpy(
        &ownerAddress,
        reinterpret_cast<const void*>(signature.ownerPointerAddress),
        sizeof(ownerAddress));
    if (ownerAddress == 0)
    {
        result.status = BF1942FrameLimiterOverrideStatus::OwnerUnavailable;
        return result;
    }
    if (ownerAddress >
        std::numeric_limits<std::uint32_t>::max() -
            kFrameLimiterValueOffset)
    {
        result.status = BF1942FrameLimiterOverrideStatus::ValueFieldUnwritable;
        return result;
    }

    result.valueAddress = ownerAddress + kFrameLimiterValueOffset;
    if (!IsRangeAccessible(result.valueAddress, sizeof(float), true))
    {
        result.status = BF1942FrameLimiterOverrideStatus::ValueFieldUnwritable;
        return result;
    }

    std::memcpy(
        &result.previousValue,
        reinterpret_cast<const void*>(result.valueAddress),
        sizeof(result.previousValue));
    if (result.previousValue == kUnlimitedFrameRate)
    {
        InterlockedExchange(&g_frameLimiterOverrideApplied, 1);
        result.status = BF1942FrameLimiterOverrideStatus::AlreadyUnlimited;
        return result;
    }
    if (!std::isfinite(result.previousValue) ||
        result.previousValue < 0.0f ||
        result.previousValue > 100000.0f)
    {
        result.status =
            BF1942FrameLimiterOverrideStatus::UnexpectedCurrentValue;
        return result;
    }

    const LONG unlimitedBits = std::bit_cast<LONG>(kUnlimitedFrameRate);
    InterlockedExchange(
        reinterpret_cast<volatile LONG*>(result.valueAddress),
        unlimitedBits);

    float verifiedValue = 0.0f;
    std::memcpy(
        &verifiedValue,
        reinterpret_cast<const void*>(result.valueAddress),
        sizeof(verifiedValue));
    if (verifiedValue != kUnlimitedFrameRate)
    {
        result.status =
            BF1942FrameLimiterOverrideStatus::WriteVerificationFailed;
        return result;
    }

    InterlockedExchange(&g_frameLimiterOverrideApplied, 1);
    result.status = BF1942FrameLimiterOverrideStatus::Applied;
    return result;
}

const wchar_t* DescribeBF1942FrameLimiterOverrideStatus(
    BF1942FrameLimiterOverrideStatus status) noexcept
{
    switch (status)
    {
    case BF1942FrameLimiterOverrideStatus::NotRequested:
        return L"not requested";
    case BF1942FrameLimiterOverrideStatus::Applied:
        return L"applied";
    case BF1942FrameLimiterOverrideStatus::AlreadyUnlimited:
        return L"already unlimited";
    case BF1942FrameLimiterOverrideStatus::AlreadyApplied:
        return L"already applied";
    case BF1942FrameLimiterOverrideStatus::SignatureNotFound:
        return L"compatible lockFps signature not found";
    case BF1942FrameLimiterOverrideStatus::SignatureAmbiguous:
        return L"lockFps signature was ambiguous";
    case BF1942FrameLimiterOverrideStatus::SignatureOwnerMismatch:
        return L"lockFps signature owner operands disagreed";
    case BF1942FrameLimiterOverrideStatus::InvalidExecutableImage:
        return L"invalid executable image";
    case BF1942FrameLimiterOverrideStatus::OwnerPointerUnreadable:
        return L"renderer owner pointer was unreadable";
    case BF1942FrameLimiterOverrideStatus::OwnerUnavailable:
        return L"renderer owner was not ready";
    case BF1942FrameLimiterOverrideStatus::ValueFieldUnwritable:
        return L"renderer lockFps value was not writable";
    case BF1942FrameLimiterOverrideStatus::UnexpectedCurrentValue:
        return L"renderer lockFps value was unexpected";
    case BF1942FrameLimiterOverrideStatus::WriteVerificationFailed:
        return L"renderer lockFps write verification failed";
    }
    return L"unknown";
}

} // namespace bfvr
