#include "client/BF1942HudToggle.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::array<std::uint8_t, 13> kSetter = {
    0x8A, 0x44, 0x24, 0x04,
    0x88, 0x81, 0xBC, 0x08, 0x00, 0x00,
    0xC2, 0x04, 0x00};

void PutSetter(std::uint8_t* destination, std::uint32_t offset)
{
    std::memcpy(destination, kSetter.data(), kSetter.size());
    std::memcpy(destination + 6, &offset, sizeof(offset));
}

bool TestUniqueMatch()
{
    std::array<std::uint8_t, 48> bytes = {};
    bytes.fill(0xCC);
    PutSetter(bytes.data() + 9, 0x8BC);
    const auto result = bfvr::FindBF1942HudSetter(
        bytes.data(), bytes.size());
    return result.status == bfvr::BF1942HudSetterSignatureStatus::Found &&
        result.matchCount == 1 && result.matchOffset == 9 &&
        result.fieldOffset == 0x8BC;
}

bool TestShiftedFieldRejected()
{
    std::array<std::uint8_t, 32> bytes = {};
    bytes.fill(0x90);
    PutSetter(bytes.data() + 3, 0x944);
    const auto result = bfvr::FindBF1942HudSetter(
        bytes.data(), bytes.size());
    return result.status ==
            bfvr::BF1942HudSetterSignatureStatus::NotFound &&
        result.matchCount == 0;
}

bool TestDuplicateRejected()
{
    std::array<std::uint8_t, 64> bytes = {};
    bytes.fill(0xCC);
    PutSetter(bytes.data() + 2, 0x8BC);
    PutSetter(bytes.data() + 35, 0x8BC);
    const auto result = bfvr::FindBF1942HudSetter(
        bytes.data(), bytes.size());
    return result.status ==
            bfvr::BF1942HudSetterSignatureStatus::Ambiguous &&
        result.matchCount == 2 && result.fieldOffset == 0;
}

bool TestNearMissRejected()
{
    std::array<std::uint8_t, 32> bytes = {};
    bytes.fill(0xCC);
    PutSetter(bytes.data() + 4, 0x8BC);
    bytes[4 + 10] ^= 1;
    const auto result = bfvr::FindBF1942HudSetter(
        bytes.data(), bytes.size());
    return result.status ==
            bfvr::BF1942HudSetterSignatureStatus::NotFound &&
        result.matchCount == 0;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (!TestUniqueMatch() || !TestShiftedFieldRejected() ||
        !TestDuplicateRejected() || !TestNearMissRejected())
    {
        std::fwprintf(stderr, L"BF1942 native HUD-toggle tests failed.\n");
        return 1;
    }
    if (argc == 2)
    {
        const auto mapped =
            bfvr::FindBF1942HudSetterInExecutableFile(argv[1]);
        if (mapped.status !=
                bfvr::BF1942HudSetterSignatureStatus::Found ||
            mapped.matchCount != 1 || mapped.fieldOffset != 0x8BC)
        {
            std::fwprintf(
                stderr,
                L"BF1942 read-only mapped-image HUD-setter validation failed: status=%d matches=%zu field=0x%X.\n",
                static_cast<int>(mapped.status),
                mapped.matchCount,
                mapped.fieldOffset);
            return 1;
        }
    }
    std::wprintf(L"BF1942 native HUD-toggle tests passed.\n");
    return 0;
}
