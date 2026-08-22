#include "Language/Definition/SymbolDefinitionResolver.h"
#include "Language/Protocol/LspProtocolSerializer.h"
#include "Language/Toolchain/ToolchainDetector.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace Zenvra::Language::Definition
{

SymbolDefinitionResolver& SymbolDefinitionResolver::instance()
{
    static SymbolDefinitionResolver s_instance;
    return s_instance;
}

std::string SymbolDefinitionResolver::extract_symbol_at(std::string_view line_text, std::size_t col)
{
    if (line_text.empty())
    {
        return {};
    }

    // First check if inside an include directive: #include <header> or #include "header"
    if (line_text.find("#include") != std::string_view::npos)
    {
        const auto first_delim = line_text.find_first_of("<\"");
        if (first_delim != std::string_view::npos)
        {
            const char open_ch = line_text[first_delim];
            const char close_ch = (open_ch == '<') ? '>' : '"';
            const auto end_pos = line_text.find(close_ch, first_delim + 1);
            if (end_pos != std::string_view::npos && col >= first_delim && col <= end_pos)
            {
                return std::string(line_text.substr(first_delim + 1, end_pos - first_delim - 1));
            }
        }
    }

    if (col >= line_text.size())
    {
        col = line_text.size() - 1;
    }

    auto is_sym_char = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == ':';
    };

    if (!is_sym_char(line_text[col]))
    {
        if (col > 0 && is_sym_char(line_text[col - 1]))
        {
            --col;
        }
        else
        {
            return {};
        }
    }

    std::size_t start = col;
    while (start > 0 && is_sym_char(line_text[start - 1]))
    {
        --start;
    }

    std::size_t end = col;
    while (end < line_text.size() && is_sym_char(line_text[end]))
    {
        ++end;
    }

    // Clean leading/trailing colons
    while (start < end && line_text[start] == ':')
    {
        ++start;
    }
    while (end > start && line_text[end - 1] == ':')
    {
        --end;
    }

    return std::string(line_text.substr(start, end - start));
}

std::pair<std::size_t, std::size_t> SymbolDefinitionResolver::extract_symbol_range(std::string_view line_text, std::size_t col)
{
    if (line_text.empty())
    {
        return {0, 0};
    }
    if (col >= line_text.size())
    {
        col = line_text.size() - 1;
    }

    // 1. Check if inside an include directive: #include <algorithm> or #include "header.h"
    if (line_text.find("#include") != std::string_view::npos)
    {
        const auto first_delim = line_text.find_first_of("<\"");
        if (first_delim != std::string_view::npos)
        {
            const char open_ch = line_text[first_delim];
            const char close_ch = (open_ch == '<') ? '>' : '"';
            const auto end_pos = line_text.find(close_ch, first_delim + 1);
            if (end_pos != std::string_view::npos)
            {
                if (col >= first_delim && col <= end_pos)
                {
                    return {first_delim + 1, end_pos};
                }
            }
        }
    }

    // 2. Check if inside a string import/include: e.g. import "fmt" or from "..." or import '...'
    if (line_text.find("import") != std::string_view::npos ||
        line_text.find("require") != std::string_view::npos ||
        line_text.find("from") != std::string_view::npos)
    {
        for (const char quote : {'"', '\''})
        {
            const auto first_q = line_text.find(quote);
            if (first_q != std::string_view::npos)
            {
                const auto second_q = line_text.find(quote, first_q + 1);
                if (second_q != std::string_view::npos && col >= first_q && col <= second_q)
                {
                    return {first_q + 1, second_q};
                }
            }
        }
    }

    auto is_ident_char = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    };

    if (!is_ident_char(line_text[col]))
    {
        if (col > 0 && is_ident_char(line_text[col - 1]))
        {
            --col;
        }
        else if (col + 1 < line_text.size() && is_ident_char(line_text[col + 1]))
        {
            ++col;
        }
        else
        {
            return {0, 0};
        }
    }

    std::size_t start = col;
    while (start > 0 && is_ident_char(line_text[start - 1]))
    {
        --start;
    }

    std::size_t end = col;
    while (end < line_text.size() && is_ident_char(line_text[end]))
    {
        ++end;
    }

    return {start, end};
}

std::vector<Protocol::Location> SymbolDefinitionResolver::resolve_definition(
    std::string_view uri,
    std::string_view filename,
    const Protocol::Position& pos,
    std::string_view line_text,
    const std::filesystem::path& workspace_root,
    const std::vector<DocumentContext>& open_documents)
{
    std::string symbol = extract_symbol_at(line_text, pos.character);
    if (symbol.empty())
    {
        return {};
    }

    std::filesystem::path current_file_path;
    if (!filename.empty())
    {
        current_file_path = std::filesystem::path(filename);
    }
    else if (!uri.empty())
    {
        current_file_path = Protocol::LspProtocolSerializer::uri_to_path(uri);
    }

    // 1. Direct Include / Import jump
    if (line_text.find("#include") != std::string_view::npos ||
        line_text.find("import ") != std::string_view::npos ||
        line_text.find("from ") != std::string_view::npos)
    {
        auto inc_locs = resolve_include_or_import(line_text, current_file_path, workspace_root);
        if (!inc_locs.empty())
        {
            return inc_locs;
        }
    }

    // 2. Standard C++ Library Symbols (std::string, basic_string, vector, map, cout, etc.)
    auto cpp_std_locs = resolve_cpp_standard_symbol(symbol, workspace_root);
    if (!cpp_std_locs.empty())
    {
        return cpp_std_locs;
    }

    // 3. Workspace Symbol Search (classes, functions, structs, enums, variables across all languages)
    return resolve_workspace_symbol(symbol, filename, workspace_root, open_documents);
}

std::vector<Protocol::Location> SymbolDefinitionResolver::resolve_include_or_import(
    std::string_view line_text,
    const std::filesystem::path& current_file_path,
    const std::filesystem::path& workspace_root)
{
    std::vector<Protocol::Location> results;
    std::string target_name;

    const auto first_delim = line_text.find_first_of("<\"'");
    if (first_delim != std::string_view::npos)
    {
        const char open_ch = line_text[first_delim];
        const char close_ch = (open_ch == '<') ? '>' : ((open_ch == '"') ? '"' : '\'');
        const auto end_pos = line_text.find(close_ch, first_delim + 1);
        if (end_pos != std::string_view::npos)
        {
            target_name = std::string(line_text.substr(first_delim + 1, end_pos - first_delim - 1));
        }
    }

    if (target_name.empty())
    {
        return results;
    }

    std::error_code ec;

    // Check relative to current file
    if (!current_file_path.empty())
    {
        auto rel_p = current_file_path.parent_path() / target_name;
        if (std::filesystem::exists(rel_p, ec) && std::filesystem::is_regular_file(rel_p, ec))
        {
            Protocol::Location loc;
            loc.uri = Protocol::LspProtocolSerializer::path_to_uri(std::filesystem::canonical(rel_p, ec));
            loc.range.start = {0, 0};
            loc.range.end = {0, 0};
            results.push_back(std::move(loc));
            return results;
        }
    }

    // Check in system include paths and workspace search roots
    auto found_p = find_header_in_system_paths(target_name, workspace_root);
    if (found_p && std::filesystem::exists(*found_p, ec))
    {
        Protocol::Location loc;
        loc.uri = Protocol::LspProtocolSerializer::path_to_uri(std::filesystem::canonical(*found_p, ec));
        loc.range.start = {0, 0};
        loc.range.end = {0, 0};
        results.push_back(std::move(loc));
        return results;
    }

    return results;
}

std::vector<Protocol::Location> SymbolDefinitionResolver::resolve_cpp_standard_symbol(
    std::string_view symbol,
    const std::filesystem::path& workspace_root)
{
    std::vector<Protocol::Location> results;

    std::string clean_sym(symbol);
    if (clean_sym.starts_with("std::"))
    {
        clean_sym = clean_sym.substr(5);
    }
    if (clean_sym.starts_with("filesystem::"))
    {
        clean_sym = clean_sym.substr(12);
    }
    if (clean_sym.starts_with("chrono::"))
    {
        clean_sym = clean_sym.substr(8);
    }

    // Standard C++ symbol -> standard header name mapping
    static const std::unordered_map<std::string, std::string> s_std_headers = {
        // String
        {"string", "string"}, {"basic_string", "string"}, {"wstring", "string"},
        {"u8string", "string"}, {"u16string", "string"}, {"u32string", "string"},
        {"to_string", "string"}, {"stoi", "string"}, {"stol", "string"}, {"stoll", "string"},
        {"stoul", "string"}, {"stoull", "string"}, {"stof", "string"}, {"stod", "string"}, {"stold", "string"},
        {"getline", "string"},
        // String View
        {"string_view", "string_view"}, {"basic_string_view", "string_view"},
        // Containers
        {"vector", "vector"}, {"map", "map"}, {"multimap", "map"},
        {"set", "set"}, {"multiset", "set"},
        {"unordered_map", "unordered_map"}, {"unordered_multimap", "unordered_map"},
        {"unordered_set", "unordered_set"}, {"unordered_multiset", "unordered_set"},
        {"deque", "deque"}, {"list", "list"}, {"forward_list", "forward_list"},
        {"array", "array"}, {"span", "span"},
        {"queue", "queue"}, {"priority_queue", "queue"}, {"stack", "stack"},
        // IO
        {"cout", "iostream"}, {"cin", "iostream"}, {"cerr", "iostream"}, {"clog", "iostream"},
        {"wcout", "iostream"}, {"wcin", "iostream"}, {"wcerr", "iostream"}, {"wclog", "iostream"},
        {"endl", "ostream"}, {"ostream", "ostream"}, {"istream", "istream"},
        {"iostream", "iostream"}, {"ios", "ios"}, {"streambuf", "streambuf"},
        {"ifstream", "fstream"}, {"ofstream", "fstream"}, {"fstream", "fstream"},
        {"istringstream", "sstream"}, {"ostringstream", "sstream"}, {"stringstream", "sstream"},
        {"printf", "cstdio"}, {"fprintf", "cstdio"}, {"sprintf", "cstdio"}, {"snprintf", "cstdio"},
        {"scanf", "cstdio"}, {"sscanf", "cstdio"}, {"fopen", "cstdio"}, {"fclose", "cstdio"},
        {"puts", "cstdio"}, {"getchar", "cstdio"}, {"putchar", "cstdio"},
        // Memory & Utility
        {"unique_ptr", "memory"}, {"shared_ptr", "memory"}, {"weak_ptr", "memory"},
        {"make_unique", "memory"}, {"make_shared", "memory"}, {"allocator", "memory"},
        {"pair", "utility"}, {"make_pair", "utility"}, {"move", "utility"}, {"forward", "utility"},
        {"swap", "utility"}, {"declval", "utility"}, {"in_place", "utility"},
        {"tuple", "tuple"}, {"make_tuple", "tuple"}, {"tie", "tuple"}, {"get", "tuple"},
        {"optional", "optional"}, {"nullopt", "optional"},
        {"variant", "variant"}, {"monostate", "variant"}, {"holds_alternative", "variant"},
        {"any", "any"}, {"any_cast", "any"},
        // Algorithms & Math
        {"sort", "algorithm"}, {"stable_sort", "algorithm"}, {"find", "algorithm"}, {"find_if", "algorithm"},
        {"copy", "algorithm"}, {"copy_if", "algorithm"}, {"transform", "algorithm"},
        {"count", "algorithm"}, {"count_if", "algorithm"}, {"min", "algorithm"}, {"max", "algorithm"},
        {"clamp", "algorithm"}, {"min_element", "algorithm"}, {"max_element", "algorithm"},
        {"accumulate", "numeric"}, {"iota", "numeric"}, {"reduce", "numeric"},
        {"sqrt", "cmath"}, {"pow", "cmath"}, {"sin", "cmath"}, {"cos", "cmath"}, {"tan", "cmath"},
        {"abs", "cmath"}, {"floor", "cmath"}, {"ceil", "cmath"}, {"round", "cmath"},
        // Concurrency
        {"thread", "thread"}, {"jthread", "thread"},
        {"mutex", "mutex"}, {"recursive_mutex", "mutex"}, {"lock_guard", "mutex"},
        {"unique_lock", "mutex"}, {"scoped_lock", "mutex"},
        {"atomic", "atomic"}, {"atomic_bool", "atomic"}, {"atomic_int", "atomic"},
        {"condition_variable", "condition_variable"}, {"condition_variable_any", "condition_variable"},
        // Functional & Chrono & Filesystem
        {"function", "functional"}, {"bind", "functional"}, {"hash", "functional"},
        {"path", "filesystem"}, {"exists", "filesystem"}, {"is_directory", "filesystem"},
        {"is_regular_file", "filesystem"}, {"directory_iterator", "filesystem"},
        {"current_path", "filesystem"}, {"file_size", "filesystem"}, {"canonical", "filesystem"},
        {"steady_clock", "chrono"}, {"system_clock", "chrono"}, {"high_resolution_clock", "chrono"},
        {"duration", "chrono"}, {"time_point", "chrono"}, {"seconds", "chrono"},
        {"milliseconds", "chrono"}, {"microseconds", "chrono"}, {"nanoseconds", "chrono"},
        // Types & Exceptions
        {"size_t", "cstddef"}, {"ptrdiff_t", "cstddef"}, {"byte", "cstddef"},
        {"int8_t", "cstdint"}, {"int16_t", "cstdint"}, {"int32_t", "cstdint"}, {"int64_t", "cstdint"},
        {"uint8_t", "cstdint"}, {"uint16_t", "cstdint"}, {"uint32_t", "cstdint"}, {"uint64_t", "cstdint"},
        {"exception", "exception"}, {"runtime_error", "stdexcept"}, {"logic_error", "stdexcept"},
        {"invalid_argument", "stdexcept"}, {"out_of_range", "stdexcept"},
        {"numeric_limits", "limits"},
        {"enable_if", "type_traits"}, {"is_same", "type_traits"}, {"decay", "type_traits"}
    };

    auto it = s_std_headers.find(clean_sym);
    if (it == s_std_headers.end())
    {
        return results;
    }

    const std::string& header_name = it->second;
    auto header_path = find_header_in_system_paths(header_name, workspace_root);
    if (!header_path)
    {
        return results;
    }

    // Search inside the header file for the definition
    auto loc = find_symbol_in_file(*header_path, clean_sym);
    if (loc)
    {
        results.push_back(std::move(*loc));
    }
    else
    {
        // Default to beginning of the header file if precise definition not parsed
        Protocol::Location def_loc;
        def_loc.uri = Protocol::LspProtocolSerializer::path_to_uri(*header_path);
        def_loc.range.start = {0, 0};
        def_loc.range.end = {0, 0};
        results.push_back(std::move(def_loc));
    }

    return results;
}

std::vector<Protocol::Location> SymbolDefinitionResolver::resolve_workspace_symbol(
    std::string_view symbol,
    std::string_view filename,
    const std::filesystem::path& workspace_root,
    const std::vector<DocumentContext>& open_documents)
{
    std::vector<Protocol::Location> results;
    std::string clean_sym(symbol);

    // Strip namespace qualifiers for broad matching (e.g. MyNamespace::MyClass -> MyClass)
    const auto last_colon = clean_sym.rfind("::");
    if (last_colon != std::string_view::npos)
    {
        clean_sym = clean_sym.substr(last_colon + 2);
    }
    const auto last_dot = clean_sym.rfind('.');
    if (last_dot != std::string_view::npos)
    {
        clean_sym = clean_sym.substr(last_dot + 1);
    }

    if (clean_sym.empty())
    {
        return results;
    }

    // 1. Search in open documents first (in-memory, immediate)
    for (const auto& doc : open_documents)
    {
        for (std::size_t line_idx = 0; line_idx < doc.lines.size(); ++line_idx)
        {
            const std::string& line = doc.lines[line_idx];
            const auto sym_pos = line.find(clean_sym);
            if (sym_pos != std::string::npos)
            {
                // Check if line looks like a definition
                bool is_definition = false;
                std::string_view lview(line);

                if (lview.find("class " + clean_sym) != std::string_view::npos ||
                    lview.find("struct " + clean_sym) != std::string_view::npos ||
                    lview.find("interface " + clean_sym) != std::string_view::npos ||
                    lview.find("enum " + clean_sym) != std::string_view::npos ||
                    lview.find("enum class " + clean_sym) != std::string_view::npos ||
                    lview.find("def " + clean_sym) != std::string_view::npos ||
                    lview.find("fn " + clean_sym) != std::string_view::npos ||
                    lview.find("func " + clean_sym) != std::string_view::npos ||
                    lview.find("function " + clean_sym) != std::string_view::npos ||
                    lview.find("type " + clean_sym) != std::string_view::npos ||
                    lview.find("using " + clean_sym) != std::string_view::npos ||
                    lview.find("#define " + clean_sym) != std::string_view::npos)
                {
                    is_definition = true;
                }
                else if (lview.find(clean_sym + "(") != std::string_view::npos)
                {
                    // Function signature
                    is_definition = true;
                }

                if (is_definition)
                {
                    Protocol::Location loc;
                    loc.uri = doc.uri.empty() ? Protocol::LspProtocolSerializer::path_to_uri(doc.filename) : doc.uri;
                    loc.range.start = {line_idx, sym_pos};
                    loc.range.end = {line_idx, sym_pos + clean_sym.size()};
                    results.push_back(std::move(loc));
                    return results;
                }
            }
        }
    }

    // 2. Search workspace files on disk
    std::vector<std::filesystem::path> search_dirs;
    if (!workspace_root.empty())
    {
        search_dirs.push_back(workspace_root);
    }
    if (!filename.empty())
    {
        std::filesystem::path fp(filename);
        if (fp.has_parent_path())
        {
            search_dirs.push_back(fp.parent_path());
        }
    }

    std::error_code ec;
    std::size_t files_scanned = 0;
    static constexpr std::size_t max_files_to_scan = 150;

    for (const auto& root_dir : search_dirs)
    {
        if (!std::filesystem::exists(root_dir, ec) || !std::filesystem::is_directory(root_dir, ec))
        {
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 root_dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (files_scanned++ >= max_files_to_scan)
            {
                break;
            }

            if (!entry.is_regular_file(ec))
            {
                continue;
            }

            const auto p = entry.path();
            const std::string ext = p.extension().string();

            // Only scan supported language files
            if (ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".c" &&
                ext != ".cc" && ext != ".cxx" && ext != ".rs" && ext != ".py" &&
                ext != ".go" && ext != ".ts" && ext != ".tsx" && ext != ".js" &&
                ext != ".jsx" && ext != ".zig" && ext != ".lua" && ext != ".cs" &&
                ext != ".java" && ext != ".asm" && ext != ".s" && ext != ".inc")
            {
                continue;
            }

            // Skip build / node_modules / git dirs
            const std::string p_str = p.generic_string();
            if (p_str.find("/build/") != std::string::npos ||
                p_str.find("/.git/") != std::string::npos ||
                p_str.find("/node_modules/") != std::string::npos ||
                p_str.find("/.cache/") != std::string::npos)
            {
                continue;
            }

            auto loc = find_symbol_in_file(p, clean_sym);
            if (loc)
            {
                results.push_back(std::move(*loc));
                return results;
            }
        }
    }

    return results;
}

std::optional<Protocol::Location> SymbolDefinitionResolver::find_symbol_in_file(
    const std::filesystem::path& file_path,
    std::string_view symbol)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    std::string line;
    std::size_t line_index = 0;

    std::optional<Protocol::Location> fallback_loc;

    while (std::getline(file, line))
    {
        if (line.size() > 2048)
        {
            // Skip binary/minified lines
            ++line_index;
            continue;
        }

        const auto pos = line.find(symbol);
        if (pos != std::string::npos)
        {
            // Ensure word boundary
            const bool left_bound = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(line[pos - 1])) && line[pos - 1] != '_');
            const bool right_bound = (pos + symbol.size() >= line.size()) || (!std::isalnum(static_cast<unsigned char>(line[pos + symbol.size()])) && line[pos + symbol.size()] != '_');

            if (left_bound && right_bound)
            {
                std::string_view lview(line);

                // Priority 1: Direct definition signatures
                if (lview.find("class " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("struct " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("interface " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("enum " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("def " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("fn " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("func " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("function " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("type " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("using " + std::string(symbol)) != std::string_view::npos ||
                    lview.find("typedef ") != std::string_view::npos ||
                    lview.find("#define " + std::string(symbol)) != std::string_view::npos)
                {
                    Protocol::Location loc;
                    loc.uri = Protocol::LspProtocolSerializer::path_to_uri(file_path);
                    loc.range.start = {line_index, pos};
                    loc.range.end = {line_index, pos + symbol.size()};
                    return loc;
                }

                // Priority 2: Function header or definition line
                if (lview.find(std::string(symbol) + "(") != std::string_view::npos ||
                    lview.find(std::string(symbol) + " (") != std::string_view::npos)
                {
                    Protocol::Location loc;
                    loc.uri = Protocol::LspProtocolSerializer::path_to_uri(file_path);
                    loc.range.start = {line_index, pos};
                    loc.range.end = {line_index, pos + symbol.size()};
                    return loc;
                }

                if (!fallback_loc)
                {
                    Protocol::Location loc;
                    loc.uri = Protocol::LspProtocolSerializer::path_to_uri(file_path);
                    loc.range.start = {line_index, pos};
                    loc.range.end = {line_index, pos + symbol.size()};
                    fallback_loc = loc;
                }
            }
        }
        ++line_index;
    }

    return fallback_loc;
}

std::vector<std::filesystem::path> SymbolDefinitionResolver::get_all_include_search_paths(
    const std::filesystem::path& workspace_root)
{
    std::vector<std::filesystem::path> paths;
    std::error_code ec;

    // 1. Toolchain system include paths
    const auto& toolchain = Toolchain::ToolchainDetector::instance().get_active_toolchain();
    for (const auto& p : toolchain.system_include_paths)
    {
        if (!p.empty() && std::filesystem::exists(p, ec))
        {
            paths.push_back(p);
        }
    }
    if (!toolchain.sdk_include_path.empty() && std::filesystem::exists(toolchain.sdk_include_path, ec))
    {
        paths.push_back(toolchain.sdk_include_path);
    }

    // 2. Workspace subdirectories
    if (!workspace_root.empty() && std::filesystem::exists(workspace_root, ec))
    {
        paths.push_back(workspace_root);
        const std::filesystem::path candidate_subs[] = {
            workspace_root / "Source",
            workspace_root / "src",
            workspace_root / "include",
            workspace_root / "Include",
            workspace_root / "Drivers",
            workspace_root / "ThirdParty",
            workspace_root / "Utility",
            workspace_root / "UI"
        };
        for (const auto& sub : candidate_subs)
        {
            if (std::filesystem::exists(sub, ec) && std::filesystem::is_directory(sub, ec))
            {
                paths.push_back(sub);
            }
        }
    }

    // 3. Fallback well-known system header locations
#if defined(_WIN32)
    const std::filesystem::path win_candidates[] = {
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC",
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC",
        "C:/Program Files (x86)/Windows Kits/10/Include",
        "C:/Users/Administrator/scoop/apps/llvm/current/lib/clang",
        "C:/msys64/mingw64/include/c++",
        "C:/MinGW/include"
    };
    for (const auto& cand : win_candidates)
    {
        if (std::filesystem::exists(cand, ec) && std::filesystem::is_directory(cand, ec))
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     cand, std::filesystem::directory_options::skip_permission_denied, ec))
            {
                if (entry.is_directory(ec) && (entry.path().filename() == "include" || entry.path().filename() == "ucrt"))
                {
                    paths.push_back(entry.path());
                }
            }
        }
    }
#else
    const std::filesystem::path posix_candidates[] = {
        "/usr/include",
        "/usr/local/include",
        "/usr/include/c++/13",
        "/usr/include/c++/12",
        "/usr/include/c++/11",
        "/usr/include/x86_64-linux-gnu",
        "/usr/include/x86_64-linux-gnu/c++/13",
        "/usr/include/x86_64-linux-gnu/c++/12",
        "/usr/include/x86_64-linux-gnu/c++/11",
        "/Library/Developer/CommandLineTools/usr/include/c++/v1",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1"
    };
    for (const auto& cand : posix_candidates)
    {
        if (std::filesystem::exists(cand, ec) && std::filesystem::is_directory(cand, ec))
        {
            paths.push_back(cand);
        }
    }
#endif

    // Deduplicate
    std::vector<std::filesystem::path> unique_paths;
    for (const auto& p : paths)
    {
        bool found = false;
        for (const auto& u : unique_paths)
        {
            if (std::filesystem::equivalent(p, u, ec))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            unique_paths.push_back(p);
        }
    }

    return unique_paths;
}

std::optional<std::filesystem::path> SymbolDefinitionResolver::find_header_in_system_paths(
    std::string_view header_name,
    const std::filesystem::path& workspace_root)
{
    const auto paths = get_all_include_search_paths(workspace_root);
    std::error_code ec;

    std::string h_str(header_name);

    for (const auto& search_dir : paths)
    {
        // 1. Direct path check (e.g. search_dir / "string" or search_dir / "iostream")
        const auto direct = search_dir / h_str;
        if (std::filesystem::exists(direct, ec) && std::filesystem::is_regular_file(direct, ec))
        {
            return direct;
        }

        // 2. With .h extension check (e.g. cstdio -> stdio.h)
        const auto with_h = search_dir / (h_str + ".h");
        if (std::filesystem::exists(with_h, ec) && std::filesystem::is_regular_file(with_h, ec))
        {
            return with_h;
        }

        // 3. Recursive directory scan up to depth 3 for submodules (e.g. bits/basic_string.h, xstring, etc.)
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 search_dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (entry.is_regular_file(ec))
            {
                const std::string fname = entry.path().filename().string();
                if (fname == h_str || fname == (h_str + ".h") || (h_str == "string" && fname == "xstring") ||
                    (h_str == "vector" && fname == "vector") || (h_str == "iostream" && fname == "iostream"))
                {
                    return entry.path();
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace Zenvra::Language::Definition
