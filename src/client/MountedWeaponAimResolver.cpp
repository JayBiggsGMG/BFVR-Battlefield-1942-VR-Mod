#include "client/MountedWeaponAimResolver.h"

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

constexpr std::ptrdiff_t kFireArmsTransformationRva = 0x00139580;
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr DWORD kPlayerControlObjectInterfaceId = 0x0000C4C5;
constexpr std::size_t kQueryInterfaceVtableOffset = 0x08;
constexpr std::size_t kGetTransformationVtableOffset = 0x3C;
constexpr std::size_t kGetWeaponsVtableOffset = 0x58;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr std::size_t kBFPlayerCurrentControlObjectOffset = 0x64;
constexpr std::size_t kBFPlayerDefaultControlObjectOffset = 0x98;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xA9;
constexpr std::uint32_t kMaximumPlausibleWeaponCount = 64;
constexpr std::array<BYTE, 16> kFireArmsTransformationPrefix = {
    0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x4C, 0x8A, 0x88,
    0xD0, 0x03, 0x00, 0x00, 0x84, 0xC9, 0x74, 0x37};

using QueryInterfaceFn = void*(__thiscall*)(void*, DWORD);
using GetWeaponsFn = void*(__thiscall*)(void*);
using GetTransformationFn =
    const bfvr::stereo::Matrix4*(__thiscall*)(void*);
using GetFireArmsTransformationFn =
    const bfvr::stereo::Matrix4*(__thiscall*)(void*);

struct NativeWeaponVector
{
    void* allocator = nullptr;
    void** begin = nullptr;
    void** end = nullptr;
    void** capacityEnd = nullptr;
};

static_assert(sizeof(NativeWeaponVector) == 16);

std::byte* g_gameImage = nullptr;
GetFireArmsTransformationFn g_getFireArmsTransformation = nullptr;
void (*g_appendLog)(const wchar_t* message) = nullptr;

void WriteLog(const wchar_t* format, ...) noexcept
{
    if (g_appendLog == nullptr)
    {
        return;
    }
    std::array<wchar_t, 480> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    g_appendLog(message.data());
}

bool IsFinite(const bfvr::stereo::Matrix4& matrix) noexcept
{
    for (const auto& row : matrix.values)
    {
        for (const float value : row)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace bfvr
{

bool ReadLocalPlayerControlContext(
    LocalPlayerControlContext& context) noexcept
{
    context = {};
    if (g_gameImage == nullptr)
    {
        return false;
    }
    __try
    {
        void* const manager = *reinterpret_cast<void* const*>(
            g_gameImage + kPlayerManagerGlobalRva);
        const auto* const player = manager == nullptr
            ? nullptr
            : *reinterpret_cast<const std::byte* const*>(
                static_cast<const std::byte*>(manager) +
                kPlayerManagerLocalPlayerOffset);
        if (player == nullptr)
        {
            return false;
        }
        context.alive =
            std::to_integer<BYTE>(player[kBFPlayerIsAliveOffset]) != 0;
        context.currentControlObject = *reinterpret_cast<void* const*>(
            player + kBFPlayerCurrentControlObjectOffset);
        context.defaultControlObject = *reinterpret_cast<void* const*>(
            player + kBFPlayerDefaultControlObjectOffset);
        return context.alive && context.currentControlObject != nullptr &&
            context.defaultControlObject != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        context = {};
        return false;
    }
}

bool InitializeMountedWeaponAimResolver(
    void* gameImage,
    void (*appendLog)(const wchar_t* message)) noexcept
{
    ShutdownMountedWeaponAimResolver();
    g_appendLog = appendLog;
    if (gameImage == nullptr)
    {
        return false;
    }

    auto* const target = static_cast<std::byte*>(gameImage) +
        kFireArmsTransformationRva;
    bool prefixMatches = false;
    __try
    {
        prefixMatches = std::memcmp(
            target,
            kFireArmsTransformationPrefix.data(),
            kFireArmsTransformationPrefix.size()) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        prefixMatches = false;
    }
    if (!prefixMatches)
    {
        WriteLog(
            L"Mounted weapon aim resolver rejected %p: the profiled WinPC FireArms::getFireArmsTransformation prefix differs.",
            target);
        return false;
    }

    g_gameImage = static_cast<std::byte*>(gameImage);
    g_getFireArmsTransformation =
        reinterpret_cast<GetFireArmsTransformationFn>(target);
    WriteLog(
        L"Mounted weapon resolver armed at 0x00539580: occupied controls query PlayerControlObject interface 0xC4C5/getWeapons +0x58, sample weapon zero through FireArms::getFireArmsTransformation, and expose the control object's stable world transform through its verified vtable +0x3C getter.");
    return true;
}

void ShutdownMountedWeaponAimResolver() noexcept
{
    g_getFireArmsTransformation = nullptr;
    g_gameImage = nullptr;
    g_appendLog = nullptr;
}

bool ReadMountedWeaponFirePose(
    void* currentControlObject,
    stereo::Matrix4& firePose) noexcept
{
    firePose = {};
    if (g_gameImage == nullptr || g_getFireArmsTransformation == nullptr ||
        currentControlObject == nullptr)
    {
        return false;
    }

    __try
    {
        void** const objectVtable =
            *reinterpret_cast<void***>(currentControlObject);
        if (objectVtable == nullptr)
        {
            return false;
        }
        const auto queryInterface = reinterpret_cast<QueryInterfaceFn>(
            objectVtable[kQueryInterfaceVtableOffset / sizeof(void*)]);
        if (queryInterface == nullptr)
        {
            return false;
        }
        void* const playerControlObject = queryInterface(
            currentControlObject,
            kPlayerControlObjectInterfaceId);
        if (playerControlObject == nullptr)
        {
            return false;
        }

        void** const playerControlVtable =
            *reinterpret_cast<void***>(playerControlObject);
        if (playerControlVtable == nullptr)
        {
            return false;
        }
        const auto getWeapons = reinterpret_cast<GetWeaponsFn>(
            playerControlVtable[kGetWeaponsVtableOffset / sizeof(void*)]);
        if (getWeapons == nullptr)
        {
            return false;
        }
        const auto* const weapons = static_cast<const NativeWeaponVector*>(
            getWeapons(playerControlObject));
        if (weapons == nullptr || weapons->begin == nullptr ||
            weapons->end == nullptr)
        {
            return false;
        }

        const std::uintptr_t begin =
            reinterpret_cast<std::uintptr_t>(weapons->begin);
        const std::uintptr_t end =
            reinterpret_cast<std::uintptr_t>(weapons->end);
        if (end <= begin || (end - begin) % sizeof(void*) != 0)
        {
            return false;
        }
        const std::uintptr_t weaponCount =
            (end - begin) / sizeof(void*);
        if (weaponCount > kMaximumPlausibleWeaponCount)
        {
            return false;
        }

        void* const weapon = weapons->begin[0];
        if (weapon == nullptr)
        {
            return false;
        }
        const stereo::Matrix4* const resolved =
            g_getFireArmsTransformation(weapon);
        if (resolved == nullptr)
        {
            return false;
        }
        firePose = *resolved;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        firePose = {};
        return false;
    }
}

bool ReadOccupiedMountedWeaponStationPose(
    MountedWeaponStationPose& stationPose) noexcept
{
    stationPose = {};
    if (g_gameImage == nullptr || g_getFireArmsTransformation == nullptr)
    {
        return false;
    }

    __try
    {
        void* const manager = *reinterpret_cast<void* const*>(
            g_gameImage + kPlayerManagerGlobalRva);
        auto* const player = manager == nullptr
            ? nullptr
            : *reinterpret_cast<std::byte* const*>(
                static_cast<const std::byte*>(manager) +
                kPlayerManagerLocalPlayerOffset);
        if (player == nullptr ||
            std::to_integer<BYTE>(player[kBFPlayerIsAliveOffset]) == 0)
        {
            return false;
        }

        void* const currentControlObject =
            *reinterpret_cast<void* const*>(
                player + kBFPlayerCurrentControlObjectOffset);
        void* const defaultControlObject =
            *reinterpret_cast<void* const*>(
                player + kBFPlayerDefaultControlObjectOffset);
        if (currentControlObject == nullptr ||
            defaultControlObject == nullptr ||
            currentControlObject == defaultControlObject)
        {
            return false;
        }

        stereo::Matrix4 firePose = {};
        if (!ReadMountedWeaponFirePose(currentControlObject, firePose))
        {
            return false;
        }

        void* const vtable =
            *reinterpret_cast<void* const*>(currentControlObject);
        void* const target = vtable == nullptr
            ? nullptr
            : *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(vtable) +
                kGetTransformationVtableOffset);
        const auto getTransformation =
            reinterpret_cast<GetTransformationFn>(target);
        const stereo::Matrix4* const world = getTransformation == nullptr
            ? nullptr
            : getTransformation(currentControlObject);
        if (world == nullptr)
        {
            return false;
        }

        stereo::Matrix4 copy = {};
        std::memcpy(&copy, world, sizeof(copy));
        if (!IsFinite(copy))
        {
            return false;
        }
        stationPose.controlObject = currentControlObject;
        stationPose.stationWorld = copy;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        stationPose = {};
        return false;
    }
}

} // namespace bfvr
