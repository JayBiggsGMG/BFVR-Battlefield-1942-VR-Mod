#include "stereo/D3D8FirstPersonArmPolicy.h"

namespace bfvr::stereo
{

bool ShouldSuppressBF1942FirstPersonArmDraw(
    const bool presentationMode,
    const bool firstPersonArmDraw,
    const bool nativeFirstPersonArmsEnabled) noexcept
{
    return presentationMode &&
        firstPersonArmDraw &&
        !nativeFirstPersonArmsEnabled;
}

} // namespace bfvr::stereo
