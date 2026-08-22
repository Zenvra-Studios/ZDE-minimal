#pragma once

#include "Language/Protocol/LspTypes.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Language::Definition
{

struct DocumentContext
{
    std::string uri;
    std::string filename;
    std::vector<std::string> lines;
};

class SymbolDefinitionResolver
{
public:
    [[nodiscard]] static SymbolDefinitionResolver& instance();

    /// Resolves the definition for a symbol at the given position in a document.
    /// Supports C++ standard library (std::string, vector, cout, etc.), external headers,
    /// and multi-language workspace symbol search (C++, Python, Rust, TS/JS, Go, Zig, etc.).
    [[nodiscard]] std::vector<Protocol::Location> resolve_definition(
        std::string_view uri,
        std::string_view filename,
        const Protocol::Position& pos,
        std::string_view line_text,
        const std::filesystem::path& workspace_root,
        const std::vector<DocumentContext>& open_documents = {});

    /// Extracts the full identifier or include path at the specified column.
    [[nodiscard]] static std::string extract_symbol_at(std::string_view line_text, std::size_t col);

    /// Extracts the word boundary [start_col, end_col) at the specified column.
    [[nodiscard]] static std::pair<std::size_t, std::size_t> extract_symbol_range(std::string_view line_text, std::size_t col);

private:
    SymbolDefinitionResolver() = default;

    std::vector<Protocol::Location> resolve_include_or_import(
        std::string_view line_text,
        const std::filesystem::path& current_file_path,
        const std::filesystem::path& workspace_root);

    std::vector<Protocol::Location> resolve_cpp_standard_symbol(
        std::string_view symbol,
        const std::filesystem::path& workspace_root);

    std::vector<Protocol::Location> resolve_workspace_symbol(
        std::string_view symbol,
        std::string_view filename,
        const std::filesystem::path& workspace_root,
        const std::vector<DocumentContext>& open_documents);

    std::optional<Protocol::Location> find_symbol_in_file(
        const std::filesystem::path& file_path,
        std::string_view symbol);

    std::optional<std::filesystem::path> find_header_in_system_paths(
        std::string_view header_name,
        const std::filesystem::path& workspace_root);

    std::vector<std::filesystem::path> get_all_include_search_paths(
        const std::filesystem::path& workspace_root);
};

} // namespace Zenvra::Language::Definition
