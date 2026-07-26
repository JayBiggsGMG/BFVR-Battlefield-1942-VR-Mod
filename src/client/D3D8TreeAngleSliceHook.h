#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace bfvr
{

using D3D8TreeAngleSliceLogCallback = void (*)(const wchar_t* message);

// Captures the exact retail TreeMesh::drawBlocks group-0 context while the
// original routine is active. The caller owns MinHook initialization.
class D3D8TreeAngleSliceHook
{
public:
    D3D8TreeAngleSliceHook();
    ~D3D8TreeAngleSliceHook();

    D3D8TreeAngleSliceHook(const D3D8TreeAngleSliceHook&) = delete;
    D3D8TreeAngleSliceHook& operator=(const D3D8TreeAngleSliceHook&) = delete;

    bool Create(void* gameImage, D3D8TreeAngleSliceLogCallback logCallback);
    bool Enable();
    [[nodiscard]] std::optional<std::uint32_t> RemapEyeStartIndex(
        const stereo::Vec3& sourceCamera,
        const stereo::Vec3& eyeCamera,
        std::uint32_t originalStartIndex,
        std::uint32_t primitiveCount) noexcept;
    void DisableAndRemove();
    void LogSummary() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bfvr
