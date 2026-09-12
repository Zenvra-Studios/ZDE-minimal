#pragma once

#include "Services/Toolchain/Toolchain.h"

namespace Zenvra::Services::Toolchain
{

class FreePascalToolchain : public IToolchain
{
public:
    [[nodiscard]] ToolchainType type() const noexcept override { return ToolchainType::FreePascal; }
    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }
    bool detect(const std::filesystem::path& workspace_root = {}) override;
    [[nodiscard]] bool is_available() const override { return m_available; }
    [[nodiscard]] std::string version() const override { return m_version; }
    [[nodiscard]] std::filesystem::path executable() const override { return m_executable; }
    [[nodiscard]] ToolchainSource source() const override { return m_source; }

private:
    std::string m_name = "Free Pascal (fpc)";
    std::filesystem::path m_executable;
    std::string m_version;
    ToolchainSource m_source = ToolchainSource::System;
    bool m_available = false;
};

} // namespace Zenvra::Services::Toolchain
