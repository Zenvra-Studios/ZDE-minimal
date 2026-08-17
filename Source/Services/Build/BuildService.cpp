#include "Services/Build/BuildService.h"

namespace Zenvra::Services::Build
{

BuildService::~BuildService()
{
    if (m_worker_thread.joinable())
    {
        m_worker_thread.join();
    }
}

bool BuildService::build_async(
    const Tools::Builder::CMakeBuildOptions& options,
    std::function<void(std::string_view)> output_callback,
    std::function<void(bool)> completion_callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_is_building.load())
    {
        return false;
    }

    if (m_worker_thread.joinable())
    {
        m_worker_thread.join();
    }

    m_is_building.store(true);

    m_worker_thread = std::thread([this, options, output_cb = std::move(output_callback), completion_cb = std::move(completion_callback)]() {
        Output::OutputLogManager::instance().append_line(
            Output::OutputCategory::Build,
            "[Build] Starting CMake build target: " + options.target_name);

        const auto result = m_builder.build_target(options, [output_cb](std::string_view chunk) {
            Output::OutputLogManager::instance().append_text(Output::OutputCategory::Build, chunk);
            if (output_cb)
            {
                output_cb(chunk);
            }
        });

        Output::OutputLogManager::instance().append_line(
            Output::OutputCategory::Build,
            result.success ? "[Build] Finished successfully." : "[Build] Build failed with exit code: " + std::to_string(result.exit_code));

        m_is_building.store(false);

        if (completion_cb)
        {
            completion_cb(result.success);
        }
    });

    return true;
}

} // namespace Zenvra::Services::Build
