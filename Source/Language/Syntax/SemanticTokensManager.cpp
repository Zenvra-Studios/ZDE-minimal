#include "Language/Syntax/SemanticTokensManager.h"

#include <algorithm>

namespace Zenvra::Language::Syntax
{

std::vector<SemanticTokenSpan> SemanticTokensManager::decode_lsp_tokens(
    std::span<const uint32_t> raw_data,
    std::span<const std::string> legend_types,
    std::span<const std::string> /*legend_modifiers*/) noexcept
{
    std::vector<SemanticTokenSpan> result;
    if (raw_data.empty() || raw_data.size() % 5 != 0)
    {
        return result;
    }

    result.reserve(raw_data.size() / 5);

    std::size_t current_line = 0;
    std::size_t current_char = 0;

    for (std::size_t i = 0; i < raw_data.size(); i += 5)
    {
        const uint32_t delta_line = raw_data[i];
        const uint32_t delta_start = raw_data[i + 1];
        const uint32_t length = raw_data[i + 2];
        const uint32_t token_type_index = raw_data[i + 3];
        const uint32_t token_modifiers = raw_data[i + 4];

        if (delta_line > 0)
        {
            current_line += delta_line;
            current_char = delta_start;
        }
        else
        {
            current_char += delta_start;
        }

        SemanticTokenType token_type = SemanticTokenType::Custom;
        if (!legend_types.empty() && token_type_index < legend_types.size())
        {
            token_type = string_to_semantic_token_type(legend_types[token_type_index]);
        }
        else if (token_type_index < static_cast<uint32_t>(SemanticTokenType::Count))
        {
            token_type = static_cast<SemanticTokenType>(token_type_index);
        }

        result.push_back(SemanticTokenSpan{
            .line = current_line,
            .start_column = current_char,
            .length = static_cast<std::size_t>(length),
            .type = token_type,
            .modifiers = token_modifiers
        });
    }

    return result;
}

void SemanticTokensManager::update_document_tokens(
    const std::string& uri,
    std::vector<SemanticTokenSpan> tokens)
{
    std::map<std::size_t, std::vector<SemanticTokenSpan>> line_map;
    for (auto&& span : tokens)
    {
        line_map[span.line].push_back(span);
    }

    // Sort spans within each line by start_column
    for (auto& [line, spans] : line_map)
    {
        std::sort(spans.begin(), spans.end(), [](const SemanticTokenSpan& a, const SemanticTokenSpan& b) {
            return a.start_column < b.start_column;
        });
    }

    m_document_tokens[uri] = std::move(line_map);
}

std::vector<SemanticTokenSpan> SemanticTokensManager::get_tokens_for_line(
    const std::string& uri,
    std::size_t line) const
{
    const auto doc_it = m_document_tokens.find(uri);
    if (doc_it == m_document_tokens.end())
    {
        return {};
    }

    const auto line_it = doc_it->second.find(line);
    if (line_it == doc_it->second.end())
    {
        return {};
    }

    return line_it->second;
}

bool SemanticTokensManager::has_tokens(const std::string& uri) const noexcept
{
    const auto it = m_document_tokens.find(uri);
    return it != m_document_tokens.end() && !it->second.empty();
}

void SemanticTokensManager::clear_document_tokens(const std::string& uri) noexcept
{
    m_document_tokens.erase(uri);
}

UI::Theme::Color SemanticTokensManager::get_token_color(
    SemanticTokenType type,
    const UI::Editor::StudioEditorPalette& palette) noexcept
{
    switch (type)
    {
        case SemanticTokenType::Keyword:
        case SemanticTokenType::Modifier:
            return palette.keyword;

        case SemanticTokenType::Type:
        case SemanticTokenType::Class:
        case SemanticTokenType::Struct:
        case SemanticTokenType::Interface:
        case SemanticTokenType::Enum:
        case SemanticTokenType::TypeParameter:
        case SemanticTokenType::Namespace:
            return palette.type;

        case SemanticTokenType::Function:
        case SemanticTokenType::Method:
        case SemanticTokenType::Macro:
            return palette.label;

        case SemanticTokenType::Parameter:
        case SemanticTokenType::Variable:
        case SemanticTokenType::Property:
        case SemanticTokenType::EnumMember:
        case SemanticTokenType::Event:
            return palette.text_primary;

        case SemanticTokenType::String:
        case SemanticTokenType::Regexp:
            return palette.success;

        case SemanticTokenType::Number:
            return palette.number;

        case SemanticTokenType::Comment:
            return palette.comment;

        case SemanticTokenType::Operator:
            return palette.accent;

        case SemanticTokenType::Custom:
        case SemanticTokenType::Count:
        default:
            return palette.text_primary;
    }
}

} // namespace Zenvra::Language::Syntax
