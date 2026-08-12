#include "client/BF1942FrameLimiterOverride.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::array<std::uint8_t, 66> kSignature = {
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

void PutSignature(
    std::uint8_t* destination,
    std::uint32_t firstOwnerAddress,
    std::uint32_t secondOwnerAddress)
{
    std::memcpy(destination, kSignature.data(), kSignature.size());
    std::memcpy(destination + 22, &firstOwnerAddress, sizeof(firstOwnerAddress));
    std::memcpy(destination + 53, &secondOwnerAddress, sizeof(secondOwnerAddress));
}

bool TestUniqueMatch()
{
    std::array<std::uint8_t, 96> bytes = {};
    bytes.fill(0xCC);
    PutSignature(bytes.data() + 7, 0x00971EAC, 0x00971EAC);
    const auto result = bfvr::FindBF1942FrameLimiterOwnerPointer(
        bytes.data(),
        bytes.size());
    return result.status ==
            bfvr::BF1942FrameLimiterSignatureStatus::Found &&
        result.matchCount == 1 &&
        result.matchOffset == 7 &&
        result.ownerPointerAddress == 0x00971EAC;
}

bool TestDuplicateRejected()
{
    std::array<std::uint8_t, 160> bytes = {};
    bytes.fill(0xCC);
    PutSignature(bytes.data() + 3, 0x00971EAC, 0x00971EAC);
    PutSignature(bytes.data() + 90, 0x00971EAC, 0x00971EAC);
    const auto result = bfvr::FindBF1942FrameLimiterOwnerPointer(
        bytes.data(),
        bytes.size());
    return result.status ==
            bfvr::BF1942FrameLimiterSignatureStatus::Ambiguous &&
        result.matchCount == 2;
}

bool TestMismatchedOwnersRejected()
{
    std::array<std::uint8_t, 80> bytes = {};
    bytes.fill(0xCC);
    PutSignature(bytes.data() + 5, 0x00971EAC, 0x00971EB0);
    const auto result = bfvr::FindBF1942FrameLimiterOwnerPointer(
        bytes.data(),
        bytes.size());
    return result.status ==
        bfvr::BF1942FrameLimiterSignatureStatus::MismatchedOwnerPointers;
}

bool TestNearMissRejected()
{
    std::array<std::uint8_t, 80> bytes = {};
    bytes.fill(0xCC);
    PutSignature(bytes.data() + 5, 0x00971EAC, 0x00971EAC);
    bytes[5 + 31] ^= 0x01;
    const auto result = bfvr::FindBF1942FrameLimiterOwnerPointer(
        bytes.data(),
        bytes.size());
    return result.status ==
            bfvr::BF1942FrameLimiterSignatureStatus::NotFound &&
        result.matchCount == 0;
}
} // namespace

int wmain()
{
    if (!TestUniqueMatch() ||
        !TestDuplicateRejected() ||
        !TestMismatchedOwnersRejected() ||
        !TestNearMissRejected())
    {
        std::fwprintf(stderr, L"BF1942 frame-limiter signature tests failed.\n");
        return 1;
    }
    std::wprintf(L"BF1942 frame-limiter signature tests passed.\n");
    return 0;
}
