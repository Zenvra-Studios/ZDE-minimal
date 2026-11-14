#pragma once

#include <cstdint>
#include <string>

namespace Zenvra::Services::Language::Detection
{

enum class DetectionMatchReason : std::uint8_t
{
    UserOverride = 0,
    SpecialFilename,
    FileExtension,
    FirstLinePattern,
    ProjectMarker,
    ContentHeuristic,
    Fallback
};

struct DetectionResult
{
    std::string language_id;
    DetectionMatchReason reason{DetectionMatchReason::Fallback};
    float confidence{0.0F}; // 0.0 to 1.0

    [[nodiscard]] bool is_valid() const noexcept { return !language_id.empty() && confidence > 0.0F; }
};

} // namespace Zenvra::Services::Language::Detection

