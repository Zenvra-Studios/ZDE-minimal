#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Zenvra::Application
{

struct ApplicationSpecification
{
    std::string name = "Zenvra Development Studio";
    std::uint32_t width = 1600;
    std::uint32_t height = 900;
    bool custom_titlebar = true;
    bool enable_docking = true;
    bool enable_viewports = false;
    bool smoke_test = false;
    std::optional<std::filesystem::path> initial_path = std::nullopt;
};

} // namespace Zenvra::Application
