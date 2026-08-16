#pragma once

#include "Application/ApplicationSpecification.h"
#include "Application/ViewModels/StudioViewModel.h"
#include "Platform/IPlatformWindow.h"
#include "Services/Build/BuildService.h"
#include "Services/Execution/ExecutionService.h"
#include "Utility/MultiContext.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace Zenvra::Application
{

struct WindowContext
{
    std::uint64_t context_id = 0;
    std::unique_ptr<Platform::IPlatformWindow> window;
    std::unique_ptr<ViewModels::StudioViewModel> studio_view_model;
};

class Application
{
public:
    explicit Application(ApplicationSpecification specification);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] int run();
    void request_close();

    [[nodiscard]] Platform::IPlatformWindow* create_new_window(
        std::optional<std::filesystem::path> initial_path = std::nullopt);
    void close_window(Platform::IPlatformWindow* window);
    [[nodiscard]] std::size_t get_window_count() const noexcept;

    [[nodiscard]] Platform::IPlatformWindow* get_window() const noexcept;
    [[nodiscard]] const Commands::CommandRegistry* get_commands() const noexcept;

private:
    [[nodiscard]] bool initialize();
    void show_about(Platform::IPlatformWindow* window = nullptr) const;

    ApplicationSpecification m_specification;
    std::vector<std::unique_ptr<WindowContext>> m_windows;
    std::shared_ptr<Services::Build::BuildService> m_build_service;
    std::shared_ptr<Services::Execution::ExecutionService> m_execution_service;
};

} // namespace Zenvra::Application
