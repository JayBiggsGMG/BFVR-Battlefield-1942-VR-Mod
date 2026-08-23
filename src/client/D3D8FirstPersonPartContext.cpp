#include "client/D3D8FirstPersonPartContext.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace
{

constexpr std::ptrdiff_t kAnimatedMeshDrawRva = 0x001AEEC0;
constexpr std::size_t kAnimatedMeshTemplateOffset = 0x24;
constexpr std::size_t kAnimatedMeshTemplateNameOffset = 0x34;
constexpr BYTE kAnimatedMeshDrawPrefix[] = {
    0xA1, 0x44, 0x9A, 0x95, 0x00, 0x83, 0xEC, 0x18,
    0x85, 0xC0, 0x55, 0x8B, 0xE9, 0x7C, 0x36, 0x8B,
    0x45, 0x24};

using AnimatedMeshDrawFn = void(__thiscall*)(void*, void*, float);

struct CurrentAnimatedMeshContext
{
    void* animatedMesh = nullptr;
    bfvr::stereo::D3D8FirstPersonPartKind partKind =
        bfvr::stereo::D3D8FirstPersonPartKind::UnknownOrCombined;
    bool classificationResolved = false;
};

thread_local CurrentAnimatedMeshContext g_currentAnimatedMesh = {};
thread_local bfvr::stereo::D3D8FirstPersonPartClassificationCache
    g_partClassificationCache = {};

bool HasExpectedPrefix(const void* target) noexcept
{
    if (target == nullptr)
    {
        return false;
    }
    __try
    {
        return std::memcmp(
                   target,
                   kAnimatedMeshDrawPrefix,
                   sizeof(kAnimatedMeshDrawPrefix)) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <std::size_t N>
bool CopyPrintableAscii(
    const char* source,
    std::array<char, N>& destination,
    std::size_t& length) noexcept
{
    static_assert(N >= 2);
    destination.fill('\0');
    length = 0;
    if (source == nullptr)
    {
        return false;
    }
    __try
    {
        for (std::size_t index = 0; index + 1 < destination.size(); ++index)
        {
            const unsigned char value =
                static_cast<unsigned char>(source[index]);
            if (value == 0)
            {
                length = index;
                return index != 0;
            }
            if (value < 0x20U || value > 0x7EU)
            {
                destination.fill('\0');
                return false;
            }
            destination[index] = static_cast<char>(value);
        }
        length = destination.size() - 1;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        destination.fill('\0');
        length = 0;
        return false;
    }
}

class FirstPersonPartContext
{
public:
    bool Start(
        void* gameImage,
        void (*appendLog)(const wchar_t* message)) noexcept
    {
        if (started_ || gameImage == nullptr)
        {
            return started_;
        }
        appendLog_ = appendLog;
        target_ = static_cast<std::byte*>(gameImage) +
            kAnimatedMeshDrawRva;
        if (!HasExpectedPrefix(target_))
        {
            WriteLog(
                L"First-person part context unavailable: the AnimatedMesh draw prefix does not match this executable. Hands Only will conservatively hide combined and unrecognized first-person meshes.");
            target_ = nullptr;
            return false;
        }
        const MH_STATUS create = MH_CreateHook(
            target_,
            reinterpret_cast<LPVOID>(&FirstPersonPartContext::DrawHook),
            reinterpret_cast<LPVOID*>(&original_));
        if (create != MH_OK || original_ == nullptr)
        {
            WriteLog(
                L"First-person part context unavailable: the forwarding hook could not be created. Hands Only will conservatively hide combined and unrecognized first-person meshes.");
            target_ = nullptr;
            original_ = nullptr;
            return false;
        }
        hookCreated_ = true;
        active_ = this;
        const MH_STATUS enable = MH_EnableHook(target_);
        if (enable != MH_OK)
        {
            active_ = nullptr;
            MH_RemoveHook(target_);
            hookCreated_ = false;
            target_ = nullptr;
            original_ = nullptr;
            WriteLog(
                L"First-person part context unavailable: the forwarding hook could not be enabled. Hands Only will conservatively hide combined and unrecognized first-person meshes.");
            return false;
        }
        hookEnabled_ = true;
        started_ = true;
        WriteLog(
            L"First-person part context enabled. Hands Only retains explicitly named left/right hand meshes; combined and unrecognized first-person meshes use the conservative hidden fallback.");
        return true;
    }

    void Stop() noexcept
    {
        if (hookEnabled_ && target_ != nullptr)
        {
            MH_DisableHook(target_);
            hookEnabled_ = false;
        }
        while (InterlockedCompareExchange(&activeCallbacks_, 0, 0) != 0)
        {
            Sleep(0);
        }
        active_ = nullptr;
        if (hookCreated_ && target_ != nullptr)
        {
            MH_RemoveHook(target_);
        }
        hookCreated_ = false;
        started_ = false;
        original_ = nullptr;
        target_ = nullptr;
        appendLog_ = nullptr;
        g_currentAnimatedMesh = {};
        g_partClassificationCache.Clear();
    }

    bfvr::stereo::D3D8FirstPersonPartKind ReadCurrentPartKind() noexcept
    {
        if (!g_currentAnimatedMesh.classificationResolved)
        {
            g_currentAnimatedMesh.partKind =
                ClassifyCached(g_currentAnimatedMesh.animatedMesh);
            g_currentAnimatedMesh.classificationResolved = true;
        }
        return g_currentAnimatedMesh.partKind;
    }

private:
    static void __fastcall DrawHook(
        void* animatedMesh,
        void*,
        void* renderContext,
        float lodDistance)
    {
        FirstPersonPartContext* const context = active_;
        if (context == nullptr || context->original_ == nullptr)
        {
            return;
        }
        InterlockedIncrement(&context->activeCallbacks_);
        const CurrentAnimatedMeshContext previous = g_currentAnimatedMesh;
        g_currentAnimatedMesh = {};
        g_currentAnimatedMesh.animatedMesh = animatedMesh;
        __try
        {
            context->original_(animatedMesh, renderContext, lodDistance);
        }
        __finally
        {
            g_currentAnimatedMesh = previous;
            InterlockedDecrement(&context->activeCallbacks_);
        }
    }

    static bool ReadTemplateIdentity(
        void* animatedMesh,
        const void*& meshTemplate,
        bfvr::stereo::D3D8FirstPersonPartTemplateCacheKey& key) noexcept
    {
        meshTemplate = nullptr;
        key = {};
        if (animatedMesh == nullptr)
        {
            return false;
        }
        __try
        {
            const auto* const mesh =
                static_cast<const std::byte*>(animatedMesh);
            meshTemplate = *reinterpret_cast<void* const*>(
                mesh + kAnimatedMeshTemplateOffset);
            if (meshTemplate == nullptr)
            {
                return false;
            }
            const auto* const nameStorage =
                static_cast<const std::byte*>(meshTemplate) +
                kAnimatedMeshTemplateNameOffset;
            key.templateAddress = reinterpret_cast<std::uintptr_t>(
                meshTemplate);
            std::memcpy(
                key.nameStorageIdentity.data(),
                nameStorage,
                sizeof(key.nameStorageIdentity));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            meshTemplate = nullptr;
            key = {};
            return false;
        }
    }

    static bool ClassifyTemplate(
        const void* meshTemplate,
        bfvr::stereo::D3D8FirstPersonPartKind& partKind) noexcept
    {
        using bfvr::stereo::ClassifyD3D8FirstPersonPartTemplateName;
        using bfvr::stereo::D3D8FirstPersonPartKind;
        partKind = D3D8FirstPersonPartKind::UnknownOrCombined;
        if (meshTemplate == nullptr)
        {
            return false;
        }
        __try
        {
            const auto* const nameStorage =
                static_cast<const std::byte*>(meshTemplate) +
                kAnimatedMeshTemplateNameOffset;
            std::array<char, 128> name = {};
            std::size_t length = 0;
            bool validName = false;
            if (CopyPrintableAscii(
                    reinterpret_cast<const char*>(nameStorage),
                    name,
                    length))
            {
                validName = true;
                partKind = ClassifyD3D8FirstPersonPartTemplateName(
                    std::string_view(name.data(), length));
                if (partKind == D3D8FirstPersonPartKind::SeparateHand)
                {
                    return true;
                }
            }
            const char* const externalName =
                *reinterpret_cast<const char* const*>(nameStorage);
            if (CopyPrintableAscii(externalName, name, length))
            {
                partKind = ClassifyD3D8FirstPersonPartTemplateName(
                    std::string_view(name.data(), length));
                return true;
            }
            return validName;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bfvr::stereo::D3D8FirstPersonPartKind ClassifyCached(
        void* animatedMesh) noexcept
    {
        using bfvr::stereo::D3D8FirstPersonPartKind;
        const void* meshTemplate = nullptr;
        bfvr::stereo::D3D8FirstPersonPartTemplateCacheKey key = {};
        if (!ReadTemplateIdentity(animatedMesh, meshTemplate, key))
        {
            return D3D8FirstPersonPartKind::UnknownOrCombined;
        }
        D3D8FirstPersonPartKind partKind =
            D3D8FirstPersonPartKind::UnknownOrCombined;
        if (g_partClassificationCache.Find(key, partKind))
        {
            return partKind;
        }
        if (ClassifyTemplate(meshTemplate, partKind))
        {
            g_partClassificationCache.Store(key, partKind);
        }
        return partKind;
    }

    void WriteLog(const wchar_t* message) const noexcept
    {
        if (appendLog_ != nullptr && message != nullptr)
        {
            appendLog_(message);
        }
    }

    static FirstPersonPartContext* active_;
    void* target_ = nullptr;
    AnimatedMeshDrawFn original_ = nullptr;
    void (*appendLog_)(const wchar_t* message) = nullptr;
    volatile LONG activeCallbacks_ = 0;
    bool hookCreated_ = false;
    bool hookEnabled_ = false;
    bool started_ = false;
};

FirstPersonPartContext* FirstPersonPartContext::active_ = nullptr;
FirstPersonPartContext g_partContext;

} // namespace

namespace bfvr
{

bool StartD3D8FirstPersonPartContext(
    void* gameImage,
    void (*appendLog)(const wchar_t* message)) noexcept
{
    return g_partContext.Start(gameImage, appendLog);
}

void StopD3D8FirstPersonPartContext() noexcept
{
    g_partContext.Stop();
}

stereo::D3D8FirstPersonPartKind ReadD3D8FirstPersonPartKind() noexcept
{
    return g_partContext.ReadCurrentPartKind();
}

} // namespace bfvr
