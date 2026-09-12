#pragma once

#include "Language/Protocol/LspTypes.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Zenvra::Language::Node
{

struct NodePackageInfo
{
    std::string name;
    std::string version;
    std::string description;
    std::string types_path;
    bool is_dev_dependency = false;
    bool is_installed = false;
};

struct ProjectManifest
{
    std::filesystem::path root_path;
    std::string project_name;
    std::vector<NodePackageInfo> packages;
    std::filesystem::path local_tsserver_path;
    std::chrono::steady_clock::time_point last_scanned;
};

class NodePackageManager
{
public:
    static NodePackageManager& instance() noexcept;

    /// Finds the project root containing package.json or node_modules for a given path or workspace.
    [[nodiscard]] std::filesystem::path find_project_root(const std::filesystem::path& start_path) const;

    /// Scans package.json and node_modules in the workspace root.
    ProjectManifest scan_workspace(const std::filesystem::path& workspace_root);

    /// Returns list of detected package names (from package.json and node_modules).
    std::vector<std::string> get_detected_packages(const std::filesystem::path& workspace_root = {});

    /// Returns path to local tsserver (node_modules/typescript/lib/tsserverlibrary.js or tsserver.js) if available.
    std::filesystem::path find_local_tsserver(const std::filesystem::path& workspace_root = {});

    /// Checks if a line and column represents an import / require module specifier context.
    /// (e.g. `import ... from '...'`, `require('...')`, `import('...')`, `export ... from '...'`)
    static bool is_import_specifier_context(std::string_view line_text, std::size_t col, std::string& out_partial);

    /// Returns package completion items for import / require statements.
    std::vector<Protocol::CompletionItem> get_import_completions(
        std::string_view line_text,
        std::size_t col,
        const std::filesystem::path& workspace_root = {});

    /// Returns import statement completions (e.g. `from 'package'`, package named exports, or default import snippets)
    /// when the user is writing an import line.
    std::vector<Protocol::CompletionItem> get_import_statement_completions(
        std::string_view line_text,
        std::size_t col,
        const std::filesystem::path& workspace_root = {});

    /// Returns library completions (methods, classes, objects, hooks, types)
    /// for detected libraries and frameworks in the workspace.
    std::vector<Protocol::CompletionItem> get_library_completions(
        const std::filesystem::path& workspace_root = {},
        bool is_scoped = false,
        std::string_view scoped_prefix = {});

    /// Parses a .d.ts TypeScript declaration file to extract exported functions, classes, methods, and types.
    static std::vector<Protocol::CompletionItem> parse_declaration_file(
        const std::filesystem::path& dts_path,
        const std::string& package_name);

    /// Clears all caches.
    void clear_cache() noexcept;

private:
    NodePackageManager() = default;
    ~NodePackageManager() = default;

    /// Built-in catalog definitions for popular frameworks and libraries.
    std::vector<Protocol::CompletionItem> get_catalog_completions_for_package(
        const std::string& package_name,
        bool is_scoped,
        std::string_view scoped_prefix) const;

    std::mutex m_mutex;
    std::unordered_map<std::string, ProjectManifest> m_manifest_cache; // root_path -> manifest
    std::unordered_map<std::string, std::vector<Protocol::CompletionItem>> m_dts_symbol_cache; // dts_path -> symbols
};

} // namespace Zenvra::Language::Node
