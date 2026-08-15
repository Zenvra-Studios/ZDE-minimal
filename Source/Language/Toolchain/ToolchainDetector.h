#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Language::Toolchain
{

enum class ToolchainKind
{
    None,
    MinGW_GCC,
    MSVC,
    Clang,
    AppleClang,
    Gcc,
};

enum class ToolchainStatus
{
    Ready,                  // Compiler and Standard SDK headers detected
    MissingCompiler,        // No C/C++ compiler found on the system
    MissingSdk,             // Compiler executable found, but Standard Library / SDK headers missing
    MissingBuildSystem,     // Compiler found, but CMake / Ninja not detected
};

struct ToolchainInfo
{
    ToolchainKind kind = ToolchainKind::None;
    ToolchainStatus status = ToolchainStatus::MissingCompiler;
    std::string name;
    std::string compiler_version;
    std::filesystem::path compiler_path;
    std::filesystem::path sdk_include_path;
    std::vector<std::filesystem::path> system_include_paths;
    bool has_standard_headers = false;
    std::string user_status_text;
    std::string warning_message;
    std::string installation_guide;

    [[nodiscard]] bool is_ready() const noexcept { return status == ToolchainStatus::Ready; }
};

class ToolchainDetector
{
public:
    [[nodiscard]] static ToolchainDetector& instance();

    [[nodiscard]] const ToolchainInfo& get_active_toolchain();
    void refresh();

    [[nodiscard]] bool has_valid_sdk() const;
    [[nodiscard]] std::string get_status_bar_label() const;
    [[nodiscard]] std::string get_tooltip_guidance() const;

private:
    ToolchainDetector();
    void detect_environment();

    ToolchainInfo m_info;
    bool m_detected = false;
};

} // namespace Zenvra::Language::Toolchain
