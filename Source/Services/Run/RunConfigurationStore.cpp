#include "Services/Run/RunConfigurationStore.h"
#include "Services/Project/ProjectDetector.h"

namespace Zenvra::Services::Run
{

void RunConfigurationStore::add_configuration(RunConfiguration config)
{
    m_configurations.push_back(std::move(config));
}

bool RunConfigurationStore::remove_configuration(std::size_t index)
{
    if (index >= m_configurations.size())
    {
        return false;
    }
    m_configurations.erase(m_configurations.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_active_index >= m_configurations.size() && !m_configurations.empty())
    {
        m_active_index = m_configurations.size() - 1;
    }
    return true;
}

void RunConfigurationStore::clear()
{
    m_configurations.clear();
    m_active_index = 0;
}

const RunConfiguration* RunConfigurationStore::get_active_configuration() const noexcept
{
    if (m_active_index < m_configurations.size())
    {
        return &m_configurations[m_active_index];
    }
    return nullptr;
}

RunConfiguration* RunConfigurationStore::get_active_configuration() noexcept
{
    if (m_active_index < m_configurations.size())
    {
        return &m_configurations[m_active_index];
    }
    return nullptr;
}

bool RunConfigurationStore::set_active_index(std::size_t index)
{
    if (index < m_configurations.size())
    {
        m_active_index = index;
        return true;
    }
    return false;
}

void RunConfigurationStore::populate_defaults(const Project::ProjectInfo& info, const std::filesystem::path& root)
{
    clear();

    const auto has_type = [&info](Project::ProjectType type) {
        for (const auto t : info.detected_types)
        {
            if (t == type) return true;
        }
        return false;
    };

    // 1. CMake binaries
    if (has_type(Project::ProjectType::CMake))
    {
        const auto cmake_targets = Project::ProjectDetector::detect_cmake_executable_targets(root);
        for (const auto& target_name : cmake_targets)
        {
            RunConfiguration cfg;
            cfg.name = target_name;
            cfg.type = ExecutionType::BinaryExecutable;
            cfg.build_before_run = true;
            cfg.build_target = target_name;
            cfg.working_directory = root;
            cfg.target_file = target_name;
            add_configuration(std::move(cfg));
        }
    }

    // 2. Cargo binaries
    if (has_type(Project::ProjectType::Cargo))
    {
        const auto cargo_targets = Project::ProjectDetector::detect_cargo_binary_targets(root);
        for (const auto& bin_name : cargo_targets)
        {
            RunConfiguration cfg;
            cfg.name = bin_name;
            cfg.type = ExecutionType::BinaryExecutable;
            cfg.build_before_run = true;
            cfg.build_target = bin_name;
            cfg.working_directory = root;
            cfg.target_file = bin_name;
            add_configuration(std::move(cfg));
        }
    }

    // 3. Python scripts
    if (has_type(Project::ProjectType::Python))
    {
        const auto py_targets = Project::ProjectDetector::detect_python_targets(root);
        const auto py_interp = Project::ProjectDetector::resolve_python_interpreter(root);

        for (const auto& py : py_targets)
        {
            RunConfiguration cfg;
            cfg.name = py.name;
            cfg.type = ExecutionType::InterpreterScript;
            cfg.build_before_run = false;
            cfg.target_file = py.artifact_or_script;
            cfg.interpreter_path = py_interp;
            cfg.working_directory = root;
            add_configuration(std::move(cfg));
        }
    }

    // 4. FreePascal
    if (has_type(Project::ProjectType::Pascal))
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        {
            if (entry.is_regular_file())
            {
                const auto ext = entry.path().extension().string();
                if (ext == ".lpr" || ext == ".pas" || ext == ".pp")
                {
                    RunConfiguration cfg;
                    cfg.name = entry.path().filename().string();
                    cfg.type = ExecutionType::BinaryExecutable;
                    cfg.build_before_run = true;
                    cfg.build_target = entry.path().filename().string();
                    cfg.working_directory = root;
                    cfg.target_file = entry.path().stem().string();
                    add_configuration(std::move(cfg));
                    break; // Pick primary entry
                }
            }
        }
    }

    m_active_index = 0;
}

} // namespace Zenvra::Services::Run
