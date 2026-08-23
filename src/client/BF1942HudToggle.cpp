#include "client/BF1942HudToggle.h"

#include "client/BF1942FrameLimiterOverride.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <limits>

namespace bfvr
{
namespace
{
constexpr std::array<std::uint8_t, 13> kSetterSignature = {
    0x8A, 0x44, 0x24, 0x04,
    0x88, 0x81, 0xBC, 0x08, 0x00, 0x00,
    0xC2, 0x04, 0x00};
constexpr std::size_t kFieldOffsetPosition = 6;
constexpr std::size_t kFieldOffsetSize = sizeof(std::uint32_t);
constexpr std::uintptr_t kPreferredImageBase = 0x00400000U;
constexpr std::uintptr_t kConsoleObjectsRva = 0x009AB610U -
    kPreferredImageBase;
constexpr std::uintptr_t kUseHudVtableRva = 0x008C6EA8U -
    kPreferredImageBase;
constexpr std::size_t kConsoleObjectsListHeadOffset = 28;
constexpr std::size_t kConsoleObjectsListSizeOffset = 32;
constexpr std::size_t kConsoleNodeNextOffset = 0;
constexpr std::size_t kConsoleNodeValueOffset = 8;
constexpr std::size_t kConsoleObjectVtableOffset = 0;
constexpr std::size_t kConsoleObjectNameOffset = 8;
constexpr std::size_t kConsoleFunctionNameOffset = 12;
constexpr std::size_t kConsoleObjectArgCountOffset = 28;
constexpr std::size_t kConsoleObjectFirstArgOffset = 32;
constexpr std::size_t kConsoleObjectHasReturnValueOffset = 160;
constexpr std::size_t kConsoleObjectReturnValueOffset = 164;
constexpr std::size_t kConsoleIsObjectActiveSlot = 17;
constexpr std::size_t kConsoleExecuteObjectMethodSlot = 18;
constexpr std::size_t kMaximumConsoleObjects = 4096;

bool IsRangeAccessible(
    std::uintptr_t address,
    std::size_t size,
    bool requireWrite) noexcept;

bool SignatureMatchesAt(const std::uint8_t* candidate) noexcept
{
    for (std::size_t index = 0; index < kSetterSignature.size(); ++index)
    {
        if (candidate[index] != kSetterSignature[index])
        {
            return false;
        }
    }
    return true;
}

bool IsReadableProtection(DWORD protection) noexcept
{
    const DWORD base = protection & 0xFFU;
    return base == PAGE_READONLY || base == PAGE_READWRITE ||
        base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ ||
        base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

bool IsWritableProtection(DWORD protection) noexcept
{
    const DWORD base = protection & 0xFFU;
    return base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
        base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

bool IsExecutableProtection(DWORD protection) noexcept
{
    const DWORD base = protection & 0xFFU;
    return base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
        base == PAGE_EXECUTE_READWRITE ||
        base == PAGE_EXECUTE_WRITECOPY;
}

bool IsExecutableAddress(std::uintptr_t address) noexcept
{
    MEMORY_BASIC_INFORMATION information = {};
    return address != 0 &&
        VirtualQuery(
            reinterpret_cast<const void*>(address),
            &information,
            sizeof(information)) == sizeof(information) &&
        information.State == MEM_COMMIT &&
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
        IsExecutableProtection(information.Protect);
}

bool ReadAddress(
    std::uintptr_t address,
    std::uintptr_t& value) noexcept
{
    value = 0;
    if (!IsRangeAccessible(address, sizeof(std::uint32_t), false))
    {
        return false;
    }
    std::uint32_t value32 = 0;
    std::memcpy(
        &value32,
        reinterpret_cast<const void*>(address),
        sizeof(value32));
    value = value32;
    return true;
}

char FoldAscii(char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool MatchesAsciiName(
    std::uintptr_t address,
    const char* expected) noexcept
{
    if (address == 0 || expected == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0; index < 64; ++index)
    {
        if (!IsRangeAccessible(address + index, sizeof(char), false))
        {
            return false;
        }
        char actual = 0;
        std::memcpy(
            &actual,
            reinterpret_cast<const void*>(address + index),
            sizeof(actual));
        const char wanted = expected[index];
        if (FoldAscii(actual) != FoldAscii(wanted))
        {
            return false;
        }
        if (wanted == '\0')
        {
            return true;
        }
    }
    return false;
}

struct HudConsoleObjectResult
{
    std::uintptr_t object = 0;
    std::uintptr_t isObjectActive = 0;
    std::uintptr_t executeObjectMethod = 0;
    std::size_t visited = 0;
};

HudConsoleObjectResult FindHudConsoleObject(
    std::uintptr_t executableBase) noexcept
{
    HudConsoleObjectResult result = {};
    if (executableBase == 0 ||
        executableBase > std::numeric_limits<std::uintptr_t>::max() -
            kConsoleObjectsRva)
    {
        return result;
    }
    const std::uintptr_t registry = executableBase + kConsoleObjectsRva;
    std::uintptr_t head = 0;
    std::uint32_t objectCount = 0;
    if (!ReadAddress(
            registry + kConsoleObjectsListHeadOffset,
            head) ||
        !IsRangeAccessible(
            registry + kConsoleObjectsListSizeOffset,
            sizeof(objectCount),
            false))
    {
        return result;
    }
    std::memcpy(
        &objectCount,
        reinterpret_cast<const void*>(
            registry + kConsoleObjectsListSizeOffset),
        sizeof(objectCount));
    if (head == 0 || objectCount == 0 ||
        objectCount > kMaximumConsoleObjects)
    {
        return result;
    }
    std::uintptr_t node = 0;
    if (!ReadAddress(head + kConsoleNodeNextOffset, node))
    {
        return result;
    }
    const std::uintptr_t expectedVtable =
        executableBase + kUseHudVtableRva;
    while (node != 0 && node != head &&
        result.visited < objectCount &&
        result.visited < kMaximumConsoleObjects)
    {
        ++result.visited;
        std::uintptr_t next = 0;
        std::uintptr_t object = 0;
        if (!ReadAddress(node + kConsoleNodeNextOffset, next) ||
            !ReadAddress(node + kConsoleNodeValueOffset, object))
        {
            return {};
        }
        std::uintptr_t vtable = 0;
        std::uintptr_t objectName = 0;
        std::uintptr_t functionName = 0;
        if (ReadAddress(object + kConsoleObjectVtableOffset, vtable) &&
            vtable == expectedVtable &&
            ReadAddress(object + kConsoleObjectNameOffset, objectName) &&
            ReadAddress(
                object + kConsoleFunctionNameOffset,
                functionName) &&
            MatchesAsciiName(objectName, "game") &&
            MatchesAsciiName(functionName, "useHud"))
        {
            std::uintptr_t isObjectActive = 0;
            std::uintptr_t executeObjectMethod = 0;
            if (!ReadAddress(
                    vtable + kConsoleIsObjectActiveSlot *
                        sizeof(std::uint32_t),
                    isObjectActive) ||
                !ReadAddress(
                    vtable + kConsoleExecuteObjectMethodSlot *
                        sizeof(std::uint32_t),
                    executeObjectMethod) ||
                !IsExecutableAddress(isObjectActive) ||
                !IsExecutableAddress(executeObjectMethod))
            {
                return {};
            }
            result.object = object;
            result.isObjectActive = isObjectActive;
            result.executeObjectMethod = executeObjectMethod;
            return result;
        }
        node = next;
    }
    return result;
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
    const std::uintptr_t start =
        reinterpret_cast<std::uintptr_t>(information.BaseAddress);
    if (start > std::numeric_limits<std::uintptr_t>::max() -
            information.RegionSize)
    {
        return false;
    }
    const std::uintptr_t end = start + information.RegionSize;
    return address >= start && address + size <= end &&
        (requireWrite
            ? IsWritableProtection(information.Protect)
            : IsReadableProtection(information.Protect));
}

BF1942HudSetterSignatureResult FindInExecutableImage(
    HMODULE executableModule) noexcept
{
    BF1942HudSetterSignatureResult combined = {};
    combined.status = BF1942HudSetterSignatureStatus::NotFound;
    if (executableModule == nullptr)
    {
        combined.status = BF1942HudSetterSignatureStatus::InvalidArgument;
        return combined;
    }
    const auto* const image =
        reinterpret_cast<const std::uint8_t*>(executableModule);
    if (!IsRangeAccessible(
            reinterpret_cast<std::uintptr_t>(image),
            sizeof(IMAGE_DOS_HEADER),
            false))
    {
        combined.status = BF1942HudSetterSignatureStatus::InvalidArgument;
        return combined;
    }
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
    {
        combined.status = BF1942HudSetterSignatureStatus::InvalidArgument;
        return combined;
    }
    const std::uintptr_t ntAddress =
        reinterpret_cast<std::uintptr_t>(image) +
        static_cast<std::uintptr_t>(dos->e_lfanew);
    if (!IsRangeAccessible(ntAddress, sizeof(IMAGE_NT_HEADERS32), false))
    {
        combined.status = BF1942HudSetterSignatureStatus::InvalidArgument;
        return combined;
    }
    const auto* const nt =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(ntAddress);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.SizeOfImage == 0)
    {
        combined.status = BF1942HudSetterSignatureStatus::InvalidArgument;
        return combined;
    }
    const IMAGE_SECTION_HEADER* const sections = IMAGE_FIRST_SECTION(nt);
    const std::size_t sectionTableSize =
        static_cast<std::size_t>(nt->FileHeader.NumberOfSections) *
        sizeof(IMAGE_SECTION_HEADER);
    if (!IsRangeAccessible(
            reinterpret_cast<std::uintptr_t>(sections),
            sectionTableSize,
            false))
    {
        combined.status = BF1942HudSetterSignatureStatus::InvalidArgument;
        return combined;
    }
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index)
    {
        const IMAGE_SECTION_HEADER& section = sections[index];
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
            section.VirtualAddress >= nt->OptionalHeader.SizeOfImage)
        {
            continue;
        }
        const std::size_t available =
            nt->OptionalHeader.SizeOfImage - section.VirtualAddress;
        const std::size_t requested = (std::max<std::size_t>)(
            section.Misc.VirtualSize,
            section.SizeOfRawData);
        const std::size_t sectionSize = (std::min)(requested, available);
        const std::uintptr_t sectionAddress =
            reinterpret_cast<std::uintptr_t>(image) +
            section.VirtualAddress;
        if (sectionSize < kSetterSignature.size() ||
            !IsRangeAccessible(sectionAddress, sectionSize, false))
        {
            continue;
        }
        const BF1942HudSetterSignatureResult result = FindBF1942HudSetter(
            reinterpret_cast<const std::uint8_t*>(sectionAddress),
            sectionSize);
        if (result.matchCount == 0)
        {
            continue;
        }
        if (result.status != BF1942HudSetterSignatureStatus::Found ||
            combined.matchCount != 0)
        {
            combined.status = BF1942HudSetterSignatureStatus::Ambiguous;
            combined.matchCount += result.matchCount;
            combined.fieldOffset = 0;
            return combined;
        }
        combined = result;
        combined.matchOffset += section.VirtualAddress;
    }
    return combined;
}

BF1942HudSetterSignatureResult FindInExecutableFilePath(
    const wchar_t* executablePath) noexcept
{
    BF1942HudSetterSignatureResult result = {};
    result.status = BF1942HudSetterSignatureStatus::InvalidArgument;
    if (executablePath == nullptr || executablePath[0] == L'\0')
    {
        return result;
    }

    const HANDLE file = CreateFileW(
        executablePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return result;
    }

    const HANDLE mapping = CreateFileMappingW(
        file,
        nullptr,
        PAGE_READONLY | SEC_IMAGE_NO_EXECUTE,
        0,
        0,
        nullptr);
    if (mapping == nullptr)
    {
        CloseHandle(file);
        return result;
    }

    const void* const image = MapViewOfFile(
        mapping,
        FILE_MAP_READ,
        0,
        0,
        0);
    if (image != nullptr)
    {
        result = FindInExecutableImage(
            reinterpret_cast<HMODULE>(const_cast<void*>(image)));
        UnmapViewOfFile(image);
    }
    CloseHandle(mapping);
    CloseHandle(file);
    return result;
}

BF1942HudSetterSignatureResult FindInExecutableFile(
    HMODULE executableModule) noexcept
{
    BF1942HudSetterSignatureResult result = {};
    result.status = BF1942HudSetterSignatureStatus::InvalidArgument;
    if (executableModule == nullptr)
    {
        return result;
    }
    std::array<wchar_t, 32768> executablePath = {};
    const DWORD pathLength = GetModuleFileNameW(
        executableModule,
        executablePath.data(),
        static_cast<DWORD>(executablePath.size()));
    if (pathLength == 0 ||
        pathLength >= static_cast<DWORD>(executablePath.size()))
    {
        return result;
    }
    return FindInExecutableFilePath(executablePath.data());
}
} // namespace

BF1942HudSetterSignatureResult FindBF1942HudSetter(
    const std::uint8_t* bytes,
    std::size_t byteCount) noexcept
{
    BF1942HudSetterSignatureResult result = {};
    if (bytes == nullptr)
    {
        result.status = BF1942HudSetterSignatureStatus::InvalidArgument;
        return result;
    }
    if (byteCount < kSetterSignature.size())
    {
        result.status = BF1942HudSetterSignatureStatus::NotFound;
        return result;
    }
    for (std::size_t offset = 0;
         offset <= byteCount - kSetterSignature.size();
         ++offset)
    {
        if (!SignatureMatchesAt(bytes + offset))
        {
            continue;
        }
        ++result.matchCount;
        if (result.matchCount == 1)
        {
            result.matchOffset = offset;
            std::memcpy(
                &result.fieldOffset,
                bytes + offset + kFieldOffsetPosition,
                sizeof(result.fieldOffset));
        }
    }
    if (result.matchCount == 0)
    {
        result.status = BF1942HudSetterSignatureStatus::NotFound;
    }
    else if (result.matchCount != 1 || result.fieldOffset == 0)
    {
        result.status = BF1942HudSetterSignatureStatus::Ambiguous;
        result.fieldOffset = 0;
    }
    else
    {
        result.status = BF1942HudSetterSignatureStatus::Found;
    }
    return result;
}

BF1942HudSetterSignatureResult FindBF1942HudSetterInExecutableFile(
    const wchar_t* path) noexcept
{
    return FindInExecutableFilePath(path);
}

bool BF1942HudToggle::Initialize(
    HMODULE executableModule,
    BF1942HudToggleLogCallback logCallback) noexcept
{
    ownerPointerAddress_ = 0;
    executableBase_ = reinterpret_cast<std::uintptr_t>(executableModule);
    setterAddress_ = 0;
    fieldOffset_ = 0;
    hudEnabled_ = true;
    consumedSequence_ = 0;
    appliedCount_ = 0;
    rejectedCount_ = 0;
    logCallback_ = logCallback;

    const BF1942FrameLimiterSignatureResult owner =
        FindBF1942FrameLimiterOwnerPointerInExecutableImage(
            executableModule);
    const BF1942HudSetterSignatureResult liveSetter =
        FindInExecutableImage(executableModule);
    BF1942HudSetterSignatureResult setter = liveSetter;
    bool usedDiskImage = false;
    if (setter.status != BF1942HudSetterSignatureStatus::Found ||
        setter.matchCount != 1 || setter.fieldOffset == 0)
    {
        setter = FindInExecutableFile(executableModule);
        usedDiskImage = setter.status ==
                BF1942HudSetterSignatureStatus::Found &&
            setter.matchCount == 1 && setter.fieldOffset != 0;
    }
    const std::uintptr_t imageBase =
        reinterpret_cast<std::uintptr_t>(executableModule);
    const bool setterRvaValid = setter.matchOffset != 0 &&
        imageBase <= std::numeric_limits<std::uintptr_t>::max() -
            setter.matchOffset;
    const std::uintptr_t liveSetterAddress = setterRvaValid
        ? imageBase + setter.matchOffset
        : 0;
    if (owner.status != BF1942FrameLimiterSignatureStatus::Found ||
        owner.matchCount != 1 || owner.ownerPointerAddress == 0 ||
        setter.status != BF1942HudSetterSignatureStatus::Found ||
        setter.matchCount != 1 || setter.fieldOffset == 0 ||
        !IsExecutableAddress(liveSetterAddress))
    {
        WriteLog(
            L"Native HUD toggle unavailable: Setup-owner status=%d matches=%zu address=%p; live legacy-byte-setter status=%d matches=%zu; disk status=%d matches=%zu field=0x%X. Other Quick Menu buttons remain active.",
            static_cast<int>(owner.status),
            owner.matchCount,
            reinterpret_cast<void*>(owner.ownerPointerAddress),
            static_cast<int>(liveSetter.status),
            liveSetter.matchCount,
            static_cast<int>(setter.status),
            setter.matchCount,
            setter.fieldOffset);
        return false;
    }
    ownerPointerAddress_ = owner.ownerPointerAddress;
    setterAddress_ = liveSetterAddress;
    fieldOffset_ = setter.fieldOffset;
    WriteLog(
        L"Native HUD toggle validated Setup owner %p and legacy executable-profile marker %p from the %s image (field +0x%X); runtime requests will dispatch the registered game.useHUD console object without opening the console.",
        reinterpret_cast<void*>(ownerPointerAddress_),
        reinterpret_cast<void*>(setterAddress_),
        usedDiskImage ? L"read-only on-disk" : L"live process",
        fieldOffset_);
    return true;
}

void BF1942HudToggle::Consume(LONG requestedSequence) noexcept
{
    if (requestedSequence <= 0 || requestedSequence == consumedSequence_)
    {
        return;
    }
    consumedSequence_ = requestedSequence;
    const HudConsoleObjectResult command =
        FindHudConsoleObject(executableBase_);
    if (command.object == 0 || command.isObjectActive == 0 ||
        command.executeObjectMethod == 0)
    {
        ++rejectedCount_;
        WriteLog(
            L"Native HUD toggle sequence %ld failed closed because the verified game.useHUD console object was not found (visited=%zu).",
            requestedSequence,
            command.visited);
        return;
    }
    using IsObjectActiveFn = bool(__thiscall*)(void* object);
    using ExecuteObjectMethodFn = void*(__thiscall*)(void* object);
    std::int32_t priorArgCount = 0;
    std::uintptr_t priorFirstArg = 0;
    std::uint8_t priorHasReturnValue = 0;
    std::uint32_t priorReturnValue = 0;
    if (!IsRangeAccessible(
            command.object + kConsoleObjectArgCountOffset,
            sizeof(priorArgCount),
            true) ||
        !IsRangeAccessible(
            command.object + kConsoleObjectFirstArgOffset,
            sizeof(std::uint32_t),
            true) ||
        !IsRangeAccessible(
            command.object + kConsoleObjectHasReturnValueOffset,
            sizeof(priorHasReturnValue),
            true) ||
        !IsRangeAccessible(
            command.object + kConsoleObjectReturnValueOffset,
            sizeof(priorReturnValue),
            true))
    {
        ++rejectedCount_;
        WriteLog(
            L"Native HUD toggle sequence %ld failed closed because the verified game.useHUD argument state is unavailable.",
            requestedSequence);
        return;
    }
    std::memcpy(
        &priorArgCount,
        reinterpret_cast<const void*>(
            command.object + kConsoleObjectArgCountOffset),
        sizeof(priorArgCount));
    if (!ReadAddress(
            command.object + kConsoleObjectFirstArgOffset,
            priorFirstArg))
    {
        ++rejectedCount_;
        return;
    }
    std::memcpy(
        &priorHasReturnValue,
        reinterpret_cast<const void*>(
            command.object + kConsoleObjectHasReturnValueOffset),
        sizeof(priorHasReturnValue));
    std::memcpy(
        &priorReturnValue,
        reinterpret_cast<const void*>(
            command.object + kConsoleObjectReturnValueOffset),
        sizeof(priorReturnValue));
    bool invoked = false;
    bool desired = false;
    std::uint8_t argumentValue = 0;
    __try
    {
        const bool active = reinterpret_cast<IsObjectActiveFn>(
            command.isObjectActive)(
                reinterpret_cast<void*>(command.object));
        if (active)
        {
            const std::int32_t queryArgCount = 0;
            std::memcpy(
                reinterpret_cast<void*>(
                    command.object + kConsoleObjectArgCountOffset),
                &queryArgCount,
                sizeof(queryArgCount));
            void* const currentValueAddress =
                reinterpret_cast<ExecuteObjectMethodFn>(
                    command.executeObjectMethod)(
                        reinterpret_cast<void*>(command.object));
            std::uint8_t currentValue = 0xFFU;
            if (IsRangeAccessible(
                    reinterpret_cast<std::uintptr_t>(currentValueAddress),
                    sizeof(currentValue),
                    false))
            {
                std::memcpy(
                    &currentValue,
                    currentValueAddress,
                    sizeof(currentValue));
            }
            if (currentValue > 1U)
            {
                __leave;
            }
            desired = currentValue == 0U;
            argumentValue = desired ? 1U : 0U;
            const std::int32_t setArgCount = 1;
            const std::uint32_t firstArg = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(&argumentValue));
            std::memcpy(
                reinterpret_cast<void*>(
                    command.object + kConsoleObjectArgCountOffset),
                &setArgCount,
                sizeof(setArgCount));
            std::memcpy(
                reinterpret_cast<void*>(
                    command.object + kConsoleObjectFirstArgOffset),
                &firstArg,
                sizeof(firstArg));
            (void)reinterpret_cast<ExecuteObjectMethodFn>(
                command.executeObjectMethod)(
                    reinterpret_cast<void*>(command.object));
            invoked = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        invoked = false;
    }
    const std::uint32_t restoredFirstArg =
        static_cast<std::uint32_t>(priorFirstArg);
    std::memcpy(
        reinterpret_cast<void*>(
            command.object + kConsoleObjectArgCountOffset),
        &priorArgCount,
        sizeof(priorArgCount));
    std::memcpy(
        reinterpret_cast<void*>(
            command.object + kConsoleObjectFirstArgOffset),
        &restoredFirstArg,
        sizeof(restoredFirstArg));
    std::memcpy(
        reinterpret_cast<void*>(
            command.object + kConsoleObjectHasReturnValueOffset),
        &priorHasReturnValue,
        sizeof(priorHasReturnValue));
    std::memcpy(
        reinterpret_cast<void*>(
            command.object + kConsoleObjectReturnValueOffset),
        &priorReturnValue,
        sizeof(priorReturnValue));
    if (!invoked)
    {
        ++rejectedCount_;
        WriteLog(
            L"Native HUD toggle sequence %ld failed closed because the verified game.useHUD console object was inactive, returned a non-Boolean query value, or raised an exception.",
            requestedSequence);
        return;
    }
    hudEnabled_ = desired;
    ++appliedCount_;
    WriteLog(
        L"Native HUD toggle sequence %ld executed registered console object %p method %p as game.useHUD=%u without opening the console (visited=%zu).",
        requestedSequence,
        reinterpret_cast<void*>(command.object),
        reinterpret_cast<void*>(command.executeObjectMethod),
        desired ? 1U : 0U,
        command.visited);
}

void BF1942HudToggle::LogSummary() const noexcept
{
    WriteLog(
        L"Native HUD toggle summary: applied=%ld rejected=%ld lastSequence=%ld resolved=%d legacySetter=%p requestedEnabled=%d legacyField=0x%X.",
        appliedCount_,
        rejectedCount_,
        consumedSequence_,
        ownerPointerAddress_ != 0 && setterAddress_ != 0 && fieldOffset_ != 0
            ? 1
            : 0,
        reinterpret_cast<void*>(setterAddress_),
        hudEnabled_ ? 1 : 0,
        fieldOffset_);
}

void BF1942HudToggle::WriteLog(const wchar_t* format, ...) const noexcept
{
    if (logCallback_ == nullptr || format == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1000> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    logCallback_(message.data());
}

} // namespace bfvr
