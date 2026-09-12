#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Zenvra::Services::Process
{

struct EnvironmentVariable
{
    std::string key;
    std::string value;
};

struct ProcessSpec
{
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::vector<EnvironmentVariable> environment;
    bool redirect_stdout = true;
    bool redirect_stderr = true;
    bool run_in_background = false;
};

} // namespace Zenvra::Services::Process
