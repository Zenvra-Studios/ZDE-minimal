#pragma once

#include "Services/Installation/Catalog/ServerCatalog.h"

#include <string>
#include <vector>

namespace Zenvra::Services::Installation::Catalog
{

struct CatalogValidationIssue
{
    std::string server_id;
    std::string message;
};

class CatalogValidator
{
public:
    [[nodiscard]] static std::vector<CatalogValidationIssue> validate(const ServerCatalogEntry& entry);
};

} // namespace Zenvra::Services::Installation::Catalog

