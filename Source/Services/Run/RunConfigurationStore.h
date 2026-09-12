#pragma once

#include "Services/Project/ProjectInfo.h"
#include "Services/Run/RunConfiguration.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Zenvra::Services::Run
{

class RunConfigurationStore
{
public:
    RunConfigurationStore() = default;

    void add_configuration(RunConfiguration config);
    bool remove_configuration(std::size_t index);
    void clear();

    [[nodiscard]] const std::vector<RunConfiguration>& get_configurations() const noexcept { return m_configurations; }
    [[nodiscard]] std::vector<RunConfiguration>& get_configurations() noexcept { return m_configurations; }

    [[nodiscard]] const RunConfiguration* get_active_configuration() const noexcept;
    [[nodiscard]] RunConfiguration* get_active_configuration() noexcept;

    [[nodiscard]] std::size_t get_active_index() const noexcept { return m_active_index; }
    bool set_active_index(std::size_t index);

    void populate_defaults(const Project::ProjectInfo& info, const std::filesystem::path& root);

private:
    std::vector<RunConfiguration> m_configurations;
    std::size_t m_active_index = 0;
};

} // namespace Zenvra::Services::Run
