#pragma once

#include <string>

namespace Zenvra::Services::Process
{

struct ProcessResult
{
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    bool success = false;
};

} // namespace Zenvra::Services::Process
