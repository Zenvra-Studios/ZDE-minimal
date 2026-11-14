#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::LSP::Process
{

struct ProcessSpec
{
    std::filesystem::path executable_path;
    std::string executable; // Convenience string alias
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::unordered_map<std::string, std::string> environment_variables;
    bool create_job_object{true}; // Win32 Job Object for clean process tree termination
};

} // namespace Zenvra::Services::LSP::Process

