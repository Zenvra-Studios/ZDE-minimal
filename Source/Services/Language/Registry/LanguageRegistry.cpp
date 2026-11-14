#include "Services/Language/Registry/LanguageRegistry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/param.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace Zenvra::Services::Language::Registry
{

namespace
{
std::string to_lower_str(std::string_view str)
{
    std::string result;
    result.reserve(str.size());
    for (char c : str)
    {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

std::filesystem::path get_executable_directory()
{
#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len > buffer.size() - 1)
    {
        buffer.resize(len + 1);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (len > 0)
    {
        return std::filesystem::path(buffer.data()).parent_path();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size > 0)
    {
        std::vector<char> buf(size);
        if (_NSGetExecutablePath(buf.data(), &size) == 0)
        {
            return std::filesystem::path(buf.data()).parent_path();
        }
    }
#elif defined(__linux__)
    char buf[PATH_MAX]{};
    ssize_t count = readlink("/proc/self/exe", buf, sizeof(buf));
    if (count > 0 && count < static_cast<ssize_t>(sizeof(buf)))
    {
        return std::filesystem::path(std::string(buf, count)).parent_path();
    }
#endif
    return std::filesystem::current_path();
}
} // namespace

LanguageRegistry& LanguageRegistry::instance() noexcept
{
    static auto* reg = new LanguageRegistry();
    return *reg;
}

LanguageRegistry::LanguageRegistry()
{
    initialize();
}

void LanguageRegistry::initialize(const std::filesystem::path& assets_dir)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (m_initialized) return;

    // Register safe embedded defaults first
    register_builtin_defaults();

    // Determine path to Assets/LanguageDefinitions
    std::filesystem::path defs_dir = assets_dir;
    if (defs_dir.empty())
    {
        std::error_code ec;
        const auto app_dir = get_executable_directory();
        const std::filesystem::path candidate_paths[] = {
            app_dir / "Assets" / "LanguageDefinitions",
            app_dir / "Resources" / "Assets" / "LanguageDefinitions",
            std::filesystem::current_path(ec) / "Assets" / "LanguageDefinitions",
            app_dir / ".." / "Assets" / "LanguageDefinitions",
            app_dir / ".." / ".." / "Assets" / "LanguageDefinitions",
            app_dir / ".." / ".." / ".." / "Assets" / "LanguageDefinitions"
        };

        for (const auto& path : candidate_paths)
        {
            if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec))
            {
                defs_dir = path;
                break;
            }
        }
    }

    if (!defs_dir.empty())
    {
        // Load and override from declarative JSON files
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(defs_dir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
            {
                std::ifstream stream(entry.path());
                if (stream)
                {
                    try
                    {
                        nlohmann::json j;
                        stream >> j;
                        if (auto def = Definitions::LanguageDefinition::from_json(j))
                        {
                            def->is_builtin = true;
                            // Map extensions
                            for (const auto& ext : def->extensions)
                            {
                                m_extension_to_id[to_lower_str(ext)] = def->id;
                            }
                            // Map filenames
                            for (const auto& fn : def->filenames)
                            {
                                m_filename_to_id[to_lower_str(fn)] = def->id;
                            }
                            m_definitions[def->id] = std::move(*def);
                        }
                    }
                    catch (...)
                    {
                        // Ignore malformed JSON file safely
                    }
                }
            }
        }
    }

    m_initialized = true;
}

bool LanguageRegistry::load_from_file(const std::filesystem::path& file_path)
{
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec) || !std::filesystem::is_regular_file(file_path, ec))
    {
        return false;
    }

    std::ifstream stream(file_path);
    if (!stream) return false;

    try
    {
        nlohmann::json j;
        stream >> j;
        if (auto def = Definitions::LanguageDefinition::from_json(j))
        {
            return register_language(std::move(*def));
        }
    }
    catch (...)
    {
        return false;
    }
    return false;
}

bool LanguageRegistry::register_language(Definitions::LanguageDefinition definition)
{
    std::vector<Definitions::LanguageDefinition> existing;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        existing.reserve(m_definitions.size());
        for (const auto& [_, def] : m_definitions)
        {
            existing.push_back(def);
        }
    }

    const auto issues = LanguageValidator::validate(definition, existing);
    for (const auto& issue : issues)
    {
        if (issue.severity == ValidationIssue::Severity::Error)
        {
            return false;
        }
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    for (const auto& ext : definition.extensions)
    {
        m_extension_to_id[to_lower_str(ext)] = definition.id;
    }
    for (const auto& fn : definition.filenames)
    {
        m_filename_to_id[to_lower_str(fn)] = definition.id;
    }
    m_definitions[definition.id] = std::move(definition);
    return true;
}

bool LanguageRegistry::unregister_language(std::string_view language_id)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_definitions.find(std::string(language_id));
    if (it == m_definitions.end()) return false;

    // Never allow unregistering protected built-in languages
    if (it->second.is_builtin) return false;

    for (const auto& ext : it->second.extensions)
    {
        const auto lower = to_lower_str(ext);
        if (auto eit = m_extension_to_id.find(lower); eit != m_extension_to_id.end() && eit->second == language_id)
        {
            m_extension_to_id.erase(eit);
        }
    }
    for (const auto& fn : it->second.filenames)
    {
        const auto lower = to_lower_str(fn);
        if (auto fit = m_filename_to_id.find(lower); fit != m_filename_to_id.end() && fit->second == language_id)
        {
            m_filename_to_id.erase(fit);
        }
    }
    m_definitions.erase(it);
    return true;
}

void LanguageRegistry::unregister_languages_for_plugin(std::string_view plugin_id)
{
    if (plugin_id.empty()) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    std::vector<std::string> to_remove;
    for (const auto& [id, def] : m_definitions)
    {
        if (!def.is_builtin && def.plugin_id == plugin_id)
        {
            to_remove.push_back(id);
        }
    }

    for (const auto& id : to_remove)
    {
        auto it = m_definitions.find(id);
        if (it != m_definitions.end())
        {
            for (const auto& ext : it->second.extensions)
            {
                const auto lower = to_lower_str(ext);
                if (auto eit = m_extension_to_id.find(lower); eit != m_extension_to_id.end() && eit->second == id)
                {
                    m_extension_to_id.erase(eit);
                }
            }
            for (const auto& fn : it->second.filenames)
            {
                const auto lower = to_lower_str(fn);
                if (auto fit = m_filename_to_id.find(lower); fit != m_filename_to_id.end() && fit->second == id)
                {
                    m_filename_to_id.erase(fit);
                }
            }
            m_definitions.erase(it);
        }
    }
}

const Definitions::LanguageDefinition* LanguageRegistry::find_by_id(std::string_view language_id) const noexcept
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_definitions.find(std::string(language_id));
    if (it != m_definitions.end()) return &it->second;

    const std::string lower = to_lower_str(language_id);
    for (const auto& [id, def] : m_definitions)
    {
        if (to_lower_str(id) == lower) return &def;
        for (const auto& alias : def.aliases)
        {
            if (to_lower_str(alias) == lower) return &def;
        }
    }
    return nullptr;
}

const Definitions::LanguageDefinition* LanguageRegistry::find_by_extension(std::string_view extension) const noexcept
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto lower = to_lower_str(extension);
    auto it = m_extension_to_id.find(lower);
    if (it != m_extension_to_id.end())
    {
        auto def_it = m_definitions.find(it->second);
        if (def_it != m_definitions.end()) return &def_it->second;
    }
    return nullptr;
}

const Definitions::LanguageDefinition* LanguageRegistry::find_by_filename(std::string_view filename) const noexcept
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto lower = to_lower_str(filename);
    auto it = m_filename_to_id.find(lower);
    if (it != m_filename_to_id.end())
    {
        auto def_it = m_definitions.find(it->second);
        if (def_it != m_definitions.end()) return &def_it->second;
    }
    return nullptr;
}

std::vector<const Definitions::LanguageDefinition*> LanguageRegistry::get_all_languages() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<const Definitions::LanguageDefinition*> result;
    result.reserve(m_definitions.size());
    for (const auto& [_, def] : m_definitions)
    {
        result.push_back(&def);
    }
    return result;
}

bool LanguageRegistry::has_language(std::string_view language_id) const noexcept
{
    return find_by_id(language_id) != nullptr;
}

Detection::DetectionResult LanguageRegistry::detect_language(
    const std::filesystem::path& file_path,
    std::string_view file_content,
    const std::filesystem::path& workspace_root) const
{
    std::vector<Definitions::LanguageDefinition> defs;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        defs.reserve(m_definitions.size());
        for (const auto& [_, def] : m_definitions)
        {
            defs.push_back(def);
        }
    }
    return m_detector.detect(file_path, file_content, workspace_root, defs);
}

Definitions::EditorSettings LanguageRegistry::get_editor_settings(
    std::string_view language_id_or_extension) const noexcept
{
    if (const auto* def = find_by_id(language_id_or_extension))
    {
        return def->editor_settings;
    }
    if (const auto* def = find_by_extension(language_id_or_extension))
    {
        return def->editor_settings;
    }
    return Definitions::EditorSettings{};
}

std::optional<Definitions::ServerAssociation> LanguageRegistry::get_server_association(
    std::string_view language_id) const noexcept
{
    if (const auto* def = find_by_id(language_id))
    {
        return def->server_association;
    }
    return std::nullopt;
}

std::size_t LanguageRegistry::load_definitions_from_directory(const std::filesystem::path& dir_path)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec) || !std::filesystem::is_directory(dir_path, ec))
    {
        return 0;
    }

    std::size_t loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
        {
            std::ifstream file(entry.path());
            if (file)
            {
                try
                {
                    nlohmann::json j;
                    file >> j;
                    if (auto def = Definitions::LanguageDefinition::from_json(j))
                    {
                        if (register_language(std::move(*def)))
                        {
                            ++loaded;
                        }
                    }
                }
                catch (...)
                {
                }
            }
        }
    }
    return loaded;
}

void LanguageRegistry::clear() noexcept
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_definitions.clear();
    m_extension_to_id.clear();
    m_filename_to_id.clear();
    m_initialized = false;
}

void LanguageRegistry::register_builtin_defaults()
{
    auto add_builtin = [this](Definitions::LanguageDefinition def) {
        def.is_builtin = true;
        for (const auto& ext : def.extensions)
        {
            m_extension_to_id[to_lower_str(ext)] = def.id;
        }
        for (const auto& fn : def.filenames)
        {
            m_filename_to_id[to_lower_str(fn)] = def.id;
        }
        m_definitions[def.id] = std::move(def);
    };

    // C++
    {
        Definitions::LanguageDefinition cpp;
        cpp.id = "cpp";
        cpp.display_name = "C++";
        cpp.extensions = {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".h", ".inl", ".ixx", ".cppm", ".cu", ".cuh", ".mm"};
        cpp.project_markers = {"CMakeLists.txt", "compile_commands.json", ".clangd"};
        cpp.aliases = {"c++", "cplusplus"};
        cpp.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        cpp.server_association = {
            .default_server_id = "clangd",
            .candidate_executables = {"clangd", "clangd-19", "clangd-18", "clangd-17", "clangd-16", "clangd-15"},
            .default_arguments = {"--background-index", "--clang-tidy", "--header-insertion=iwyu"}
        };
        add_builtin(std::move(cpp));
    }

    // C
    {
        Definitions::LanguageDefinition c;
        c.id = "c";
        c.display_name = "C";
        c.extensions = {".c", ".h", ".m"};
        c.project_markers = {"CMakeLists.txt", "Makefile", "compile_commands.json"};
        c.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        c.server_association = {
            .default_server_id = "clangd",
            .candidate_executables = {"clangd", "clangd-19", "clangd-18", "clangd-17", "clangd-16"},
            .default_arguments = {"--background-index"}
        };
        add_builtin(std::move(c));
    }

    // Rust
    {
        Definitions::LanguageDefinition rust;
        rust.id = "rust";
        rust.display_name = "Rust";
        rust.extensions = {".rs"};
        rust.filenames = {"Cargo.lock"};
        rust.project_markers = {"Cargo.toml", "Cargo.lock"};
        rust.aliases = {"rs"};
        rust.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        rust.server_association = {
            .default_server_id = "rust-analyzer",
            .candidate_executables = {"rust-analyzer", "rust-analyzer-linux", "rust-analyzer-mac"},
            .root_markers = {"Cargo.toml", ".git"}
        };
        add_builtin(std::move(rust));
    }

    // Python
    {
        Definitions::LanguageDefinition py;
        py.id = "python";
        py.display_name = "Python";
        py.extensions = {".py", ".pyw", ".pyi", ".pyd"};
        py.filenames = {"Pipfile", "pyproject.toml", "requirements.txt"};
        py.first_line_patterns = {"^#!.*\\bpython[0-9.]*\\b"};
        py.project_markers = {"pyproject.toml", "setup.py", "requirements.txt"};
        py.aliases = {"py"};
        py.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "#", .block_comment_start = "\"\"\"", .block_comment_end = "\"\"\""
        };
        py.server_association = {
            .default_server_id = "pyright",
            .candidate_executables = {"pyright-langserver", "pyright", "pylsp", "jedi-language-server"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(py));
    }

    // TypeScript
    {
        Definitions::LanguageDefinition ts;
        ts.id = "typescript";
        ts.display_name = "TypeScript";
        ts.extensions = {".ts", ".tsx", ".mts", ".cts"};
        ts.filenames = {"tsconfig.json"};
        ts.first_line_patterns = {"^#!.*\\b(?:deno|bun|node)\\b"};
        ts.project_markers = {"tsconfig.json", "package.json"};
        ts.aliases = {"ts"};
        ts.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        ts.server_association = {
            .default_server_id = "typescript-language-server",
            .candidate_executables = {"typescript-language-server", "tls", "vtsls", "deno", "bun"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(ts));
    }

    // JavaScript
    {
        Definitions::LanguageDefinition js;
        js.id = "javascript";
        js.display_name = "JavaScript";
        js.extensions = {".js", ".jsx", ".mjs", ".cjs"};
        js.filenames = {"jsconfig.json"};
        js.first_line_patterns = {"^#!.*\\b(?:node|deno|bun)\\b"};
        js.project_markers = {"package.json", "jsconfig.json"};
        js.aliases = {"js"};
        js.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        js.server_association = {
            .default_server_id = "typescript-language-server",
            .candidate_executables = {"typescript-language-server", "tls", "vtsls", "deno", "bun"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(js));
    }

    // Go
    {
        Definitions::LanguageDefinition go;
        go.id = "go";
        go.display_name = "Go";
        go.extensions = {".go"};
        go.filenames = {"go.mod", "go.sum", "go.work"};
        go.project_markers = {"go.mod", "go.work"};
        go.aliases = {"golang"};
        go.editor_settings = {
            .tab_size = 4, .insert_spaces = false,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        go.server_association = {
            .default_server_id = "gopls",
            .candidate_executables = {"gopls", "gopls-v0.23.0"}
        };
        add_builtin(std::move(go));
    }

    // Java
    {
        Definitions::LanguageDefinition java;
        java.id = "java";
        java.display_name = "Java";
        java.extensions = {".java", ".jav"};
        java.project_markers = {"pom.xml", "build.gradle", "build.gradle.kts"};
        java.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        java.server_association = {
            .default_server_id = "jdtls",
            .candidate_executables = {"jdtls", "jdtls.bat", "jdtls.cmd", "jdt-language-server", "eclipse-jdtls"}
        };
        add_builtin(std::move(java));
    }

    // CMake
    {
        Definitions::LanguageDefinition cmake;
        cmake.id = "cmake";
        cmake.display_name = "CMake";
        cmake.extensions = {".cmake"};
        cmake.filenames = {"CMakeLists.txt", "CMakePresets.json"};
        cmake.project_markers = {"CMakeLists.txt"};
        cmake.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "#", .block_comment_start = "#[[", .block_comment_end = "]]"
        };
        cmake.server_association = {
            .default_server_id = "cmakels",
            .candidate_executables = {"cmakels", "cmake-language-server", "neocmakelsp", "cmake-ls"}
        };
        add_builtin(std::move(cmake));
    }

    // HTML
    {
        Definitions::LanguageDefinition html;
        html.id = "html";
        html.display_name = "HTML";
        html.extensions = {".html", ".htm", ".xhtml"};
        html.first_line_patterns = {"^<!DOCTYPE\\s+html", "^<html"};
        html.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "<!--", .block_comment_start = "<!--", .block_comment_end = "-->"
        };
        html.server_association = {
            .default_server_id = "vscode-html-language-server",
            .candidate_executables = {"vscode-html-language-server", "html-languageserver", "html"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(html));
    }

    // CSS
    {
        Definitions::LanguageDefinition css;
        css.id = "css";
        css.display_name = "CSS";
        css.extensions = {".css", ".scss", ".less"};
        css.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "/*", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        css.server_association = {
            .default_server_id = "vscode-css-language-server",
            .candidate_executables = {"vscode-css-language-server", "css-languageserver"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(css));
    }

    // JSON
    {
        Definitions::LanguageDefinition json;
        json.id = "json";
        json.display_name = "JSON";
        json.extensions = {".json", ".jsonc"};
        json.filenames = {"package.json", "tsconfig.json", "composer.json"};
        json.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        json.server_association = {
            .default_server_id = "vscode-json-language-server",
            .candidate_executables = {"vscode-json-language-server", "json-languageserver"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(json));
    }

    // Shell
    {
        Definitions::LanguageDefinition sh;
        sh.id = "shell";
        sh.display_name = "Shell Script";
        sh.extensions = {".sh", ".bash", ".zsh", ".ksh"};
        sh.filenames = {".bashrc", ".zshrc", ".profile"};
        sh.first_line_patterns = {"^#!.*\\b(?:bash|sh|zsh|ksh)\\b"};
        sh.aliases = {"sh", "bash", "zsh"};
        sh.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "#", .block_comment_start = "", .block_comment_end = ""
        };
        sh.server_association = {
            .default_server_id = "bash-language-server",
            .candidate_executables = {"bash-language-server"},
            .default_arguments = {"start"}
        };
        add_builtin(std::move(sh));
    }

    // PHP
    {
        Definitions::LanguageDefinition php;
        php.id = "php";
        php.display_name = "PHP";
        php.extensions = {".php", ".phtml", ".php4", ".php5", ".phps"};
        php.filenames = {"composer.json", "artisan"};
        php.first_line_patterns = {"^#!.*\\bphp\\b", "^<\\?php"};
        php.project_markers = {"composer.json"};
        php.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/"
        };
        php.server_association = {
            .default_server_id = "intelephense",
            .candidate_executables = {"intelephense", "phpactor", "php-language-server"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(php));
    }

    // YAML
    {
        Definitions::LanguageDefinition yaml;
        yaml.id = "yaml";
        yaml.display_name = "YAML";
        yaml.extensions = {".yaml", ".yml"};
        yaml.filenames = {".clang-format", ".clang-tidy"};
        yaml.first_line_patterns = {"^%YAML", "^---"};
        yaml.aliases = {"yml"};
        yaml.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "#", .block_comment_start = "", .block_comment_end = ""
        };
        yaml.server_association = {
            .default_server_id = "yaml-language-server",
            .candidate_executables = {"yaml-language-server"},
            .default_arguments = {"--stdio"}
        };
        add_builtin(std::move(yaml));
    }

    // Lua
    {
        Definitions::LanguageDefinition lua;
        lua.id = "lua";
        lua.display_name = "Lua";
        lua.extensions = {".lua"};
        lua.first_line_patterns = {"^#!.*\\blua\\b"};
        lua.editor_settings = {
            .tab_size = 2, .insert_spaces = true,
            .line_comment = "--", .block_comment_start = "--[[", .block_comment_end = "]]"
        };
        lua.server_association = {
            .default_server_id = "lua-language-server",
            .candidate_executables = {"lua-language-server"}
        };
        add_builtin(std::move(lua));
    }

    // Zig
    {
        Definitions::LanguageDefinition zig;
        zig.id = "zig";
        zig.display_name = "Zig";
        zig.extensions = {".zig", ".zon"};
        zig.filenames = {"build.zig", "build.zig.zon"};
        zig.project_markers = {"build.zig", "build.zig.zon"};
        zig.editor_settings = {
            .tab_size = 4, .insert_spaces = true,
            .line_comment = "//", .block_comment_start = "", .block_comment_end = ""
        };
        zig.server_association = {
            .default_server_id = "zls",
            .candidate_executables = {"zls"}
        };
        add_builtin(std::move(zig));
    }
}

} // namespace Zenvra::Services::Language::Registry

