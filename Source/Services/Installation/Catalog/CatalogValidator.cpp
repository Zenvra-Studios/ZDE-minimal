#include "Services/Installation/Catalog/CatalogValidator.h"

namespace Zenvra::Services::Installation::Catalog
{

std::vector<CatalogValidationIssue> CatalogValidator::validate(const ServerCatalogEntry& entry)
{
    std::vector<CatalogValidationIssue> issues;
    if (entry.id.empty())
    {
        issues.push_back({.server_id = entry.id, .message = "Catalog entry ID cannot be empty."});
    }
    if (entry.artifacts.empty())
    {
        issues.push_back({.server_id = entry.id, .message = "Catalog entry must declare at least one platform artifact."});
    }
    for (const auto& art : entry.artifacts)
    {
        if (art.platform.empty())
        {
            issues.push_back({.server_id = entry.id, .message = "Artifact must specify a platform ('windows', 'linux', 'macos')."});
        }
        if (art.provider_type.empty())
        {
            issues.push_back({.server_id = entry.id, .message = "Artifact must specify a provider type."});
        }
    }
    return issues;
}

} // namespace Zenvra::Services::Installation::Catalog

