#pragma once

#include "Services/Output/OutputLogManager.h"
#include "Tools/Builder/CMakeBuilder.h"

#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <string_view>
#include <thread>

namespace Zenvra::Services::Build
{

class BuildService
{
public:
    BuildService() = default;
    ~BuildService();

    BuildService(const BuildService&) = delete;
    BuildService& operator=(const BuildService&) = delete;

    bool build_async(
        const Tools::Builder::CMakeBuildOptions& options,
        std::function<void(std::string_view)> output_callback = {},
        std::function<void(bool)> completion_callback = {});

    [[nodiscard]] bool is_building() const noexcept { return m_is_building.load(); }

private:
    Tools::Builder::CMakeBuilder m_builder;
    std::atomic<bool> m_is_building{false};
    std::thread m_worker_thread;
    std::mutex m_mutex;
};

} // namespace Zenvra::Services::Build
