#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Build
{

struct BuildTarget
{
    std::string name;
    std::string output_artifact;
    bool is_default = false;
};

struct BuildResult
{
    bool success = false;
    int exit_code = 0;
    std::string command;
    std::string stdout_output;
    std::string stderr_output;
    std::filesystem::path artifact_path;
};

class IBuildSystem
{
public:
    virtual ~IBuildSystem() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool detect(const std::filesystem::path& root) = 0;
    virtual BuildResult configure(const std::filesystem::path& root, const std::string& preset = "") = 0;
    virtual BuildResult build(
        const std::filesystem::path& root,
        const std::string& target = "",
        const std::string& preset = "",
        const std::string& config = "Debug") = 0;
    virtual BuildResult clean(const std::filesystem::path& root) = 0;
    [[nodiscard]] virtual std::vector<BuildTarget> get_targets(const std::filesystem::path& root) = 0;
};

} // namespace Zenvra::Services::Build
