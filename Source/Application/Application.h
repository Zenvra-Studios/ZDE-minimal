#pragma once

#include "Application/ApplicationSpecification.h"
#include "Application/ViewModels/StudioViewModel.h"
#include "Platform/IPlatformWindow.h"

#include <memory>

namespace Zenvra::Application
{

class Application
{
public:
    explicit Application(ApplicationSpecification specification);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] int run();
    void request_close();

    [[nodiscard]] Platform::IPlatformWindow* get_window() const noexcept;
    [[nodiscard]] const Commands::CommandRegistry* get_commands() const noexcept;

private:
    [[nodiscard]] bool initialize();
    void show_about() const;

    ApplicationSpecification m_specification;
    std::unique_ptr<Platform::IPlatformWindow> m_window;
    std::unique_ptr<ViewModels::StudioViewModel> m_studio_view_model;
};

} // namespace Zenvra::Application
