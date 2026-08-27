#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Zenvra::Language
{

struct LanguageConfiguration
{
    std::size_t tab_size = 4;
    bool insert_spaces = true;
    bool auto_close_braces = true;
    bool auto_close_brackets = true;
    bool auto_close_parentheses = true;
    bool auto_close_quotes = true;
    bool auto_continue_comments = true;
    std::string line_comment = "//";
    std::string block_comment_start = "/*";
    std::string block_comment_end = "*/";

    static LanguageConfiguration get_for_extension(std::string_view extension)
    {
        LanguageConfiguration config;
        config.tab_size = 4; // Default to 4
        config.auto_close_braces = true;
        config.auto_close_brackets = true;
        config.auto_close_parentheses = true;
        config.auto_close_quotes = true;
        config.auto_continue_comments = true;

        std::string ext(extension);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        // C / C++ / Objective-C / CUDA family
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
            ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh" ||
            ext == ".inl" || ext == ".ixx" || ext == ".cppm" || ext == ".cu" ||
            ext == ".cuh" || ext == ".mm" || ext == ".m")
        {
            config.line_comment = "//";
            config.block_comment_start = "/*";
            config.block_comment_end = "*/";
        }
        // Rust / Go / Java / C# / Kotlin / Swift / Dart / Zig / Scala / PHP
        else if (ext == ".rs" || ext == ".go" || ext == ".java" || ext == ".cs" ||
                 ext == ".kt" || ext == ".kts" || ext == ".swift" || ext == ".dart" ||
                 ext == ".zig" || ext == ".scala" || ext == ".php")
        {
            config.line_comment = "//";
            config.block_comment_start = "/*";
            config.block_comment_end = "*/";
        }
        // JavaScript / TypeScript family
        else if (ext == ".js" || ext == ".jsx" || ext == ".ts" || ext == ".tsx" ||
                 ext == ".mjs" || ext == ".cjs")
        {
            config.tab_size = 2;
            config.line_comment = "//";
            config.block_comment_start = "/*";
            config.block_comment_end = "*/";
        }
        // Python / Shell / CMake / YAML / Ruby / TOML / R / Perl / Dockerfile
        else if (ext == ".py" || ext == ".pyw" || ext == ".pyi" ||
                 ext == ".sh" || ext == ".bash" || ext == ".zsh" ||
                 ext == ".cmake" || ext == ".rb" || ext == ".yml" || ext == ".yaml" ||
                 ext == ".toml" || ext == ".r" || ext == ".pl" || ext == ".pm" ||
                 ext == ".ps1" || ext == ".dockerfile")
        {
            config.line_comment = "#";
            config.block_comment_start = "";
            config.block_comment_end = "";
        }
        // Lua / SQL / Haskell / Ada / VHDL
        else if (ext == ".lua" || ext == ".sql" || ext == ".hs" || ext == ".ada" ||
                 ext == ".vhd" || ext == ".vhdl")
        {
            config.line_comment = "--";
            config.block_comment_start = "/*";
            config.block_comment_end = "*/";
        }
        // HTML / XML / SVG / Vue / Svelte
        else if (ext == ".html" || ext == ".htm" || ext == ".xhtml" ||
                 ext == ".xml" || ext == ".svg" || ext == ".vue" || ext == ".svelte")
        {
            config.tab_size = 2;
            config.line_comment = "<!--";
            config.block_comment_start = "<!--";
            config.block_comment_end = "-->";
        }
        // CSS / SCSS / LESS
        else if (ext == ".css" || ext == ".scss" || ext == ".less")
        {
            config.tab_size = 2;
            config.line_comment = "/*";
            config.block_comment_start = "/*";
            config.block_comment_end = "*/";
        }
        // JSON / JSONC
        else if (ext == ".json" || ext == ".jsonc")
        {
            config.tab_size = 2;
            config.line_comment = "//";
        }
        // Assembly
        else if (ext == ".asm" || ext == ".s" || ext == ".S" ||
                 ext == ".nasm" || ext == ".inc" || ext == ".a51")
        {
            config.auto_close_braces = false;
            config.auto_close_brackets = false;
            config.line_comment = ";";
            config.block_comment_start = "";
            config.block_comment_end = "";
        }
        // Plaintext / Markdown
        else if (ext == ".txt" || ext == ".md" || ext == ".markdown")
        {
            config.auto_close_braces = false;
            config.line_comment = "";
        }

        return config;
    }
};

} // namespace Zenvra::Language

