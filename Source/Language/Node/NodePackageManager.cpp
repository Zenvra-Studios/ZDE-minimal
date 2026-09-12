#include "Language/Node/NodePackageManager.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>

namespace Zenvra::Language::Node
{

NodePackageManager& NodePackageManager::instance() noexcept
{
    static NodePackageManager s_instance;
    return s_instance;
}

std::filesystem::path NodePackageManager::find_project_root(const std::filesystem::path& start_path) const
{
    std::filesystem::path current = start_path;
    std::error_code ec;

    if (current.empty())
    {
        current = std::filesystem::current_path(ec);
    }
    else if (std::filesystem::is_regular_file(current, ec))
    {
        current = current.parent_path();
    }

    while (!current.empty() && current != current.root_path())
    {
        if (std::filesystem::exists(current / "package.json", ec) ||
            std::filesystem::exists(current / "node_modules", ec))
        {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current)
        {
            break;
        }
        current = parent;
    }

    return start_path.empty() ? std::filesystem::current_path(ec) : start_path;
}

ProjectManifest NodePackageManager::scan_workspace(const std::filesystem::path& workspace_root)
{
    const auto root = find_project_root(workspace_root);
    const std::string root_key = root.string();

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_manifest_cache.find(root_key);
        if (it != m_manifest_cache.end())
        {
            // Cache valid for 3 seconds
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_scanned).count() < 3)
            {
                return it->second;
            }
        }
    }

    ProjectManifest manifest;
    manifest.root_path = root;
    manifest.last_scanned = now;

    std::unordered_set<std::string> seen_packages;
    std::error_code ec;

    // 1. Parse root package.json if present
    const auto pkg_json_path = root / "package.json";
    if (std::filesystem::exists(pkg_json_path, ec))
    {
        try
        {
            std::ifstream file(pkg_json_path);
            if (file.is_open())
            {
                nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
                if (!j.is_discarded() && j.is_object())
                {
                    if (j.contains("name") && j["name"].is_string())
                    {
                        manifest.project_name = j["name"].get<std::string>();
                    }

                    auto parse_deps = [&](const char* field, bool is_dev) {
                        if (j.contains(field) && j[field].is_object())
                        {
                            for (auto& [pkg_name, ver_val] : j[field].items())
                            {
                                if (seen_packages.insert(pkg_name).second)
                                {
                                    NodePackageInfo info;
                                    info.name = pkg_name;
                                    if (ver_val.is_string())
                                    {
                                        info.version = ver_val.get<std::string>();
                                    }
                                    info.is_dev_dependency = is_dev;
                                    manifest.packages.push_back(std::move(info));
                                }
                            }
                        }
                    };

                    parse_deps("dependencies", false);
                    parse_deps("devDependencies", true);
                    parse_deps("peerDependencies", false);
                }
            }
        }
        catch (...)
        {
        }
    }

    // 2. Scan node_modules directory if present
    const auto node_modules_dir = root / "node_modules";
    if (std::filesystem::exists(node_modules_dir, ec) && std::filesystem::is_directory(node_modules_dir, ec))
    {
        auto scan_package_dir = [&](const std::filesystem::path& pkg_dir, const std::string& pkg_name) {
            std::error_code dir_ec;
            if (!std::filesystem::is_directory(pkg_dir, dir_ec))
            {
                return;
            }

            // Find or insert package in manifest
            auto it = std::find_if(manifest.packages.begin(), manifest.packages.end(),
                                   [&](const NodePackageInfo& p) { return p.name == pkg_name; });
            if (it == manifest.packages.end())
            {
                if (seen_packages.insert(pkg_name).second)
                {
                    NodePackageInfo new_pkg;
                    new_pkg.name = pkg_name;
                    new_pkg.is_installed = true;
                    manifest.packages.push_back(std::move(new_pkg));
                    it = manifest.packages.end() - 1;
                }
                else
                {
                    return;
                }
            }
            else
            {
                it->is_installed = true;
            }

            // Inspect package.json inside package
            const auto sub_pkg_json = pkg_dir / "package.json";
            if (std::filesystem::exists(sub_pkg_json, dir_ec))
            {
                try
                {
                    std::ifstream sub_file(sub_pkg_json);
                    if (sub_file.is_open())
                    {
                        nlohmann::json sub_j = nlohmann::json::parse(sub_file, nullptr, false);
                        if (!sub_j.is_discarded() && sub_j.is_object())
                        {
                            if (it->description.empty() && sub_j.contains("description") && sub_j["description"].is_string())
                            {
                                it->description = sub_j["description"].get<std::string>();
                            }
                            if (it->version.empty() && sub_j.contains("version") && sub_j["version"].is_string())
                            {
                                it->version = sub_j["version"].get<std::string>();
                            }

                            // Look for types / typings
                            std::string types_field;
                            if (sub_j.contains("types") && sub_j["types"].is_string())
                            {
                                types_field = sub_j["types"].get<std::string>();
                            }
                            else if (sub_j.contains("typings") && sub_j["typings"].is_string())
                            {
                                types_field = sub_j["typings"].get<std::string>();
                            }

                            if (!types_field.empty())
                            {
                                const auto tp = pkg_dir / types_field;
                                if (std::filesystem::exists(tp, dir_ec))
                                {
                                    it->types_path = tp.string();
                                }
                            }
                        }
                    }
                }
                catch (...)
                {
                }
            }

            // Fallback type declaration checks
            if (it->types_path.empty())
            {
                if (std::filesystem::exists(pkg_dir / "index.d.ts", dir_ec))
                {
                    it->types_path = (pkg_dir / "index.d.ts").string();
                }
                else
                {
                    // Check @types/<pkg_name>
                    const auto at_types_path = node_modules_dir / "@types" / pkg_name / "index.d.ts";
                    if (std::filesystem::exists(at_types_path, dir_ec))
                    {
                        it->types_path = at_types_path.string();
                    }
                }
            }
        };

        for (const auto& entry : std::filesystem::directory_iterator(node_modules_dir, ec))
        {
            if (!entry.is_directory(ec)) continue;
            const auto name = entry.path().filename().string();
            if (name.empty() || name.front() == '.') continue;

            if (name.front() == '@')
            {
                // Scoped package e.g. @nestjs, @types
                for (const auto& sub_entry : std::filesystem::directory_iterator(entry.path(), ec))
                {
                    if (!sub_entry.is_directory(ec)) continue;
                    const auto sub_name = sub_entry.path().filename().string();
                    const std::string full_name = name + "/" + sub_name;
                    scan_package_dir(sub_entry.path(), full_name);
                }
            }
            else
            {
                scan_package_dir(entry.path(), name);
            }
        }
    }

    // 3. Find local TypeScript compiler in node_modules if present
    const auto ts_lib_dir = node_modules_dir / "typescript" / "lib";
    if (std::filesystem::exists(ts_lib_dir, ec))
    {
        const auto tsserverlib = ts_lib_dir / "tsserverlibrary.js";
        const auto tsserverjs = ts_lib_dir / "tsserver.js";
        if (std::filesystem::exists(tsserverlib, ec))
        {
            manifest.local_tsserver_path = tsserverlib;
        }
        else if (std::filesystem::exists(tsserverjs, ec))
        {
            manifest.local_tsserver_path = tsserverjs;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_manifest_cache[root_key] = manifest;
    }

    return manifest;
}

std::vector<std::string> NodePackageManager::get_detected_packages(const std::filesystem::path& workspace_root)
{
    const auto manifest = scan_workspace(workspace_root);
    std::vector<std::string> result;
    result.reserve(manifest.packages.size());
    for (const auto& pkg : manifest.packages)
    {
        result.push_back(pkg.name);
    }
    return result;
}

std::filesystem::path NodePackageManager::find_local_tsserver(const std::filesystem::path& workspace_root)
{
    const auto manifest = scan_workspace(workspace_root);
    return manifest.local_tsserver_path;
}

bool NodePackageManager::is_import_specifier_context(std::string_view line_text, std::size_t col, std::string& out_partial)
{
    const std::size_t safe_col = std::min(col, line_text.size());
    const std::string_view before_cursor = line_text.substr(0, safe_col);

    // Find the last single or double quote
    const auto last_sq = before_cursor.rfind('\'');
    const auto last_dq = before_cursor.rfind('"');

    std::size_t quote_pos = std::string_view::npos;
    char quote_char = 0;

    if (last_sq != std::string_view::npos && last_dq != std::string_view::npos)
    {
        if (last_sq > last_dq)
        {
            quote_pos = last_sq;
            quote_char = '\'';
        }
        else
        {
            quote_pos = last_dq;
            quote_char = '"';
        }
    }
    else if (last_sq != std::string_view::npos)
    {
        quote_pos = last_sq;
        quote_char = '\'';
    }
    else if (last_dq != std::string_view::npos)
    {
        quote_pos = last_dq;
        quote_char = '"';
    }
    else
    {
        return false;
    }

    // Count quotes of quote_char before quote_pos to ensure quote_pos is an opening quote
    std::size_t quote_count = 0;
    for (std::size_t i = 0; i < quote_pos; ++i)
    {
        if (before_cursor[i] == quote_char && (i == 0 || before_cursor[i - 1] != '\\'))
        {
            ++quote_count;
        }
    }
    // If quote_count is odd, quote_pos is actually a closing quote!
    if (quote_count % 2 != 0)
    {
        return false;
    }

    // Check if the preceding text matches an import/require pattern
    const std::string_view prefix = before_cursor.substr(0, quote_pos);
    const auto trim_pos = prefix.find_last_not_of(" \t");
    const std::string_view trimmed_prefix = (trim_pos != std::string_view::npos)
                                                ? prefix.substr(0, trim_pos + 1)
                                                : prefix;

    const bool is_import_stmt =
        trimmed_prefix.ends_with("from") ||
        trimmed_prefix.ends_with("import") ||
        trimmed_prefix.ends_with("require(") ||
        trimmed_prefix.ends_with("require (") ||
        trimmed_prefix.ends_with("import(") ||
        trimmed_prefix.ends_with("import (");

    if (is_import_stmt)
    {
        out_partial = std::string(before_cursor.substr(quote_pos + 1));
        return true;
    }

    return false;
}

std::vector<Protocol::CompletionItem> NodePackageManager::get_import_completions(
    std::string_view line_text,
    std::size_t col,
    const std::filesystem::path& workspace_root)
{
    std::string partial;
    if (!is_import_specifier_context(line_text, col, partial))
    {
        return {};
    }

    const auto manifest = scan_workspace(workspace_root);
    std::vector<Protocol::CompletionItem> items;
    items.reserve(manifest.packages.size());

    for (const auto& pkg : manifest.packages)
    {
        // Skip @types packages from direct import completion (suggest base name instead)
        if (pkg.name.starts_with("@types/"))
        {
            continue;
        }

        Protocol::CompletionItem item;
        item.label = pkg.name;
        item.kind = Protocol::CompletionItemKind::Module;
        item.detail = "(Node Module) " + pkg.name +
                      (pkg.version.empty() ? "" : (" " + pkg.version)) +
                      (pkg.is_dev_dependency ? " [dev]" : "");
        item.documentation = pkg.description.empty()
                                 ? ("Dependency package from " + (manifest.project_name.empty() ? "project" : manifest.project_name))
                                 : pkg.description;
        item.insert_text = pkg.name;
        item.filter_text = pkg.name;
        item.sort_text = (pkg.is_installed ? "0_" : "1_") + pkg.name;
        items.push_back(std::move(item));
    }

    return items;
}

std::vector<Protocol::CompletionItem> NodePackageManager::get_import_statement_completions(
    std::string_view line_text,
    std::size_t col,
    const std::filesystem::path& workspace_root)
{
    const std::size_t safe_col = std::min(col, line_text.size());
    const std::string_view before_cursor = line_text.substr(0, safe_col);

    // 1. If inside quotes after from/require/import:
    std::string partial;
    if (is_import_specifier_context(line_text, col, partial))
    {
        return get_import_completions(line_text, col, workspace_root);
    }

    const auto manifest = scan_workspace(workspace_root);
    std::vector<Protocol::CompletionItem> items;

    // 2. Check if writing after `import <Identifier> ` (e.g. `import React `)
    const auto first_non_space = before_cursor.find_first_not_of(" \t");
    if (first_non_space != std::string_view::npos)
    {
        const std::string_view trimmed = before_cursor.substr(first_non_space);
        if (trimmed.starts_with("import ") && trimmed.find("from") == std::string_view::npos)
        {
            std::string_view after_import = trimmed.substr(7);
            const auto id_start = after_import.find_first_not_of(" \t");
            if (id_start != std::string_view::npos)
            {
                after_import = after_import.substr(id_start);
                // Check if inside curly braces: e.g. "import { use"
                if (after_import.starts_with("{"))
                {
                    const auto libs = get_library_completions(workspace_root, false);
                    for (const auto& itm : libs)
                    {
                        if (itm.kind == Protocol::CompletionItemKind::Function ||
                            itm.kind == Protocol::CompletionItemKind::Interface ||
                            itm.kind == Protocol::CompletionItemKind::TypeParameter ||
                            itm.kind == Protocol::CompletionItemKind::Class)
                        {
                            items.push_back(itm);
                        }
                    }
                    return items;
                }

                // Extract identifier name (e.g. "React")
                std::size_t id_len = 0;
                while (id_len < after_import.size() &&
                       (std::isalnum(static_cast<unsigned char>(after_import[id_len])) ||
                        after_import[id_len] == '_' || after_import[id_len] == '$'))
                {
                    ++id_len;
                }

                if (id_len > 0)
                {
                    const std::string id = std::string(after_import.substr(0, id_len));
                    std::string id_lower = id;
                    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), ::tolower);

                    // Find matching detected package
                    std::string matched_pkg;
                    for (const auto& pkg : manifest.packages)
                    {
                        std::string pkg_lower = pkg.name;
                        std::transform(pkg_lower.begin(), pkg_lower.end(), pkg_lower.begin(), ::tolower);
                        if (pkg_lower == id_lower || pkg_lower.find(id_lower) != std::string::npos || id_lower.find(pkg_lower) != std::string::npos)
                        {
                            matched_pkg = pkg.name;
                            break;
                        }
                    }
                    if (matched_pkg.empty())
                    {
                        matched_pkg = id_lower;
                    }

                    // 1. Suggest: from '<matched_pkg>'
                    Protocol::CompletionItem item_matched;
                    item_matched.label = "from '" + matched_pkg + "'";
                    item_matched.kind = Protocol::CompletionItemKind::Keyword;
                    item_matched.detail = "from '" + matched_pkg + "';";
                    item_matched.insert_text = "from '" + matched_pkg + "';$0";
                    item_matched.filter_text = "from";
                    item_matched.sort_text = "0_from_" + matched_pkg;
                    items.push_back(std::move(item_matched));

                    // 2. Suggest: from '<module>'
                    Protocol::CompletionItem item_generic;
                    item_generic.label = "from";
                    item_generic.kind = Protocol::CompletionItemKind::Keyword;
                    item_generic.detail = "from '...';";
                    item_generic.insert_text = "from '${1:" + matched_pkg + "}';$0";
                    item_generic.filter_text = "from";
                    item_generic.sort_text = "1_from";
                    items.push_back(std::move(item_generic));

                    // 3. Suggest: , { ... } from '<matched_pkg>'
                    Protocol::CompletionItem item_named;
                    item_named.label = ", { ... } from '" + matched_pkg + "'";
                    item_named.kind = Protocol::CompletionItemKind::Snippet;
                    item_named.detail = ", { ... } from '" + matched_pkg + "';";
                    item_named.insert_text = ", { ${1:members} } from '" + matched_pkg + "';$0";
                    item_named.filter_text = ",";
                    item_named.sort_text = "2_named";
                    items.push_back(std::move(item_named));

                    // 4. Also suggest all detected packages as `from '<pkg>'`
                    for (const auto& pkg : manifest.packages)
                    {
                        if (pkg.name.starts_with("@types/") || pkg.name == matched_pkg) continue;
                        Protocol::CompletionItem itm;
                        itm.label = "from '" + pkg.name + "'";
                        itm.kind = Protocol::CompletionItemKind::Keyword;
                        itm.detail = "from '" + pkg.name + "';";
                        itm.insert_text = "from '" + pkg.name + "';$0";
                        itm.filter_text = "from";
                        itm.sort_text = "3_" + pkg.name;
                        items.push_back(std::move(itm));
                    }

                    return items;
                }
            }

            // If just "import "
            // Suggest default import snippets for all detected packages!
            for (const auto& pkg : manifest.packages)
            {
                if (pkg.name.starts_with("@types/")) continue;
                std::string default_name = pkg.name;
                if (const auto slash = default_name.rfind('/'); slash != std::string::npos)
                {
                    default_name = default_name.substr(slash + 1);
                }
                if (const auto dash = default_name.find('-'); dash != std::string::npos)
                {
                    default_name = default_name.substr(0, dash);
                }
                if (default_name == "react") default_name = "React";

                Protocol::CompletionItem itm;
                itm.label = default_name + " from '" + pkg.name + "'";
                itm.kind = Protocol::CompletionItemKind::Snippet;
                itm.detail = "import " + default_name + " from '" + pkg.name + "';";
                itm.insert_text = default_name + " from '" + pkg.name + "';$0";
                itm.filter_text = default_name;
                itm.sort_text = "0_" + default_name;
                items.push_back(std::move(itm));

                Protocol::CompletionItem itm_named;
                itm_named.label = "{ ... } from '" + pkg.name + "'";
                itm_named.kind = Protocol::CompletionItemKind::Snippet;
                itm_named.detail = "import { ... } from '" + pkg.name + "';";
                itm_named.insert_text = "{ ${1:members} } from '" + pkg.name + "';$0";
                itm_named.filter_text = "{";
                itm_named.sort_text = "1_" + pkg.name;
                items.push_back(std::move(itm_named));
            }
        }
    }

    return items;
}

std::vector<Protocol::CompletionItem> NodePackageManager::get_catalog_completions_for_package(
    const std::string& package_name,
    bool is_scoped,
    std::string_view scoped_prefix) const
{
    std::vector<Protocol::CompletionItem> items;

    // 1. React
    if (package_name == "react" || package_name.find("react") != std::string::npos)
    {
        if (is_scoped && (scoped_prefix == "React" || scoped_prefix == "react"))
        {
            static const std::vector<Protocol::CompletionItem> s_react_scoped = {
                {"createElement", Protocol::CompletionItemKind::Method, "createElement(type, props, ...children)", "Creates and returns a new React element of the given type.", "createElement(${1:type}, ${2:props})", "createElement"},
                {"cloneElement", Protocol::CompletionItemKind::Method, "cloneElement(element, [props], [...children])", "Clones and returns a new React element using element as the starting point.", "cloneElement(${1:element}, ${2:props})", "cloneElement"},
                {"useState", Protocol::CompletionItemKind::Method, "useState<T>(initialState: T): [T, Dispatch<SetStateAction<T>>]", "Returns a stateful value and a function to update it.", "useState(${1:initialState})", "useState"},
                {"useEffect", Protocol::CompletionItemKind::Method, "useEffect(effect: EffectCallback, deps?: DependencyList): void", "Accepts a function that contains imperative, possibly effectful code.", "useEffect(() => {\n    $0\n}, [${1}]);", "useEffect"},
                {"useContext", Protocol::CompletionItemKind::Method, "useContext<T>(context: Context<T>): T", "Accepts a context object and returns the current context value.", "useContext(${1:Context})", "useContext"},
                {"useReducer", Protocol::CompletionItemKind::Method, "useReducer(reducer, initialArg, init?)", "An alternative to useState for complex state logic.", "useReducer(${1:reducer}, ${2:initialState})", "useReducer"},
                {"useCallback", Protocol::CompletionItemKind::Method, "useCallback(callback, deps)", "Returns a memoized version of the callback.", "useCallback((${1:params}) => {\n    $0\n}, [${2}]);", "useCallback"},
                {"useMemo", Protocol::CompletionItemKind::Method, "useMemo(factory, deps)", "Returns a memoized value.", "useMemo(() => ${1:compute}, [${2}]);", "useMemo"},
                {"useRef", Protocol::CompletionItemKind::Method, "useRef<T>(initialValue: T): RefObject<T>", "Returns a mutable ref object whose .current property is initialized to the passed argument.", "useRef(${1:null})", "useRef"},
                {"memo", Protocol::CompletionItemKind::Method, "memo<T>(Component: T): T", "Higher-order component for memoizing rendered output.", "memo(${1:Component})", "memo"},
                {"forwardRef", Protocol::CompletionItemKind::Method, "forwardRef(render): Component", "Forward refs to child components.", "forwardRef((${1:props}, ${2:ref}) => {\n    $0\n})", "forwardRef"},
                {"createContext", Protocol::CompletionItemKind::Method, "createContext<T>(defaultValue: T): Context<T>", "Creates a Context object.", "createContext(${1:defaultValue})", "createContext"},
                {"Fragment", Protocol::CompletionItemKind::Class, "Fragment: Component", "Component for grouping a list of children without adding extra nodes to the DOM.", "Fragment", "Fragment"},
                {"Suspense", Protocol::CompletionItemKind::Class, "Suspense: Component", "Lets you display a fallback until children have finished loading.", "Suspense", "Suspense"}
            };
            items.insert(items.end(), s_react_scoped.begin(), s_react_scoped.end());
        }
        else if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_react_toplevel = {
                {"useState", Protocol::CompletionItemKind::Function, "useState<T>(initialState: T): [T, Dispatch<SetStateAction<T>>]", "Returns a stateful value and a function to update it.", "const [${1:state}, set${1/(.*)/${1:/capitalize}/}] = useState(${2:initial});", "useState"},
                {"useEffect", Protocol::CompletionItemKind::Function, "useEffect(effect: () => void | Destructor, deps?: any[]): void", "Accepts a function that contains imperative, possibly effectful code.", "useEffect(() => {\n    $0\n}, [${1}]);", "useEffect"},
                {"useContext", Protocol::CompletionItemKind::Function, "useContext<T>(context: Context<T>): T", "Returns current context value for specified context.", "const ${1:val} = useContext(${2:MyContext});", "useContext"},
                {"useReducer", Protocol::CompletionItemKind::Function, "useReducer(reducer, initialArg, init?)", "An alternative to useState for complex state logic.", "const [state, dispatch] = useReducer(${1:reducer}, ${2:initialState});", "useReducer"},
                {"useCallback", Protocol::CompletionItemKind::Function, "useCallback(callback, deps)", "Returns a memoized callback.", "const ${1:memoizedCallback} = useCallback((${2:params}) => {\n    $0\n}, [${3}]);", "useCallback"},
                {"useMemo", Protocol::CompletionItemKind::Function, "useMemo(factory, deps)", "Returns a memoized calculation result.", "const ${1:memoizedValue} = useMemo(() => ${2:compute}, [${3}]);", "useMemo"},
                {"useRef", Protocol::CompletionItemKind::Function, "useRef<T>(initialValue: T): RefObject<T>", "Returns a mutable ref object whose .current property is initialized.", "const ${1:ref} = useRef<${2:HTMLDivElement}>(${3:null});", "useRef"},
                {"FC", Protocol::CompletionItemKind::Interface, "type FC<P = {}> = FunctionComponent<P>", "React Function Component type.", "FC<${1:Props}>", "FC"},
                {"ReactNode", Protocol::CompletionItemKind::TypeParameter, "type ReactNode = ReactChild | ReactFragment | ReactPortal | boolean | null | undefined", "Represents any renderable React node.", "ReactNode", "ReactNode"},
                {"CSSProperties", Protocol::CompletionItemKind::Interface, "interface CSSProperties", "CSS properties dictionary for style props.", "CSSProperties", "CSSProperties"},
                {"Component", Protocol::CompletionItemKind::Class, "class Component<P, S>", "Base class for React class components.", "Component<${1:Props}, ${2:State}>", "Component"},
                {"PureComponent", Protocol::CompletionItemKind::Class, "class PureComponent<P, S>", "Base class with shallow prop and state comparison.", "PureComponent<${1:Props}, ${2:State}>", "PureComponent"}
            };
            items.insert(items.end(), s_react_toplevel.begin(), s_react_toplevel.end());
        }
    }

    // 2. Express
    if (package_name == "express")
    {
        if (is_scoped && (scoped_prefix == "express" || scoped_prefix == "Express"))
        {
            static const std::vector<Protocol::CompletionItem> s_express_scoped = {
                {"Router", Protocol::CompletionItemKind::Method, "express.Router(options?): Router", "Creates a new router instance.", "Router()", "Router"},
                {"json", Protocol::CompletionItemKind::Method, "express.json(options?): RequestHandler", "Built-in middleware function to parse incoming requests with JSON payloads.", "json()", "json"},
                {"urlencoded", Protocol::CompletionItemKind::Method, "express.urlencoded(options): RequestHandler", "Middleware function to parse incoming requests with urlencoded payloads.", "urlencoded({ extended: ${1:true} })", "urlencoded"},
                {"static", Protocol::CompletionItemKind::Method, "express.static(root, options?): RequestHandler", "Built-in middleware function to serve static files.", "static('${1:public}')", "static"}
            };
            items.insert(items.end(), s_express_scoped.begin(), s_express_scoped.end());
        }
        else if (is_scoped && (scoped_prefix == "app" || scoped_prefix == "router" || scoped_prefix == "server"))
        {
            static const std::vector<Protocol::CompletionItem> s_app_methods = {
                {"get", Protocol::CompletionItemKind::Method, "get(path, ...handlers): this", "Routes HTTP GET requests to the specified path with callback functions.", "get('${1:/path}', (req, res) => {\n    $0\n});", "get"},
                {"post", Protocol::CompletionItemKind::Method, "post(path, ...handlers): this", "Routes HTTP POST requests.", "post('${1:/path}', (req, res) => {\n    $0\n});", "post"},
                {"put", Protocol::CompletionItemKind::Method, "put(path, ...handlers): this", "Routes HTTP PUT requests.", "put('${1:/path}', (req, res) => {\n    $0\n});", "put"},
                {"delete", Protocol::CompletionItemKind::Method, "delete(path, ...handlers): this", "Routes HTTP DELETE requests.", "delete('${1:/path}', (req, res) => {\n    $0\n});", "delete"},
                {"patch", Protocol::CompletionItemKind::Method, "patch(path, ...handlers): this", "Routes HTTP PATCH requests.", "patch('${1:/path}', (req, res) => {\n    $0\n});", "patch"},
                {"use", Protocol::CompletionItemKind::Method, "use([path], ...middleware): this", "Mounts the specified middleware function or functions.", "use(${1:middleware});", "use"},
                {"listen", Protocol::CompletionItemKind::Method, "listen(port, [hostname], [backlog], [callback]): Server", "Binds and listens for connections on the specified host and port.", "listen(${1:port}, () => {\n    console.log(`Server listening on port ${${1:port}}`);\n});", "listen"},
                {"all", Protocol::CompletionItemKind::Method, "all(path, ...handlers): this", "Routes all HTTP verbs to the specified path.", "all('${1:path}', ${2:handler});", "all"},
                {"route", Protocol::CompletionItemKind::Method, "route(path): Route", "Returns an instance of a single route for chainable route handlers.", "route('${1:path}')", "route"},
                {"set", Protocol::CompletionItemKind::Method, "set(setting, val): this", "Assigns setting name to value.", "set('${1:setting}', ${2:val});", "set"}
            };
            items.insert(items.end(), s_app_methods.begin(), s_app_methods.end());
        }
        else if (is_scoped && (scoped_prefix == "res" || scoped_prefix == "response"))
        {
            static const std::vector<Protocol::CompletionItem> s_res_methods = {
                {"status", Protocol::CompletionItemKind::Method, "res.status(code): Response", "Sets the HTTP status for the response.", "status(${1:200})", "status"},
                {"send", Protocol::CompletionItemKind::Method, "res.send(body): Response", "Sends the HTTP response.", "send(${1:data})", "send"},
                {"json", Protocol::CompletionItemKind::Method, "res.json(body): Response", "Sends a JSON response.", "json(${1:data})", "json"},
                {"sendStatus", Protocol::CompletionItemKind::Method, "res.sendStatus(statusCode): Response", "Sets the response HTTP status code to statusCode and sends its string representation.", "sendStatus(${1:200})", "sendStatus"},
                {"sendFile", Protocol::CompletionItemKind::Method, "res.sendFile(path, [options], [fn]): void", "Transfers the file at the given path.", "sendFile(${1:filePath})", "sendFile"},
                {"redirect", Protocol::CompletionItemKind::Method, "res.redirect([status], url): void", "Redirects to the URL derived from the specified path.", "redirect('${1:url}')", "redirect"},
                {"setHeader", Protocol::CompletionItemKind::Method, "res.setHeader(name, value): Response", "Sets an outgoing header value.", "setHeader('${1:name}', '${2:value}')", "setHeader"},
                {"cookie", Protocol::CompletionItemKind::Method, "res.cookie(name, value, [options]): Response", "Sets cookie name to value.", "cookie('${1:name}', ${2:value})", "cookie"},
                {"clearCookie", Protocol::CompletionItemKind::Method, "res.clearCookie(name, [options]): Response", "Clears the cookie specified by name.", "clearCookie('${1:name}')", "clearCookie"},
                {"end", Protocol::CompletionItemKind::Method, "res.end(): void", "Ends the response process.", "end()", "end"}
            };
            items.insert(items.end(), s_res_methods.begin(), s_res_methods.end());
        }
        else if (is_scoped && (scoped_prefix == "req" || scoped_prefix == "request"))
        {
            static const std::vector<Protocol::CompletionItem> s_req_props = {
                {"body", Protocol::CompletionItemKind::Property, "req.body: any", "Contains key-value pairs of data submitted in the request body.", "body", "body"},
                {"params", Protocol::CompletionItemKind::Property, "req.params: Record<string, string>", "Contains route parameters mapped to their values.", "params", "params"},
                {"query", Protocol::CompletionItemKind::Property, "req.query: ParsedQs", "Contains key-value pairs of query string parameters.", "query", "query"},
                {"headers", Protocol::CompletionItemKind::Property, "req.headers: IncomingHttpHeaders", "Request headers dictionary.", "headers", "headers"},
                {"cookies", Protocol::CompletionItemKind::Property, "req.cookies: any", "Contains cookies sent by the request.", "cookies", "cookies"},
                {"method", Protocol::CompletionItemKind::Property, "req.method: string", "Contains a string corresponding to the HTTP method of the request.", "method", "method"},
                {"url", Protocol::CompletionItemKind::Property, "req.url: string", "The request URL string.", "url", "url"},
                {"path", Protocol::CompletionItemKind::Property, "req.path: string", "Contains the path part of the request URL.", "path", "path"},
                {"ip", Protocol::CompletionItemKind::Property, "req.ip: string", "Contains the remote IP address of the request.", "ip", "ip"},
                {"get", Protocol::CompletionItemKind::Method, "req.get(field): string | undefined", "Returns the specified HTTP request header field.", "get('${1:header}')", "get"}
            };
            items.insert(items.end(), s_req_props.begin(), s_req_props.end());
        }
        else if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_express_toplevel = {
                {"express", Protocol::CompletionItemKind::Function, "express(): Express", "Creates an Express application.", "const app = express();", "express"},
                {"Router", Protocol::CompletionItemKind::Function, "Router(options?): Router", "Creates an Express router.", "const router = express.Router();", "Router"},
                {"Request", Protocol::CompletionItemKind::Interface, "interface Request", "Express HTTP Request interface.", "Request", "Request"},
                {"Response", Protocol::CompletionItemKind::Interface, "interface Response", "Express HTTP Response interface.", "Response", "Response"},
                {"NextFunction", Protocol::CompletionItemKind::Interface, "type NextFunction = (err?: any) => void", "Express NextFunction middleware callback.", "NextFunction", "NextFunction"},
                {"Application", Protocol::CompletionItemKind::Interface, "interface Application", "Express Application interface.", "Application", "Application"},
                {"RequestHandler", Protocol::CompletionItemKind::Interface, "interface RequestHandler", "Express route handler / middleware callback interface.", "RequestHandler", "RequestHandler"}
            };
            items.insert(items.end(), s_express_toplevel.begin(), s_express_toplevel.end());
        }
    }

    // 3. Axios
    if (package_name == "axios")
    {
        if (is_scoped && (scoped_prefix == "axios" || scoped_prefix == "api" || scoped_prefix == "http" || scoped_prefix == "client"))
        {
            static const std::vector<Protocol::CompletionItem> s_axios_scoped = {
                {"get", Protocol::CompletionItemKind::Method, "axios.get<T = any, R = AxiosResponse<T>>(url, [config]): Promise<R>", "Performs an HTTP GET request.", "get<${1:any}>('${2:url}')", "get"},
                {"post", Protocol::CompletionItemKind::Method, "axios.post<T = any, R = AxiosResponse<T>>(url, [data], [config]): Promise<R>", "Performs an HTTP POST request.", "post<${1:any}>('${2:url}', ${3:data})", "post"},
                {"put", Protocol::CompletionItemKind::Method, "axios.put<T = any, R = AxiosResponse<T>>(url, [data], [config]): Promise<R>", "Performs an HTTP PUT request.", "put<${1:any}>('${2:url}', ${3:data})", "put"},
                {"delete", Protocol::CompletionItemKind::Method, "axios.delete<T = any, R = AxiosResponse<T>>(url, [config]): Promise<R>", "Performs an HTTP DELETE request.", "delete<${1:any}>('${2:url}')", "delete"},
                {"patch", Protocol::CompletionItemKind::Method, "axios.patch<T = any, R = AxiosResponse<T>>(url, [data], [config]): Promise<R>", "Performs an HTTP PATCH request.", "patch<${1:any}>('${2:url}', ${3:data})", "patch"},
                {"create", Protocol::CompletionItemKind::Method, "axios.create([config]): AxiosInstance", "Creates a new custom Axios instance with default config.", "create({\n    baseURL: '${1:http://localhost:3000}',\n    headers: {\n        'Content-Type': 'application/json'\n    }\n})", "create"},
                {"all", Protocol::CompletionItemKind::Method, "axios.all(iterable): Promise<any[]>", "Convenience method for handling concurrent requests.", "all([${1}])", "all"},
                {"isAxiosError", Protocol::CompletionItemKind::Method, "axios.isAxiosError(payload): payload is AxiosError", "Determines whether the provided payload is an AxiosError.", "isAxiosError(${1:err})", "isAxiosError"}
            };
            items.insert(items.end(), s_axios_scoped.begin(), s_axios_scoped.end());
        }
        else if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_axios_toplevel = {
                {"axios", Protocol::CompletionItemKind::Variable, "const axios: AxiosStatic", "Default Axios client instance.", "axios", "axios"},
                {"AxiosResponse", Protocol::CompletionItemKind::Interface, "interface AxiosResponse<T = any>", "Represents an HTTP response from Axios.", "AxiosResponse<${1:any}>", "AxiosResponse"},
                {"AxiosRequestConfig", Protocol::CompletionItemKind::Interface, "interface AxiosRequestConfig", "Configuration object for an Axios request.", "AxiosRequestConfig", "AxiosRequestConfig"},
                {"AxiosError", Protocol::CompletionItemKind::Class, "class AxiosError<T = unknown>", "Error object generated on rejected Axios requests.", "AxiosError<${1:any}>", "AxiosError"},
                {"AxiosInstance", Protocol::CompletionItemKind::Interface, "interface AxiosInstance", "Custom instance of Axios.", "AxiosInstance", "AxiosInstance"}
            };
            items.insert(items.end(), s_axios_toplevel.begin(), s_axios_toplevel.end());
        }
    }

    // 4. Lodash
    if (package_name == "lodash" || package_name == "lodash-es")
    {
        if (is_scoped && (scoped_prefix == "_" || scoped_prefix == "lodash"))
        {
            static const std::vector<Protocol::CompletionItem> s_lodash_scoped = {
                {"debounce", Protocol::CompletionItemKind::Method, "_.debounce(func, [wait=0], [options={}])", "Creates a debounced function.", "debounce(${1:fn}, ${2:300})", "debounce"},
                {"throttle", Protocol::CompletionItemKind::Method, "_.throttle(func, [wait=0], [options={}])", "Creates a throttled function.", "throttle(${1:fn}, ${2:300})", "throttle"},
                {"cloneDeep", Protocol::CompletionItemKind::Method, "_.cloneDeep(value)", "Recursively clones value.", "cloneDeep(${1:val})", "cloneDeep"},
                {"merge", Protocol::CompletionItemKind::Method, "_.merge(object, [sources])", "Recursively merges own and inherited enumerable string keyed properties.", "merge(${1:target}, ${2:source})", "merge"},
                {"pick", Protocol::CompletionItemKind::Method, "_.pick(object, [paths])", "Creates an object composed of the picked object properties.", "pick(${1:object}, [${2:keys}])", "pick"},
                {"omit", Protocol::CompletionItemKind::Method, "_.omit(object, [paths])", "The opposite of _.pick; creates an object without picked properties.", "omit(${1:object}, [${2:keys}])", "omit"},
                {"get", Protocol::CompletionItemKind::Method, "_.get(object, path, [defaultValue])", "Gets the value at path of object.", "get(${1:object}, '${2:path}', ${3:defaultVal})", "get"},
                {"set", Protocol::CompletionItemKind::Method, "_.set(object, path, value)", "Sets the value at path of object.", "set(${1:object}, '${2:path}', ${3:val})", "set"},
                {"isEqual", Protocol::CompletionItemKind::Method, "_.isEqual(value, other)", "Performs a deep comparison between two values.", "isEqual(${1:a}, ${2:b})", "isEqual"},
                {"isEmpty", Protocol::CompletionItemKind::Method, "_.isEmpty(value)", "Checks if value is an empty object, collection, map, or set.", "isEmpty(${1:val})", "isEmpty"},
                {"uniq", Protocol::CompletionItemKind::Method, "_.uniq(array)", "Creates a duplicate-free version of an array.", "uniq(${1:arr})", "uniq"},
                {"groupBy", Protocol::CompletionItemKind::Method, "_.groupBy(collection, [iteratee])", "Creates an object composed of keys generated from iteratee.", "groupBy(${1:arr}, '${2:key}')", "groupBy"}
            };
            items.insert(items.end(), s_lodash_scoped.begin(), s_lodash_scoped.end());
        }
        else if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_lodash_toplevel = {
                {"debounce", Protocol::CompletionItemKind::Function, "debounce(func, wait?, options?)", "Creates a debounced function.", "debounce(${1:fn}, ${2:300})", "debounce"},
                {"throttle", Protocol::CompletionItemKind::Function, "throttle(func, wait?, options?)", "Creates a throttled function.", "throttle(${1:fn}, ${2:300})", "throttle"},
                {"cloneDeep", Protocol::CompletionItemKind::Function, "cloneDeep(value)", "Recursively clones value.", "cloneDeep(${1:val})", "cloneDeep"},
                {"isEmpty", Protocol::CompletionItemKind::Function, "isEmpty(value)", "Checks if value is empty.", "isEmpty(${1:val})", "isEmpty"}
            };
            items.insert(items.end(), s_lodash_toplevel.begin(), s_lodash_toplevel.end());
        }
    }

    // 5. Zod
    if (package_name == "zod")
    {
        if (is_scoped && (scoped_prefix == "z" || scoped_prefix == "zod"))
        {
            static const std::vector<Protocol::CompletionItem> s_zod_scoped = {
                {"string", Protocol::CompletionItemKind::Method, "z.string(): ZodString", "Creates a string validator.", "string()", "string"},
                {"number", Protocol::CompletionItemKind::Method, "z.number(): ZodNumber", "Creates a number validator.", "number()", "number"},
                {"boolean", Protocol::CompletionItemKind::Method, "z.boolean(): ZodBoolean", "Creates a boolean validator.", "boolean()", "boolean"},
                {"object", Protocol::CompletionItemKind::Method, "z.object(shape): ZodObject", "Creates an object validator schema.", "object({\n    $0\n})", "object"},
                {"array", Protocol::CompletionItemKind::Method, "z.array(schema): ZodArray", "Creates an array validator schema.", "array(${1:z.string()})", "array"},
                {"enum", Protocol::CompletionItemKind::Method, "z.enum(values): ZodEnum", "Creates an enum validator.", "enum([${1}])", "enum"},
                {"infer", Protocol::CompletionItemKind::Method, "z.infer<T>", "Infers the TypeScript type from a Zod schema.", "infer<typeof ${1:Schema}>", "infer"},
                {"date", Protocol::CompletionItemKind::Method, "z.date(): ZodDate", "Creates a Date validator.", "date()", "date"},
                {"record", Protocol::CompletionItemKind::Method, "z.record(keyType, valueType)", "Creates a record validator.", "record(${1:z.string()}, ${2:z.any()})", "record"}
            };
            items.insert(items.end(), s_zod_scoped.begin(), s_zod_scoped.end());
        }
        else if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_zod_toplevel = {
                {"z", Protocol::CompletionItemKind::Variable, "import { z } from 'zod'", "The Zod schema validation library root.", "z", "z"},
                {"ZodSchema", Protocol::CompletionItemKind::Interface, "interface ZodSchema<T>", "Base interface for all Zod schemas.", "ZodSchema<${1:any}>", "ZodSchema"},
                {"ZodType", Protocol::CompletionItemKind::Class, "class ZodType<Output, Def, Input>", "Base class for Zod types.", "ZodType<${1:any}>", "ZodType"},
                {"ZodError", Protocol::CompletionItemKind::Class, "class ZodError", "Error thrown when validation fails.", "ZodError", "ZodError"},
                {"infer", Protocol::CompletionItemKind::TypeParameter, "type infer<T extends ZodType<any>> = T['_output']", "Extracts the output TypeScript type of a Zod schema.", "z.infer<typeof ${1:Schema}>", "infer"}
            };
            items.insert(items.end(), s_zod_toplevel.begin(), s_zod_toplevel.end());
        }
    }

    // 6. Vue
    if (package_name == "vue")
    {
        if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_vue_toplevel = {
                {"ref", Protocol::CompletionItemKind::Function, "ref<T>(value: T): Ref<UnwrapRef<T>>", "Takes an inner value and returns a reactive and mutable ref object.", "const ${1:state} = ref(${2:initial});", "ref"},
                {"reactive", Protocol::CompletionItemKind::Function, "reactive<T extends object>(target: T): UnwrapNestedRefs<T>", "Returns a reactive proxy of the object.", "const ${1:state} = reactive({\n    $0\n});", "reactive"},
                {"computed", Protocol::CompletionItemKind::Function, "computed<T>(getter: () => T): ComputedRef<T>", "Takes a getter function and returns a readonly reactive ref object for the returned value.", "const ${1:name} = computed(() => ${2:expression});", "computed"},
                {"watch", Protocol::CompletionItemKind::Function, "watch(source, callback, options?)", "Watches one or more reactive data sources and invokes a callback when sources change.", "watch(${1:source}, (${2:newVal}, ${3:oldVal}) => {\n    $0\n});", "watch"},
                {"watchEffect", Protocol::CompletionItemKind::Function, "watchEffect(effect, options?)", "Runs a function immediately while reactively tracking its dependencies and re-runs it whenever the dependencies are changed.", "watchEffect(() => {\n    $0\n});", "watchEffect"},
                {"onMounted", Protocol::CompletionItemKind::Function, "onMounted(hook: () => void): void", "Registers a callback to be called after the component has been mounted.", "onMounted(() => {\n    $0\n});", "onMounted"},
                {"onUnmounted", Protocol::CompletionItemKind::Function, "onUnmounted(hook: () => void): void", "Registers a callback to be called after the component has been unmounted.", "onUnmounted(() => {\n    $0\n});", "onUnmounted"},
                {"defineComponent", Protocol::CompletionItemKind::Function, "defineComponent(options)", "Defines a Vue component with type inference.", "defineComponent({\n    $0\n})", "defineComponent"},
                {"defineProps", Protocol::CompletionItemKind::Function, "defineProps<Props>()", "Defines props in script setup.", "const props = defineProps<${1:Props}>();", "defineProps"},
                {"defineEmits", Protocol::CompletionItemKind::Function, "defineEmits<Emits>()", "Defines emits in script setup.", "const emit = defineEmits<${1:Emits}>();", "defineEmits"},
                {"nextTick", Protocol::CompletionItemKind::Function, "nextTick(): Promise<void>", "A utility for waiting for the next DOM update flush.", "await nextTick();", "nextTick"}
            };
            items.insert(items.end(), s_vue_toplevel.begin(), s_vue_toplevel.end());
        }
    }

    // 7. NestJS
    if (package_name == "@nestjs/common" || package_name == "@nestjs/core")
    {
        if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_nest_toplevel = {
                {"Controller", Protocol::CompletionItemKind::Function, "@Controller(prefix?: string): ClassDecorator", "Decorator that marks a class as a Nest controller.", "@Controller('${1:path}')", "Controller"},
                {"Injectable", Protocol::CompletionItemKind::Function, "@Injectable(): ClassDecorator", "Decorator that marks a class as a provider that can be injected.", "@Injectable()", "Injectable"},
                {"Module", Protocol::CompletionItemKind::Function, "@Module(metadata): ClassDecorator", "Decorator that marks a class as a Nest module.", "@Module({\n    imports: [],\n    controllers: [],\n    providers: []\n})", "Module"},
                {"Get", Protocol::CompletionItemKind::Function, "@Get(path?: string): MethodDecorator", "Route handler decorator for HTTP GET.", "@Get('${1:path}')", "Get"},
                {"Post", Protocol::CompletionItemKind::Function, "@Post(path?: string): MethodDecorator", "Route handler decorator for HTTP POST.", "@Post('${1:path}')", "Post"},
                {"Put", Protocol::CompletionItemKind::Function, "@Put(path?: string): MethodDecorator", "Route handler decorator for HTTP PUT.", "@Put('${1:path}')", "Put"},
                {"Delete", Protocol::CompletionItemKind::Function, "@Delete(path?: string): MethodDecorator", "Route handler decorator for HTTP DELETE.", "@Delete('${1:path}')", "Delete"},
                {"Param", Protocol::CompletionItemKind::Function, "@Param(property?: string): ParameterDecorator", "Extracts route parameter from request.", "@Param('${1:id}') ${1:id}: string", "Param"},
                {"Body", Protocol::CompletionItemKind::Function, "@Body(): ParameterDecorator", "Extracts request body.", "@Body() ${1:dto}: ${2:CreateDto}", "Body"},
                {"Query", Protocol::CompletionItemKind::Function, "@Query(property?: string): ParameterDecorator", "Extracts query parameters.", "@Query('${1:key}') ${1:key}: string", "Query"},
                {"NestFactory", Protocol::CompletionItemKind::Class, "class NestFactory", "Creates an instance of the Nest application.", "NestFactory.create(${1:AppModule})", "NestFactory"},
                {"HttpException", Protocol::CompletionItemKind::Class, "class HttpException", "Base class for Nest HTTP exceptions.", "new HttpException('${1:Forbidden}', HttpStatus.${2:FORBIDDEN})", "HttpException"},
                {"HttpStatus", Protocol::CompletionItemKind::Enum, "enum HttpStatus", "HTTP status codes enum.", "HttpStatus", "HttpStatus"}
            };
            items.insert(items.end(), s_nest_toplevel.begin(), s_nest_toplevel.end());
        }
    }

    // 8. Next.js
    if (package_name == "next")
    {
        if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_next_toplevel = {
                {"NextResponse", Protocol::CompletionItemKind::Class, "class NextResponse extends Response", "Next.js enhanced response object with helpers.", "NextResponse.json(${1:data})", "NextResponse"},
                {"NextRequest", Protocol::CompletionItemKind::Class, "class NextRequest extends Request", "Next.js enhanced request object.", "NextRequest", "NextRequest"},
                {"useRouter", Protocol::CompletionItemKind::Function, "useRouter(): AppRouterInstance", "Next.js App Router navigation hook.", "const router = useRouter();", "useRouter"},
                {"usePathname", Protocol::CompletionItemKind::Function, "usePathname(): string", "Reads the current URL's pathname.", "const pathname = usePathname();", "usePathname"},
                {"useSearchParams", Protocol::CompletionItemKind::Function, "useSearchParams(): ReadonlyURLSearchParams", "Reads the current URL's search parameters.", "const searchParams = useSearchParams();", "useSearchParams"},
                {"NextPage", Protocol::CompletionItemKind::Interface, "type NextPage<P = {}, IP = P>", "Next.js page component type definition.", "NextPage<${1:Props}>", "NextPage"},
                {"GetServerSideProps", Protocol::CompletionItemKind::Interface, "type GetServerSideProps", "Next.js Pages router server-side data fetching.", "GetServerSideProps", "GetServerSideProps"},
                {"GetStaticProps", Protocol::CompletionItemKind::Interface, "type GetStaticProps", "Next.js Pages router static data fetching.", "GetStaticProps", "GetStaticProps"},
                {"redirect", Protocol::CompletionItemKind::Function, "redirect(url: string): never", "Redirects to another route.", "redirect('${1:/login}');", "redirect"}
            };
            items.insert(items.end(), s_next_toplevel.begin(), s_next_toplevel.end());
        }
    }

    // 9. Fastify
    if (package_name == "fastify")
    {
        if (is_scoped && (scoped_prefix == "fastify" || scoped_prefix == "app" || scoped_prefix == "server"))
        {
            static const std::vector<Protocol::CompletionItem> s_fastify_scoped = {
                {"get", Protocol::CompletionItemKind::Method, "fastify.get(url, [options], handler)", "Registers a GET route.", "get('${1:/path}', async (request, reply) => {\n    $0\n});", "get"},
                {"post", Protocol::CompletionItemKind::Method, "fastify.post(url, [options], handler)", "Registers a POST route.", "post('${1:/path}', async (request, reply) => {\n    $0\n});", "post"},
                {"put", Protocol::CompletionItemKind::Method, "fastify.put(url, [options], handler)", "Registers a PUT route.", "put('${1:/path}', async (request, reply) => {\n    $0\n});", "put"},
                {"delete", Protocol::CompletionItemKind::Method, "fastify.delete(url, [options], handler)", "Registers a DELETE route.", "delete('${1:/path}', async (request, reply) => {\n    $0\n});", "delete"},
                {"listen", Protocol::CompletionItemKind::Method, "fastify.listen({ port, [host] })", "Starts the Fastify server.", "listen({ port: ${1:3000} }, (err, address) => {\n    if (err) throw err;\n});", "listen"},
                {"register", Protocol::CompletionItemKind::Method, "fastify.register(plugin, [options])", "Registers a Fastify plugin.", "register(${1:plugin});", "register"},
                {"decorate", Protocol::CompletionItemKind::Method, "fastify.decorate(name, value)", "Decorates Fastify instance with custom properties.", "decorate('${1:name}', ${2:val});", "decorate"}
            };
            items.insert(items.end(), s_fastify_scoped.begin(), s_fastify_scoped.end());
        }
        else if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_fastify_toplevel = {
                {"fastify", Protocol::CompletionItemKind::Function, "fastify(options?): FastifyInstance", "Creates a Fastify server instance.", "const fastify = require('fastify')({ logger: true });", "fastify"},
                {"FastifyInstance", Protocol::CompletionItemKind::Interface, "interface FastifyInstance", "Fastify server instance interface.", "FastifyInstance", "FastifyInstance"},
                {"FastifyRequest", Protocol::CompletionItemKind::Interface, "interface FastifyRequest", "Fastify HTTP request interface.", "FastifyRequest", "FastifyRequest"},
                {"FastifyReply", Protocol::CompletionItemKind::Interface, "interface FastifyReply", "Fastify HTTP reply interface.", "FastifyReply", "FastifyReply"}
            };
            items.insert(items.end(), s_fastify_toplevel.begin(), s_fastify_toplevel.end());
        }
    }

    // 10. Prisma
    if (package_name == "@prisma/client" || package_name == "prisma")
    {
        if (!is_scoped)
        {
            static const std::vector<Protocol::CompletionItem> s_prisma_toplevel = {
                {"PrismaClient", Protocol::CompletionItemKind::Class, "class PrismaClient", "Client generated from Prisma schema for database queries.", "const prisma = new PrismaClient();", "PrismaClient"},
                {"Prisma", Protocol::CompletionItemKind::Module, "namespace Prisma", "Prisma types and utilities namespace.", "Prisma", "Prisma"}
            };
            items.insert(items.end(), s_prisma_toplevel.begin(), s_prisma_toplevel.end());
        }
    }

    return items;
}

std::vector<Protocol::CompletionItem> NodePackageManager::parse_declaration_file(
    const std::filesystem::path& dts_path,
    const std::string& package_name)
{
    std::error_code ec;
    if (!std::filesystem::exists(dts_path, ec) || !std::filesystem::is_regular_file(dts_path, ec))
    {
        return {};
    }

    std::ifstream file(dts_path);
    if (!file.is_open())
    {
        return {};
    }

    std::vector<Protocol::CompletionItem> items;
    std::unordered_set<std::string> seen;

    std::string line;
    std::size_t lines_read = 0;
    while (std::getline(file, line) && lines_read < 1000)
    {
        ++lines_read;
        // Trim leading spaces
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        const std::string_view l = std::string_view(line).substr(start);

        auto check_and_add = [&](std::string_view prefix, Protocol::CompletionItemKind kind, const char* type_str) {
            if (!l.starts_with(prefix)) return false;
            std::string_view rest = l.substr(prefix.size());
            const auto name_start = rest.find_first_not_of(" \t");
            if (name_start == std::string_view::npos) return false;
            rest = rest.substr(name_start);

            // Extract identifier
            std::size_t id_len = 0;
            while (id_len < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[id_len])) || rest[id_len] == '_' || rest[id_len] == '$'))
            {
                ++id_len;
            }
            if (id_len == 0) return false;
            const std::string id = std::string(rest.substr(0, id_len));

            if (seen.insert(id).second)
            {
                Protocol::CompletionItem item;
                item.label = id;
                item.kind = kind;
                item.detail = std::string("(") + type_str + ") " + package_name + "::" + id;
                item.documentation = line;
                item.insert_text = (kind == Protocol::CompletionItemKind::Function || kind == Protocol::CompletionItemKind::Method)
                                       ? (id + "($0)")
                                       : id;
                item.filter_text = id;
                item.sort_text = "1_" + id;
                items.push_back(std::move(item));
            }
            return true;
        };

        if (check_and_add("export declare function ", Protocol::CompletionItemKind::Function, "Function")) continue;
        if (check_and_add("export function ", Protocol::CompletionItemKind::Function, "Function")) continue;
        if (check_and_add("declare function ", Protocol::CompletionItemKind::Function, "Function")) continue;

        if (check_and_add("export declare class ", Protocol::CompletionItemKind::Class, "Class")) continue;
        if (check_and_add("export class ", Protocol::CompletionItemKind::Class, "Class")) continue;
        if (check_and_add("declare class ", Protocol::CompletionItemKind::Class, "Class")) continue;

        if (check_and_add("export declare interface ", Protocol::CompletionItemKind::Interface, "Interface")) continue;
        if (check_and_add("export interface ", Protocol::CompletionItemKind::Interface, "Interface")) continue;
        if (check_and_add("declare interface ", Protocol::CompletionItemKind::Interface, "Interface")) continue;

        if (check_and_add("export declare type ", Protocol::CompletionItemKind::TypeParameter, "Type")) continue;
        if (check_and_add("export type ", Protocol::CompletionItemKind::TypeParameter, "Type")) continue;
        if (check_and_add("declare type ", Protocol::CompletionItemKind::TypeParameter, "Type")) continue;

        if (check_and_add("export declare const ", Protocol::CompletionItemKind::Variable, "Constant")) continue;
        if (check_and_add("export const ", Protocol::CompletionItemKind::Variable, "Constant")) continue;
        if (check_and_add("declare const ", Protocol::CompletionItemKind::Variable, "Constant")) continue;

        if (check_and_add("export enum ", Protocol::CompletionItemKind::Enum, "Enum")) continue;
        if (check_and_add("declare enum ", Protocol::CompletionItemKind::Enum, "Enum")) continue;
    }

    return items;
}

std::vector<Protocol::CompletionItem> NodePackageManager::get_library_completions(
    const std::filesystem::path& workspace_root,
    bool is_scoped,
    std::string_view scoped_prefix)
{
    const auto manifest = scan_workspace(workspace_root);
    std::vector<Protocol::CompletionItem> all_items;
    std::unordered_set<std::string> seen_labels;

    // Scan detected packages
    for (const auto& pkg : manifest.packages)
    {
        // 1. Check built-in catalog definitions
        const auto catalog = get_catalog_completions_for_package(pkg.name, is_scoped, scoped_prefix);
        for (const auto& item : catalog)
        {
            if (seen_labels.insert(item.label).second)
            {
                all_items.push_back(item);
            }
        }

        // 2. If types_path is present and not scoped, dynamically parse .d.ts
        if (!is_scoped && !pkg.types_path.empty())
        {
            std::vector<Protocol::CompletionItem> dts_items;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_dts_symbol_cache.find(pkg.types_path);
                if (it != m_dts_symbol_cache.end())
                {
                    dts_items = it->second;
                }
            }

            if (dts_items.empty())
            {
                dts_items = parse_declaration_file(pkg.types_path, pkg.name);
                if (!dts_items.empty())
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_dts_symbol_cache[pkg.types_path] = dts_items;
                }
            }

            for (auto& itm : dts_items)
            {
                if (seen_labels.insert(itm.label).second)
                {
                    all_items.push_back(std::move(itm));
                }
            }
        }
    }

    return all_items;
}

void NodePackageManager::clear_cache() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_manifest_cache.clear();
    m_dts_symbol_cache.clear();
}

} // namespace Zenvra::Language::Node
