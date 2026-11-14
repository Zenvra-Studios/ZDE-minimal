#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Zenvra::Services::LSP::Capabilities
{

struct CapabilityMatrix
{
    bool completion_provider{false};
    bool hover_provider{false};
    bool definition_provider{false};
    bool declaration_provider{false};
    bool type_definition_provider{false};
    bool implementation_provider{false};
    bool references_provider{false};
    bool document_highlight_provider{false};
    bool document_symbol_provider{false};
    bool workspace_symbol_provider{false};
    bool code_action_provider{false};
    bool document_formatting_provider{false};
    bool document_range_formatting_provider{false};
    bool rename_provider{false};
    bool signature_help_provider{false};
    bool semantic_tokens_provider{false};
    bool inlay_hint_provider{false};

    std::vector<char> completion_trigger_characters;
    std::vector<char> signature_trigger_characters;
    std::vector<std::string> semantic_token_types;
    std::vector<std::string> semantic_token_modifiers;

    [[nodiscard]] nlohmann::json to_json() const;
    static CapabilityMatrix from_server_capabilities(const nlohmann::json& caps);
};

} // namespace Zenvra::Services::LSP::Capabilities

