#include "Services/Language/Routing/ServerRouter.h"

namespace Zenvra::Services::Language::Routing
{

ServerRouter& ServerRouter::instance() noexcept
{
    static auto* router = new ServerRouter();
    return *router;
}

void ServerRouter::set_server_override(std::string language_id, std::string server_id)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_server_overrides[std::move(language_id)] = std::move(server_id);
}

void ServerRouter::clear_server_overrides()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_server_overrides.clear();
}

RouteTarget ServerRouter::resolve_route(
    const std::filesystem::path& file_path,
    std::string_view file_content,
    const std::filesystem::path& workspace_root) const
{
    const auto detected = Registry::LanguageRegistry::instance().detect_language(file_path, file_content, workspace_root);
    RouteTarget target;
    target.language_id = detected.language_id;

    if (const auto* def = Registry::LanguageRegistry::instance().find_by_id(detected.language_id))
    {
        target.server_id = def->server_association.default_server_id;
        target.candidate_executables = def->server_association.candidate_executables;
        target.default_arguments = def->server_association.default_arguments;
        target.has_server = !target.candidate_executables.empty() || !target.server_id.empty();

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (auto it = m_server_overrides.find(target.language_id); it != m_server_overrides.end())
        {
            target.server_id = it->second;
        }
    }

    return target;
}

} // namespace Zenvra::Services::Language::Routing

