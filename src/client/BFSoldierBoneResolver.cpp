#include "client/BFSoldierBoneResolver.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

constexpr std::ptrdiff_t kBoneManagerGetBoneNameRva = 0x002167A0;
constexpr std::ptrdiff_t kBoneManagerGetInstanceRva = 0x00216880;
constexpr std::size_t kSkeletonBoneRecordsOffset = 0x0;
constexpr std::size_t kSkeletonBoneCountOffset = 0x4;
constexpr std::size_t kBoneRecordStride = 0xE8;
constexpr std::size_t kBoneNameIndexOffset = 0x0;
constexpr std::int32_t kMaximumBones = 256;
constexpr std::size_t kMaximumBoneNameLength = 63;
constexpr char kGameStringCStrExport[] =
    "?c_str@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
    "QBEPBDXZ";

constexpr BYTE kBoneManagerGetBoneNamePrefix[] = {
    0x57, 0x8B, 0x79, 0x04, 0x85, 0xFF, 0x75, 0x04,
    0x33, 0xD2, 0xEB, 0x1A, 0x56, 0x8B, 0x71, 0x08};
constexpr BYTE kBoneManagerGetInstancePrefix[] = {
    0x8A, 0x0D, 0x9C, 0x96, 0x9C, 0x00, 0xB8, 0x01,
    0x00, 0x00, 0x00, 0x84, 0xC8, 0x75, 0x3C};

using GetBoneNameFn = const void*(__thiscall*)(void*, short);
using GetBoneManagerInstanceFn = void*(__cdecl*)();
using GameStringCStrFn = const char*(__thiscall*)(const void*);

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    const std::size_t length) noexcept
{
    if (target == nullptr || expected == nullptr || length == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, length) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

char LowerAscii(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

bool EqualsAsciiIgnoreCase(
    const char* candidate,
    const char* expected) noexcept
{
    if (candidate == nullptr || expected == nullptr)
    {
        return false;
    }
    __try
    {
        for (std::size_t index = 0; index <= kMaximumBoneNameLength; ++index)
        {
            const char candidateCharacter = candidate[index];
            const char expectedCharacter = expected[index];
            if (LowerAscii(candidateCharacter) != LowerAscii(expectedCharacter))
            {
                return false;
            }
            if (candidateCharacter == '\0')
            {
                return true;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return false;
}

} // namespace

namespace bfvr
{

bool BFSoldierBoneResolver::Initialize(void* gameImage) noexcept
{
    Reset();
    if (gameImage == nullptr)
    {
        return false;
    }

    auto* const image = static_cast<std::byte*>(gameImage);
    void* const getBoneName = image + kBoneManagerGetBoneNameRva;
    void* const getInstance = image + kBoneManagerGetInstanceRva;
    if (!HasExpectedPrefix(
            getBoneName,
            kBoneManagerGetBoneNamePrefix,
            sizeof(kBoneManagerGetBoneNamePrefix)) ||
        !HasExpectedPrefix(
            getInstance,
            kBoneManagerGetInstancePrefix,
            sizeof(kBoneManagerGetInstancePrefix)))
    {
        return false;
    }

    const HMODULE runtime = GetModuleHandleW(L"BFCPRT.dll");
    void* const cStr = runtime == nullptr
        ? nullptr
        : reinterpret_cast<void*>(
              GetProcAddress(runtime, kGameStringCStrExport));
    if (cStr == nullptr)
    {
        return false;
    }

    getBoneNameTarget_ = getBoneName;
    getBoneManagerInstanceTarget_ = getInstance;
    gameStringCStrTarget_ = cStr;
    return true;
}

void BFSoldierBoneResolver::Reset() noexcept
{
    getBoneNameTarget_ = nullptr;
    getBoneManagerInstanceTarget_ = nullptr;
    gameStringCStrTarget_ = nullptr;
}

bool BFSoldierBoneResolver::IsReady() const noexcept
{
    return getBoneNameTarget_ != nullptr &&
        getBoneManagerInstanceTarget_ != nullptr &&
        gameStringCStrTarget_ != nullptr;
}

std::optional<std::int32_t> BFSoldierBoneResolver::ResolveBoneIndex(
    void* skeleton,
    const char* canonicalName) const noexcept
{
    if (!IsReady() || skeleton == nullptr || canonicalName == nullptr)
    {
        return std::nullopt;
    }

    __try
    {
        const auto* const skeletonBytes =
            static_cast<const std::byte*>(skeleton);
        const std::int32_t boneCount =
            *reinterpret_cast<const std::int32_t*>(
                skeletonBytes + kSkeletonBoneCountOffset);
        const std::byte* const boneRecords =
            *reinterpret_cast<std::byte* const*>(
                skeletonBytes + kSkeletonBoneRecordsOffset);
        if (boneCount <= 0 || boneCount > kMaximumBones ||
            boneRecords == nullptr)
        {
            return std::nullopt;
        }

        const auto getInstance =
            reinterpret_cast<GetBoneManagerInstanceFn>(
                getBoneManagerInstanceTarget_);
        const auto getBoneName =
            reinterpret_cast<GetBoneNameFn>(getBoneNameTarget_);
        const auto cStr =
            reinterpret_cast<GameStringCStrFn>(gameStringCStrTarget_);
        void* const manager = getInstance();
        if (manager == nullptr)
        {
            return std::nullopt;
        }

        for (std::int32_t index = 0; index < boneCount; ++index)
        {
            const std::byte* const record =
                boneRecords +
                static_cast<std::size_t>(index) * kBoneRecordStride;
            const short nameIndex = *reinterpret_cast<const short*>(
                record + kBoneNameIndexOffset);
            const void* const gameString = getBoneName(manager, nameIndex);
            const char* const boneName =
                gameString == nullptr ? nullptr : cStr(gameString);
            if (EqualsAsciiIgnoreCase(boneName, canonicalName))
            {
                return index;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace bfvr
