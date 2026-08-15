#pragma once

#include "Language/Syntax/SemanticTokenTypes.h"
#include "UI/Theme/StudioTheme.h"
#include "UI/Editor/StudioEditorModel.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace Zenvra::Language::Syntax
{

struct SemanticTokenSpan
{
    std::size_t line = 0;
    std::size_t start_column = 0;
    std::size_t length = 0;
    SemanticTokenType type = SemanticTokenType::Custom;
    uint32_t modifiers = 0;
};

class SemanticTokensManager
{
public:
    SemanticTokensManager() = default;
    ~SemanticTokensManager() = default;

    /// Decodes flat LSP 5-tuple integer array into structured SemanticTokenSpan objects.
    [[nodiscard]] static std::vector<SemanticTokenSpan> decode_lsp_tokens(
        std::span<const uint32_t> raw_data,
        std::span<const std::string> legend_types = {},
        std::span<const std::string> legend_modifiers = {}) noexcept;

    /// Updates stored semantic tokens for a given document URI.
    void update_document_tokens(const std::string& uri, std::vector<SemanticTokenSpan> tokens);

    /// Gets token spans for a specific line of a document.
    [[nodiscard]] std::vector<SemanticTokenSpan> get_tokens_for_line(
        const std::string& uri,
        std::size_t line) const;

    /// Checks if a document has cached semantic tokens.
    [[nodiscard]] bool has_tokens(const std::string& uri) const noexcept;

    /// Clears tokens for a document when closed.
    void clear_document_tokens(const std::string& uri) noexcept;

    /// Resolves theme color for a semantic token type.
    [[nodiscard]] static UI::Theme::Color get_token_color(
        SemanticTokenType type,
        const UI::Editor::StudioEditorPalette& palette) noexcept;

private:
    // uri -> line number -> list of spans on that line
    std::map<std::string, std::map<std::size_t, std::vector<SemanticTokenSpan>>> m_document_tokens;
};

} // namespace Zenvra::Language::Syntax
