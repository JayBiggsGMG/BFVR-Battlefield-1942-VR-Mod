#include "stereo/D3D8FirstPersonArmPolicy.h"

#include <array>

namespace bfvr::stereo
{

D3D8FirstPersonPartKind ClassifyD3D8FirstPersonPartTemplateName(
    const std::string_view templateName) noexcept
{
    std::array<char, 128> normalized = {};
    std::size_t length = 0;
    for (const unsigned char value : templateName)
    {
        char normalizedValue = 0;
        if (value >= 'A' && value <= 'Z')
        {
            normalizedValue = static_cast<char>(value - 'A' + 'a');
        }
        else if ((value >= 'a' && value <= 'z') ||
                 (value >= '0' && value <= '9'))
        {
            normalizedValue = static_cast<char>(value);
        }
        if (normalizedValue != 0)
        {
            if (length == normalized.size())
            {
                return D3D8FirstPersonPartKind::UnknownOrCombined;
            }
            normalized[length++] = normalizedValue;
        }
    }
    const std::string_view compact(normalized.data(), length);
    return compact.find("lefthand") != std::string_view::npos ||
            compact.find("righthand") != std::string_view::npos
        ? D3D8FirstPersonPartKind::SeparateHand
        : D3D8FirstPersonPartKind::UnknownOrCombined;
}

bool ShouldSuppressBF1942FirstPersonArmDraw(
    const bool presentationMode,
    const bool firstPersonPartDraw,
    const D3D8FirstPersonPartKind partKind,
    const bool armsAndCombinedEnabled,
    const bool separateHandsEnabled) noexcept
{
    if (!presentationMode || !firstPersonPartDraw)
    {
        return false;
    }
    return partKind == D3D8FirstPersonPartKind::SeparateHand
        ? !separateHandsEnabled
        : !armsAndCombinedEnabled;
}

} // namespace bfvr::stereo
