#pragma once

#include "Services/Language/Definitions/LanguageDefinition.h"

#include <string>
#include <vector>

namespace Zenvra::Services::Language::Registry
{

struct ValidationIssue
{
    enum class Severity { Error, Warning };
    Severity severity{Severity::Error};
    std::string language_id;
    std::string message;
};

class LanguageValidator
{
public:
    [[nodiscard]] static std::vector<ValidationIssue> validate(
        const Definitions::LanguageDefinition& def,
        const std::vector<Definitions::LanguageDefinition>& existing_definitions = {});
};

} // namespace Zenvra::Services::Language::Registry

