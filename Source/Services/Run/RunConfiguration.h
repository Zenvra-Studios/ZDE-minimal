#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Zenvra::Services::Run
{

enum class ExecutionType
{
    BinaryExecutable,
    InterpreterScript
};

[[nodiscard]] constexpr std::string_view to_string(ExecutionType type) noexcept
{
    switch (type)
    {
    case ExecutionType::BinaryExecutable: return "BinaryExecutable";
    case ExecutionType::InterpreterScript: return "InterpreterScript";
    }
    return "Unknown";
}

struct RunConfiguration
{
    std::string name;
    ExecutionType type = ExecutionType::BinaryExecutable;
    std::filesystem::path target_file;
    std::filesystem::path interpreter_path;
    std::filesystem::path working_directory;
    std::vector<std::string> arguments;
    std::vector<std::pair<std::string, std::string>> environment_variables;
    bool build_before_run = false;
    std::string build_target;
    std::string build_config = "Debug";
};

} // namespace Zenvra::Services::Run
