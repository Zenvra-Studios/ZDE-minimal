#include "Application/Application.h"

#include <algorithm>
#include <string_view>
#include <utility>

static bool has_argument(int argument_count, char** argument_values, std::string_view expected_argument)
{
    return std::any_of(
        argument_values + 1,
        argument_values + argument_count,
        [expected_argument](const char* argument) { return argument == expected_argument; });
}

int main(int argument_count, char** argument_values)
{
    Zenvra::Application::ApplicationSpecification specification;

    const bool safe_ui = has_argument(argument_count, argument_values, "--safe-ui");
    const bool native_titlebar = has_argument(argument_count, argument_values, "--native-titlebar");
    const bool smoke_test = has_argument(argument_count, argument_values, "--smoke-test");
    specification.custom_titlebar = !safe_ui && !native_titlebar;
    specification.enable_docking = !safe_ui;
    specification.enable_viewports = false;
    specification.smoke_test = smoke_test;

    Zenvra::Application::Application application(std::move(specification));
    return application.run();
}
