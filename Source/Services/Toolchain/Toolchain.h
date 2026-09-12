#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::Services::Toolchain
{

enum class ToolchainType : std::uint8_t
{
    Cpp,
    Rust,
    Java,
    Python,
    FreePascal,
    Custom
};

enum class ToolchainSource : std::uint8_t
{
    Bundled,
    System,
    VirtualEnv,
    Custom
};

class IToolchain
{
public:
    virtual ~IToolchain() = default;

    [[nodiscard]] virtual ToolchainType type() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool detect(const std::filesystem::path& workspace_root = {}) = 0;
    [[nodiscard]] virtual bool is_available() const = 0;
    [[nodiscard]] virtual std::string version() const = 0;
    [[nodiscard]] virtual std::filesystem::path executable() const = 0;
    [[nodiscard]] virtual ToolchainSource source() const = 0;
};

} // namespace Zenvra::Services::Toolchain
