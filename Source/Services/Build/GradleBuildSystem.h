#pragma once

#include "Services/Build/BuildSystem.h"
#include "Services/Process/ProcessRunner.h"

namespace Zenvra::Services::Build
{

class GradleBuildSystem : public IBuildSystem
{
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Gradle"; }
    [[nodiscard]] bool detect(const std::filesystem::path& root) override;
    BuildResult configure(const std::filesystem::path& root, const std::string& preset = "") override;
    BuildResult build(
        const std::filesystem::path& root,
        const std::string& target = "",
        const std::string& preset = "",
        const std::string& config = "Debug") override;
    BuildResult clean(const std::filesystem::path& root) override;
    [[nodiscard]] std::vector<BuildTarget> get_targets(const std::filesystem::path& root) override;

private:
    Process::ProcessRunner m_runner;
};

} // namespace Zenvra::Services::Build
