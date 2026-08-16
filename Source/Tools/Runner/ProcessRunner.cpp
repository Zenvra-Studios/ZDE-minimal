#include "Tools/Runner/ProcessRunner.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace Zenvra::Tools::Runner
{

bool ProcessRunner::launch_process(
    const ProcessExecutionOptions& options,
    std::function<void(std::string_view)> /*stdout_callback*/) const
{
    if (!std::filesystem::exists(options.executable_path))
    {
        return false;
    }

    std::ostringstream cmd;
    cmd << "\"" << options.executable_path.string() << "\"";
    for (const auto& arg : options.arguments)
    {
        cmd << " " << arg;
    }

    if (options.run_in_background)
    {
#if defined(_WIN32)
        cmd << " &";
#else
        cmd << " &";
#endif
    }

    const int res = std::system(cmd.str().c_str());
    return (res == 0);
}

void ProcessRunner::terminate_active_process() const
{
    // No-op or kill signal
}

} // namespace Zenvra::Tools::Runner
