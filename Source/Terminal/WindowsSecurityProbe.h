#pragma once

#include "Terminal/TerminalExitDecoder.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Zenvra::Terminal
{

enum class SACState : int
{
    Unknown = -1,
    Disabled = 0,
    Evaluation = 1,
    Enforced = 2
};

enum class DefenderServiceStatus
{
    Unknown,
    Present,
    MissingOrStripped
};

enum class SystemHealthClassification
{
    Healthy,
    Debloated,
    Broken
};

struct WindowsSecurityInfo
{
    SACState sac_state = SACState::Unknown;
    DefenderServiceStatus defender_status = DefenderServiceStatus::Unknown;
    bool is_franken_mod = false;
    bool dotnet_installed = false;
    uint32_t dotnet_release = 0;
    
    std::string product_name;
    std::string display_version;
    std::string build_number;
    uint32_t ubr = 0;

    SystemHealthClassification classification = SystemHealthClassification::Healthy;
    std::vector<std::string> classification_reasons;
};

class WindowsSecurityProbe
{
public:
    /**
     * @brief Probes Windows security posture and OS components.
     * Caches the result per ZDE session so overhead is paid at most once.
     */
    static const WindowsSecurityInfo& get_cached_info() noexcept;

    /**
     * @brief Performs a fresh probe of system state.
     */
    static WindowsSecurityInfo probe_system();

    /**
     * @brief Generates actionable troubleshooting hints based on probe results and exit info.
     * Returns an empty string if system is healthy or no actionable hint applies.
     */
    static std::string generate_troubleshooting_hint(const TerminalExitDecoded& exit_info);

    /**
     * @brief Formats SAC state as human-readable string.
     */
    static std::string sac_state_to_string(SACState state);

    /**
     * @brief Formats classification as human-readable string.
     */
    static std::string classification_to_string(SystemHealthClassification classification);
};

} // namespace Zenvra::Terminal
