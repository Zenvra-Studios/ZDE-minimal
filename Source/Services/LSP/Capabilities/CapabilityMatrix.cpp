#include "Services/LSP/Capabilities/CapabilityMatrix.h"

namespace Zenvra::Services::LSP::Capabilities
{

nlohmann::json CapabilityMatrix::to_json() const
{
    nlohmann::json j;
    j["completionProvider"] = completion_provider;
    j["hoverProvider"] = hover_provider;
    j["definitionProvider"] = definition_provider;
    j["declarationProvider"] = declaration_provider;
    j["typeDefinitionProvider"] = type_definition_provider;
    j["implementationProvider"] = implementation_provider;
    j["referencesProvider"] = references_provider;
    j["documentHighlightProvider"] = document_highlight_provider;
    j["documentSymbolProvider"] = document_symbol_provider;
    j["workspaceSymbolProvider"] = workspace_symbol_provider;
    j["codeActionProvider"] = code_action_provider;
    j["documentFormattingProvider"] = document_formatting_provider;
    j["renameProvider"] = rename_provider;
    j["signatureHelpProvider"] = signature_help_provider;
    j["semanticTokensProvider"] = semantic_tokens_provider;
    j["inlayHintProvider"] = inlay_hint_provider;
    return j;
}

CapabilityMatrix CapabilityMatrix::from_server_capabilities(const nlohmann::json& caps)
{
    CapabilityMatrix m;
    if (!caps.is_object()) return m;

    if (caps.contains("completionProvider"))
    {
        m.completion_provider = true;
        if (caps["completionProvider"].is_object() && caps["completionProvider"].contains("triggerCharacters"))
        {
            for (const auto& tc : caps["completionProvider"]["triggerCharacters"])
            {
                if (tc.is_string() && !tc.get<std::string>().empty())
                {
                    m.completion_trigger_characters.push_back(tc.get<std::string>()[0]);
                }
            }
        }
    }

    if (caps.contains("hoverProvider"))
        m.hover_provider = caps["hoverProvider"].is_boolean() ? caps["hoverProvider"].get<bool>() : true;

    if (caps.contains("definitionProvider"))
        m.definition_provider = caps["definitionProvider"].is_boolean() ? caps["definitionProvider"].get<bool>() : true;

    if (caps.contains("declarationProvider"))
        m.declaration_provider = caps["declarationProvider"].is_boolean() ? caps["declarationProvider"].get<bool>() : true;

    if (caps.contains("typeDefinitionProvider"))
        m.type_definition_provider = caps["typeDefinitionProvider"].is_boolean() ? caps["typeDefinitionProvider"].get<bool>() : true;

    if (caps.contains("implementationProvider"))
        m.implementation_provider = caps["implementationProvider"].is_boolean() ? caps["implementationProvider"].get<bool>() : true;

    if (caps.contains("referencesProvider"))
        m.references_provider = caps["referencesProvider"].is_boolean() ? caps["referencesProvider"].get<bool>() : true;

    if (caps.contains("documentFormattingProvider"))
        m.document_formatting_provider = caps["documentFormattingProvider"].is_boolean() ? caps["documentFormattingProvider"].get<bool>() : true;

    if (caps.contains("renameProvider"))
        m.rename_provider = caps["renameProvider"].is_boolean() ? caps["renameProvider"].get<bool>() : true;

    if (caps.contains("signatureHelpProvider"))
    {
        m.signature_help_provider = true;
        if (caps["signatureHelpProvider"].is_object() && caps["signatureHelpProvider"].contains("triggerCharacters"))
        {
            for (const auto& tc : caps["signatureHelpProvider"]["triggerCharacters"])
            {
                if (tc.is_string() && !tc.get<std::string>().empty())
                {
                    m.signature_trigger_characters.push_back(tc.get<std::string>()[0]);
                }
            }
        }
    }

    if (caps.contains("semanticTokensProvider"))
    {
        m.semantic_tokens_provider = true;
        if (caps["semanticTokensProvider"].is_object() && caps["semanticTokensProvider"].contains("legend"))
        {
            const auto& legend = caps["semanticTokensProvider"]["legend"];
            if (legend.contains("tokenTypes") && legend["tokenTypes"].is_array())
                m.semantic_token_types = legend["tokenTypes"].get<std::vector<std::string>>();
            if (legend.contains("tokenModifiers") && legend["tokenModifiers"].is_array())
                m.semantic_token_modifiers = legend["tokenModifiers"].get<std::vector<std::string>>();
        }
    }

    if (caps.contains("inlayHintProvider"))
        m.inlay_hint_provider = caps["inlayHintProvider"].is_boolean() ? caps["inlayHintProvider"].get<bool>() : true;

    return m;
}

} // namespace Zenvra::Services::LSP::Capabilities

