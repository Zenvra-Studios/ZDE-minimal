#pragma once

#include "Services/Language/Definitions/LanguageDefinition.h"
#include "Services/Language/Registry/LanguageRegistry.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Zenvra::Services::Language::Routing
{

struct RouteTarget
{
    std::string language_id;
    std::string server_id;
    std::vector<std::string> candidate_executables;
    std::vector<std::string> default_arguments;
    bool has_server{false};
};

class ServerRouter
{
public:
    static ServerRouter& instance() noexcept;

    /// Resolves the route target for a given file and workspace
    [[nodiscard]] RouteTarget resolve_route(
        const std::filesystem::path& file_path,
        std::string_view file_content = {},
        const std::filesystem::path& workspace_root = {}) const;

    /// Overrides server routing for a specific language
    void set_server_override(std::string language_id, std::string server_id);
    void clear_server_overrides();

private:
    ServerRouter() = default;

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::string> m_server_overrides;
};

} // namespace Zenvra::Services::Language::Routing

