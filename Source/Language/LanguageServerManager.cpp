#include "Language/LanguageServerManager.h"
#include "Language/Transport/StdioProcessTransport.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/CMake/CMakeLanguageDatabase.h"
#include <unordered_set>

namespace Zenvra::Language
{

LanguageServerManager& LanguageServerManager::instance() noexcept
{
    static LanguageServerManager manager;
    return manager;
}

LanguageServerManager::LanguageServerManager()
{
}

LanguageServerManager::~LanguageServerManager()
{
    shutdown_all();
}

void LanguageServerManager::set_workspace_root(std::filesystem::path root_path)
{
    m_workspace_root = std::move(root_path);
}

Client::ILanguageClient* LanguageServerManager::get_or_start_client_for_file(std::string_view filename)
{
    std::string fname_str(filename);
    if (fname_str.empty() || fname_str.find("Untitled") != std::string_view::npos || fname_str.find("untitled") != std::string_view::npos)
    {
        fname_str = "untitled.cpp";
    }

    const auto* profile = Registry::ServerRegistry::instance().find_profile_for_filename(fname_str);
    if (profile == nullptr)
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_clients_mutex);
    if (auto it = m_clients.find(profile->language_id); it != m_clients.end())
    {
        return it->second.get();
    }

    // Attempt to locate the language server executable (e.g. clangd.exe)
    const std::filesystem::path exe_path = Registry::ServerRegistry::instance().find_executable_in_system(profile->executable_name);
    if (exe_path.empty())
    {
        return nullptr;
    }

    std::vector<std::string> args = profile->default_args;
    if (profile->language_id == "cpp")
    {
        std::error_code ec;
        const std::filesystem::path search_roots[] = {
            m_workspace_root,
            std::filesystem::current_path()
        };

        std::filesystem::path found_compile_dir;
        for (const auto& root : search_roots)
        {
            if (root.empty()) continue;
            const std::filesystem::path direct_candidates[] = {
                root / "compile_commands.json",
                root / "build" / "compile_commands.json",
                root / "build" / "macos-debug" / "compile_commands.json",
                root / "build" / "macos-release" / "compile_commands.json",
                root / "build" / "linux-debug" / "compile_commands.json",
                root / "build" / "linux-release" / "compile_commands.json",
                root / "build" / "windows-x64-clang-ninja-debug" / "compile_commands.json",
                root / "build" / "windows-x64-clang-ninja-release" / "compile_commands.json",
            };
            for (const auto& cand : direct_candidates)
            {
                if (std::filesystem::exists(cand, ec))
                {
                    found_compile_dir = cand.parent_path();
                    break;
                }
            }
            if (!found_compile_dir.empty()) break;

            const std::filesystem::path build_dir = root / "build";
            if (std::filesystem::exists(build_dir, ec) && std::filesystem::is_directory(build_dir, ec))
            {
                for (const auto& entry : std::filesystem::directory_iterator(build_dir, ec))
                {
                    if (entry.is_directory())
                    {
                        const auto sub_cc = entry.path() / "compile_commands.json";
                        if (std::filesystem::exists(sub_cc, ec))
                        {
                            found_compile_dir = entry.path();
                            break;
                        }
                    }
                }
            }
            if (!found_compile_dir.empty()) break;
        }

        if (!found_compile_dir.empty())
        {
            std::erase_if(args, [](const std::string& a) {
                return a.starts_with("--compile-commands-dir");
            });
            args.push_back("--compile-commands-dir=" + found_compile_dir.generic_string());
        }
    }

    auto transport = std::make_unique<Transport::StdioProcessTransport>(
        exe_path,
        std::move(args),
        m_workspace_root
    );

    auto client = std::make_unique<Client::LanguageClient>(
        profile->language_id,
        std::move(transport),
        m_workspace_root
    );

    client->set_diagnostics_handler([this](const std::string& uri, const std::vector<Protocol::Diagnostic>& diags) {
        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            m_document_diagnostics[uri] = diags;
        }
        if (m_diagnostics_callback)
        {
            m_diagnostics_callback(uri, diags);
        }
    });

    if (client->start())
    {
        auto* ptr = client.get();
        m_clients[profile->language_id] = std::move(client);
        return ptr;
    }

    return nullptr;
}

static std::string determine_lsp_language_id(std::string_view filename, const std::string& default_lang_id)
{
    const std::filesystem::path p(filename);
    const std::string ext = p.extension().string();
    if (ext == ".tsx") return "typescriptreact";
    if (ext == ".jsx") return "javascriptreact";
    if (ext == ".ts" || ext == ".mts" || ext == ".cts") return "typescript";
    if (ext == ".js" || ext == ".mjs" || ext == ".cjs") return "javascript";
    if (ext == ".html" || ext == ".htm" || ext == ".xhtml") return "html";
    return default_lang_id;
}

void LanguageServerManager::on_document_opened(
    const std::string& uri,
    std::string_view filename,
    int version,
    std::string_view content)
{
    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        const auto* profile = Registry::ServerRegistry::instance().find_profile_for_filename(filename);
        const std::string base_lang_id = profile != nullptr ? profile->language_id : "plaintext";
        const std::string lang_id = determine_lsp_language_id(filename, base_lang_id);

        client->did_open(uri, lang_id, version, content);
        request_semantic_tokens(uri, filename);
    }
}

void LanguageServerManager::on_document_changed(
    const std::string& uri,
    std::string_view filename,
    int version,
    std::string_view content)
{
    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        client->did_change(uri, version, content);
        request_semantic_tokens(uri, filename);
    }
}

void LanguageServerManager::on_document_saved(const std::string& uri, std::string_view filename)
{
    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        client->did_save(uri);
    }
}

void LanguageServerManager::on_document_closed(const std::string& uri, std::string_view filename)
{
    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        client->did_close(uri);
    }
    m_semantic_tokens_manager.clear_document_tokens(uri);
}

namespace
{

std::vector<Protocol::CompletionItem> get_jetbrains_cpp_templates()
{
    return {
        Protocol::CompletionItem{
            .label = "struct",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "(JetBrains Template) struct Name { ... };",
            .documentation = "Generates a C++ struct definition with body and semicolon.",
            .insert_text = "struct ${1:Name}\n{\n    $0\n};",
            .filter_text = "struct"
        },
        Protocol::CompletionItem{
            .label = "class",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "(JetBrains Template) class Name { public: ... };",
            .documentation = "Generates a C++ class definition with constructor, destructor and private sections.",
            .insert_text = "class ${1:Name}\n{\npublic:\n    ${1:Name}();\n    ~${1:Name}();\n\nprivate:\n    $0\n};",
            .filter_text = "class"
        },
        Protocol::CompletionItem{
            .label = "namespace",
            .kind = Protocol::CompletionItemKind::Module,
            .detail = "(JetBrains Template) namespace Name { ... } // namespace Name",
            .documentation = "Generates a C++ namespace block with matching closing comment.",
            .insert_text = "namespace ${1:Name}\n{\n\n$0\n\n} // namespace ${1:Name}",
            .filter_text = "namespace"
        },
        Protocol::CompletionItem{
            .label = "ns",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) namespace Name { ... }",
            .documentation = "Shortcut to generate a C++ namespace block with closing comment.",
            .insert_text = "namespace ${1:Name}\n{\n\n$0\n\n} // namespace ${1:Name}",
            .filter_text = "ns"
        },
        Protocol::CompletionItem{
            .label = "enum class",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "(JetBrains Template) enum class Name : uint32_t { ... };",
            .documentation = "Generates a strongly typed enum class definition.",
            .insert_text = "enum class ${1:Name}\n{\n    $0\n};",
            .filter_text = "enum class"
        },
        Protocol::CompletionItem{
            .label = "enum",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "(JetBrains Template) enum Name { ... };",
            .documentation = "Generates an enum definition.",
            .insert_text = "enum ${1:Name}\n{\n    $0\n};",
            .filter_text = "enum"
        },
        Protocol::CompletionItem{
            .label = "interface",
            .kind = Protocol::CompletionItemKind::Interface,
            .detail = "(JetBrains Template) struct IInterface { virtual ~IInterface() = default; ... };",
            .documentation = "Generates an abstract C++ interface with virtual default destructor.",
            .insert_text = "struct I${1:Interface}\n{\n    virtual ~I${1:Interface}() = default;\n    $0\n};",
            .filter_text = "interface"
        },
        Protocol::CompletionItem{
            .label = "template struct",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) template <typename T> struct Name { ... };",
            .documentation = "Generates a templated struct definition.",
            .insert_text = "template <typename ${1:T}>\nstruct ${2:Name}\n{\n    $0\n};",
            .filter_text = "template struct"
        },
        Protocol::CompletionItem{
            .label = "template class",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) template <typename T> class Name { ... };",
            .documentation = "Generates a templated class definition.",
            .insert_text = "template <typename ${1:T}>\nclass ${2:Name}\n{\npublic:\n    ${2:Name}();\n    ~${2:Name}();\n\nprivate:\n    $0\n};",
            .filter_text = "template class"
        },
        Protocol::CompletionItem{
            .label = "template function",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) template <typename T> void fn() { ... }",
            .documentation = "Generates a templated function signature and body.",
            .insert_text = "template <typename ${1:T}>\n${2:void} ${3:function_name}(${4:/*params*/})\n{\n    $0\n}",
            .filter_text = "template function"
        },
        Protocol::CompletionItem{
            .label = "fori",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) for (size_t i = 0; i < count; ++i)",
            .documentation = "Generates an index-based standard for loop.",
            .insert_text = "for (std::size_t ${1:i} = 0; ${1:i} < ${2:count}; ++${1:i})\n{\n    $0\n}",
            .filter_text = "fori"
        },
        Protocol::CompletionItem{
            .label = "foreach",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) for (const auto& item : collection)",
            .documentation = "Generates a range-based for loop.",
            .insert_text = "for (const auto& ${1:item} : ${2:collection})\n{\n    $0\n}",
            .filter_text = "foreach"
        },
        Protocol::CompletionItem{
            .label = "iter",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) for (auto it = c.begin(); it != c.end(); ++it)",
            .documentation = "Generates an iterator-based loop.",
            .insert_text = "for (auto ${1:it} = ${2:collection}.begin(); ${1:it} != ${2:collection}.end(); ++${1:it})\n{\n    $0\n}",
            .filter_text = "iter"
        },
        Protocol::CompletionItem{
            .label = "switch",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) switch (condition) { case ...: break; default: break; }",
            .documentation = "Generates a switch-case statement with default branch.",
            .insert_text = "switch (${1:condition})\n{\ncase ${2:value}:\n    $0\n    break;\ndefault:\n    break;\n}",
            .filter_text = "switch"
        },
        Protocol::CompletionItem{
            .label = "try",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) try { ... } catch (const std::exception& e) { ... }",
            .documentation = "Generates a try-catch block catching std::exception.",
            .insert_text = "try\n{\n    $0\n}\ncatch (const std::exception& ${1:e})\n{\n}",
            .filter_text = "try"
        },
        Protocol::CompletionItem{
            .label = "lambda",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) [&]() { ... }",
            .documentation = "Generates a C++ lambda expression.",
            .insert_text = "[${1:&}](${2:/*params*/})\n{\n    $0\n}",
            .filter_text = "lambda"
        },
        Protocol::CompletionItem{
            .label = "main",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) int main(int argc, char* argv[])",
            .documentation = "Generates the standard C++ application entry point.",
            .insert_text = "int main(int argc, char* argv[])\n{\n    $0\n    return 0;\n}",
            .filter_text = "main"
        },
        Protocol::CompletionItem{
            .label = "guard",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) #pragma once",
            .documentation = "Generates a modern include guard directive.",
            .insert_text = "#pragma once\n\n$0",
            .filter_text = "guard"
        },
        Protocol::CompletionItem{
            .label = "#pragma once",
            .kind = Protocol::CompletionItemKind::Keyword,
            .detail = "(JetBrains Template) #pragma once",
            .documentation = "Header guard directive.",
            .insert_text = "#pragma once\n\n$0",
            .filter_text = "#pragma once"
        },
        Protocol::CompletionItem{
            .label = "#include <header>",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) #include <header>",
            .documentation = "System header include directive.",
            .insert_text = "#include <${1:header}>$0",
            .filter_text = "#include <header>"
        },
        Protocol::CompletionItem{
            .label = "#include \"header\"",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) #include \"header\"",
            .documentation = "Local project header include directive.",
            .insert_text = "#include \"${1:header}\"$0",
            .filter_text = "#include \"header\""
        },
        Protocol::CompletionItem{
            .label = "#import <header>",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) #import <header>",
            .documentation = "System module import directive.",
            .insert_text = "#import <${1:header}>$0",
            .filter_text = "#import <header>"
        },
        Protocol::CompletionItem{
            .label = "#import \"header\"",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) #import \"header\"",
            .documentation = "Local module import directive.",
            .insert_text = "#import \"${1:header}\"$0",
            .filter_text = "#import \"header\""
        },
        Protocol::CompletionItem{
            .label = "singleton",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) static ClassName& instance() noexcept;",
            .documentation = "Generates thread-safe Meyer's Singleton method pattern.",
            .insert_text = "static ${1:ClassName}& instance() noexcept\n{\n    static ${1:ClassName} s_instance;\n    return s_instance;\n}",
            .filter_text = "singleton"
        },
        Protocol::CompletionItem{
            .label = "pimpl",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "(JetBrains Template) PImpl idiom pointer & struct",
            .documentation = "Generates standard Pointer-to-Implementation (PImpl) declaration.",
            .insert_text = "struct Impl;\nstd::unique_ptr<Impl> m_impl;",
            .filter_text = "pimpl"
        }
    };
}



std::vector<Protocol::CompletionItem> get_jetbrains_java_templates()
{
    return {
        Protocol::CompletionItem{
            .label = "sout",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "System.out.println(...);",
            .documentation = "Prints a string to standard output.",
            .insert_text = "System.out.println($0);",
            .filter_text = "sout"
        },
        Protocol::CompletionItem{
            .label = "psvm",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "public static void main(String[] args)",
            .documentation = "Standard Java application main method entry point.",
            .insert_text = "public static void main(String[] args)\n{\n    $0\n}",
            .filter_text = "psvm"
        },
        Protocol::CompletionItem{
            .label = "class",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "public class Name { ... }",
            .documentation = "Generates a public Java class definition.",
            .insert_text = "public class ${1:Name}\n{\n    $0\n}",
            .filter_text = "class"
        },
        Protocol::CompletionItem{
            .label = "interface",
            .kind = Protocol::CompletionItemKind::Interface,
            .detail = "public interface Name { ... }",
            .documentation = "Generates a Java interface definition.",
            .insert_text = "public interface ${1:Name}\n{\n    $0\n}",
            .filter_text = "interface"
        },
        Protocol::CompletionItem{
            .label = "record",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "public record Name(...) { }",
            .documentation = "Generates an immutable Java record definition.",
            .insert_text = "public record ${1:Name}(${2:/*fields*/})\n{\n    $0\n}",
            .filter_text = "record"
        },
        Protocol::CompletionItem{
            .label = "@SpringBootApplication",
            .kind = Protocol::CompletionItemKind::Keyword,
            .detail = "Spring Boot Main Application",
            .documentation = "Convenience annotation that adds @Configuration, @EnableAutoConfiguration, and @ComponentScan.",
            .insert_text = "@SpringBootApplication\npublic class ${1:Application}\n{\n    public static void main(String[] args)\n    {\n        SpringApplication.run(${1:Application}.class, args);\n    }\n}",
            .filter_text = "@SpringBootApplication"
        },
        Protocol::CompletionItem{
            .label = "@RestController",
            .kind = Protocol::CompletionItemKind::Keyword,
            .detail = "Spring Rest Controller",
            .documentation = "Defines a RESTful web controller with @ResponseBody.",
            .insert_text = "@RestController\n@RequestMapping(\"/${1:api}\")\npublic class ${2:Controller}\n{\n    $0\n}",
            .filter_text = "@RestController"
        },
        Protocol::CompletionItem{
            .label = "@GetMapping",
            .kind = Protocol::CompletionItemKind::Keyword,
            .detail = "@GetMapping(\"/path\")",
            .documentation = "Spring HTTP GET mapping endpoint.",
            .insert_text = "@GetMapping(\"/${1:path}\")\npublic ResponseEntity<${2:String}> get${3:Endpoint}()\n{\n    return ResponseEntity.ok($0);\n}",
            .filter_text = "@GetMapping"
        },
        Protocol::CompletionItem{
            .label = "@PostMapping",
            .kind = Protocol::CompletionItemKind::Keyword,
            .detail = "@PostMapping(\"/path\")",
            .documentation = "Spring HTTP POST mapping endpoint.",
            .insert_text = "@PostMapping(\"/${1:path}\")\npublic ResponseEntity<${2:Void}> post${3:Endpoint}(@RequestBody ${4:Request} request)\n{\n    $0\n    return ResponseEntity.ok().build();\n}",
            .filter_text = "@PostMapping"
        }
    };
}

std::vector<Protocol::CompletionItem> get_jetbrains_meson_templates()
{
    return {
        Protocol::CompletionItem{
            .label = "project",
            .kind = Protocol::CompletionItemKind::Function,
            .detail = "project('name', 'cpp', version: '1.0.0')",
            .documentation = "Declares a Meson project name, languages, and version.",
            .insert_text = "project('${1:project_name}', '${2:cpp}',\n  version: '${3:1.0.0}',\n  default_options: ['warning_level=3', 'cpp_std=c++20']\n)\n",
            .filter_text = "project"
        },
        Protocol::CompletionItem{
            .label = "executable",
            .kind = Protocol::CompletionItemKind::Function,
            .detail = "executable('exe_name', sources, ...)",
            .documentation = "Builds an executable from sources.",
            .insert_text = "executable('${1:exe_name}',\n  sources: ['${2:main.cpp}'],\n  dependencies: [${0}]\n)\n",
            .filter_text = "executable"
        },
        Protocol::CompletionItem{
            .label = "dependency",
            .kind = Protocol::CompletionItemKind::Function,
            .detail = "dependency('name', required: true)",
            .documentation = "Finds external package dependencies with pkg-config/CMake.",
            .insert_text = "${1:dep} = dependency('${2:name}', required: true)\n",
            .filter_text = "dependency"
        }
    };
}

std::vector<Protocol::CompletionItem> get_jetbrains_rust_templates()
{
    return {
        Protocol::CompletionItem{
            .label = "fn main",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "fn main() { ... }",
            .documentation = "Standard Rust application main function entry point.",
            .insert_text = "fn main() {\n    $0\n}",
            .sort_text = "1_fn_main",
            .filter_text = "fn main"
        },
        Protocol::CompletionItem{
            .label = "fn",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "fn name(...) -> Ret { ... }",
            .documentation = "Defines a Rust function.",
            .insert_text = "fn ${1:name}(${2}) -> ${3:()} {\n    $0\n}",
            .sort_text = "1_fn",
            .filter_text = "fn"
        },
        Protocol::CompletionItem{
            .label = "pub fn",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "pub fn name(...) -> Ret { ... }",
            .documentation = "Defines a public Rust function.",
            .insert_text = "pub fn ${1:name}(${2}) -> ${3:()} {\n    $0\n}",
            .sort_text = "1_pub_fn",
            .filter_text = "pub fn"
        },
        Protocol::CompletionItem{
            .label = "struct",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "#[derive(Debug, Clone)] pub struct Name { ... }",
            .documentation = "Defines a public Rust struct with common derives.",
            .insert_text = "#[derive(Debug, Clone)]\npub struct ${1:Name} {\n    $0\n}",
            .sort_text = "1_struct",
            .filter_text = "struct"
        },
        Protocol::CompletionItem{
            .label = "enum",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "#[derive(Debug, Clone, PartialEq, Eq)] pub enum Name { ... }",
            .documentation = "Defines a Rust enum definition.",
            .insert_text = "#[derive(Debug, Clone, PartialEq, Eq)]\npub enum ${1:Name} {\n    $0\n}",
            .sort_text = "1_enum",
            .filter_text = "enum"
        },
        Protocol::CompletionItem{
            .label = "impl",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "impl Type { ... }",
            .documentation = "Implements methods for a Rust type.",
            .insert_text = "impl ${1:Type} {\n    $0\n}",
            .sort_text = "1_impl",
            .filter_text = "impl"
        },
        Protocol::CompletionItem{
            .label = "impl Trait",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "impl Trait for Type { ... }",
            .documentation = "Implements a trait for a given type.",
            .insert_text = "impl ${1:Trait} for ${2:Type} {\n    $0\n}",
            .sort_text = "1_impl_trait",
            .filter_text = "impl Trait"
        },
        Protocol::CompletionItem{
            .label = "println!",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "println!(\"{}\", ...);",
            .documentation = "Prints formatted text to standard output with a newline.",
            .insert_text = "println!(\"${1:{}}\", ${0});",
            .sort_text = "1_println",
            .filter_text = "println!"
        },
        Protocol::CompletionItem{
            .label = "eprintln!",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "eprintln!(\"{}\", ...);",
            .documentation = "Prints formatted text to standard error with a newline.",
            .insert_text = "eprintln!(\"${1:{}}\", ${0});",
            .sort_text = "1_eprintln",
            .filter_text = "eprintln!"
        },
        Protocol::CompletionItem{
            .label = "format!",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "format!(\"{}\", ...)",
            .documentation = "Creates a String using interpolation of runtime expressions.",
            .insert_text = "format!(\"${1:{}}\", ${0})",
            .sort_text = "1_format",
            .filter_text = "format!"
        },
        Protocol::CompletionItem{
            .label = "vec!",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "vec![...]",
            .documentation = "Creates a `Vec` containing the given elements.",
            .insert_text = "vec![${0}];",
            .sort_text = "1_vec",
            .filter_text = "vec!"
        },
        Protocol::CompletionItem{
            .label = "match",
            .kind = Protocol::CompletionItemKind::Keyword,
            .detail = "match val { ... }",
            .documentation = "Pattern matching control flow expression.",
            .insert_text = "match ${1:val} {\n    ${2:pattern} => ${0},\n}",
            .sort_text = "1_match",
            .filter_text = "match"
        },
        Protocol::CompletionItem{
            .label = "if let",
            .kind = Protocol::CompletionItemKind::Keyword,
            .detail = "if let Some(val) = opt { ... }",
            .documentation = "Conditionally matches a single pattern.",
            .insert_text = "if let Some(${1:val}) = ${2:opt} {\n    $0\n}",
            .sort_text = "1_if_let",
            .filter_text = "if let"
        },
        Protocol::CompletionItem{
            .label = "mod tests",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "#[cfg(test)] mod tests { ... }",
            .documentation = "Standard Rust unit test module block.",
            .insert_text = "#[cfg(test)]\nmod tests {\n    use super::*;\n\n    #[test]\n    fn test_${1:feature}() {\n        assert!(${0});\n    }\n}",
            .sort_text = "1_mod_tests",
            .filter_text = "mod tests"
        },
        Protocol::CompletionItem{
            .label = "test",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "#[test] fn test_name() { ... }",
            .documentation = "Defines a single unit test function.",
            .insert_text = "#[test]\nfn test_${1:feature}() {\n    assert!(${0});\n}",
            .sort_text = "1_test",
            .filter_text = "test"
        },
        Protocol::CompletionItem{
            .label = "derive",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "#[derive(Debug, Clone, ...)]",
            .documentation = "Macro attribute to derive traits on structs or enums.",
            .insert_text = "#[derive(${0:Debug, Clone, PartialEq})]",
            .sort_text = "1_derive",
            .filter_text = "derive"
        },
        Protocol::CompletionItem{
            .label = "tokio::main",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "#[tokio::main] async fn main() { ... }",
            .documentation = "Asynchronous main function for Tokio runtime applications.",
            .insert_text = "#[tokio::main]\nasync fn main() -> Result<(), Box<dyn std::error::Error>> {\n    $0\n    Ok(())\n}",
            .sort_text = "1_tokio_main",
            .filter_text = "tokio::main"
        }
    };
}

std::vector<Protocol::CompletionItem> get_jetbrains_typescript_templates(bool is_jsx)
{
    std::vector<Protocol::CompletionItem> items = {
        Protocol::CompletionItem{
            .label = "import",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "import { item } from 'module';",
            .documentation = "Imports named bindings from a module.",
            .insert_text = "import { ${1:item} } from '${2:module}';$0",
            .sort_text = "0_import",
            .filter_text = "import"
        },
        Protocol::CompletionItem{
            .label = "import default",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "import name from 'module';",
            .documentation = "Imports the default export from a module.",
            .insert_text = "import ${1:name} from '${2:module}';$0",
            .sort_text = "0_import_default",
            .filter_text = "import default"
        },
        Protocol::CompletionItem{
            .label = "import * as",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "import * as name from 'module';",
            .documentation = "Imports module namespace object.",
            .insert_text = "import * as ${1:name} from '${2:module}';$0",
            .sort_text = "0_import_all",
            .filter_text = "import * as"
        },
        Protocol::CompletionItem{
            .label = "import type",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "import type { Type } from 'module';",
            .documentation = "Type-only import statement for TypeScript.",
            .insert_text = "import type { ${1:Type} } from '${2:module}';$0",
            .sort_text = "0_import_type",
            .filter_text = "import type"
        },
        Protocol::CompletionItem{
            .label = "export const",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "export const name = value;",
            .documentation = "Exports a constant variable.",
            .insert_text = "export const ${1:name} = ${2:value};$0",
            .sort_text = "1_export_const",
            .filter_text = "export const"
        },
        Protocol::CompletionItem{
            .label = "export function",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "export function name(...) { ... }",
            .documentation = "Exports a named function.",
            .insert_text = "export function ${1:name}(${2}): ${3:void} {\n    $0\n}",
            .sort_text = "1_export_function",
            .filter_text = "export function"
        },
        Protocol::CompletionItem{
            .label = "export default",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "export default ...;",
            .documentation = "Defines the default export for a module.",
            .insert_text = "export default ${1:name};$0",
            .sort_text = "1_export_default",
            .filter_text = "export default"
        },
        Protocol::CompletionItem{
            .label = "interface",
            .kind = Protocol::CompletionItemKind::Interface,
            .detail = "interface Name { ... }",
            .documentation = "Defines a TypeScript interface shape.",
            .insert_text = "interface ${1:Name} {\n    ${2:id}: ${3:string};\n    $0\n}",
            .sort_text = "1_interface",
            .filter_text = "interface"
        },
        Protocol::CompletionItem{
            .label = "type",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "type Name = ...;",
            .documentation = "Defines a TypeScript type alias.",
            .insert_text = "type ${1:Name} = ${2:string};$0",
            .sort_text = "1_type",
            .filter_text = "type"
        },
        Protocol::CompletionItem{
            .label = "enum",
            .kind = Protocol::CompletionItemKind::Enum,
            .detail = "enum Name { ... }",
            .documentation = "Defines a TypeScript enum.",
            .insert_text = "enum ${1:Name} {\n    ${2:First} = \"${3:FIRST}\",\n    $0\n}",
            .sort_text = "1_enum",
            .filter_text = "enum"
        },
        Protocol::CompletionItem{
            .label = "class",
            .kind = Protocol::CompletionItemKind::Class,
            .detail = "class Name { ... }",
            .documentation = "Defines an ES6 / TypeScript class.",
            .insert_text = "class ${1:Name} {\n    constructor(${2}) {\n        $0\n    }\n}",
            .sort_text = "1_class",
            .filter_text = "class"
        },
        Protocol::CompletionItem{
            .label = "async function",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "async function name(...): Promise<T> { ... }",
            .documentation = "Defines an asynchronous function returning a Promise.",
            .insert_text = "async function ${1:name}(${2}): Promise<${3:void}> {\n    $0\n}",
            .sort_text = "1_async_function",
            .filter_text = "async function"
        },
        Protocol::CompletionItem{
            .label = "const fn",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "const name = (...) => { ... }",
            .documentation = "Defines an arrow function constant.",
            .insert_text = "const ${1:name} = (${2}): ${3:void} => {\n    $0\n};",
            .sort_text = "1_const_fn",
            .filter_text = "const fn"
        },
        Protocol::CompletionItem{
            .label = "async arrow",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "const name = async (...) => { ... }",
            .documentation = "Defines an asynchronous arrow function.",
            .insert_text = "const ${1:name} = async (${2}): Promise<${3:void}> => {\n    $0\n};",
            .sort_text = "1_async_arrow",
            .filter_text = "async arrow"
        },
        Protocol::CompletionItem{
            .label = "clog",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "console.log(...)",
            .documentation = "Outputs message to the console.",
            .insert_text = "console.log(${0});",
            .sort_text = "0_clog",
            .filter_text = "clog"
        },
        Protocol::CompletionItem{
            .label = "cerr",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "console.error(...)",
            .documentation = "Outputs error message to the console.",
            .insert_text = "console.error(${0});",
            .sort_text = "0_cerr",
            .filter_text = "cerr"
        },
        Protocol::CompletionItem{
            .label = "trycatch",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "try { ... } catch (error) { ... }",
            .documentation = "Try-catch error handling block.",
            .insert_text = "try {\n    $1\n} catch (error) {\n    console.error(error);\n    $0\n}",
            .sort_text = "1_trycatch",
            .filter_text = "trycatch"
        },
        Protocol::CompletionItem{
            .label = "forof",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "for (const item of items) { ... }",
            .documentation = "Iterates over iterable collections.",
            .insert_text = "for (const ${1:item} of ${2:items}) {\n    $0\n}",
            .sort_text = "1_forof",
            .filter_text = "forof"
        },
        Protocol::CompletionItem{
            .label = "forin",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "for (const key in object) { ... }",
            .documentation = "Iterates over object property keys.",
            .insert_text = "for (const ${1:key} in ${2:object}) {\n    $0\n}",
            .sort_text = "1_forin",
            .filter_text = "forin"
        },
        Protocol::CompletionItem{
            .label = "map",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "array.map((item) => ...)",
            .documentation = "Maps array items through a transform function.",
            .insert_text = "${1:array}.map((${2:item}) => ${0})",
            .sort_text = "1_map",
            .filter_text = "map"
        },
        Protocol::CompletionItem{
            .label = "filter",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "array.filter((item) => ...)",
            .documentation = "Filters array items with a predicate.",
            .insert_text = "${1:array}.filter((${2:item}) => ${0})",
            .sort_text = "1_filter",
            .filter_text = "filter"
        },
        Protocol::CompletionItem{
            .label = "reduce",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "array.reduce((acc, cur) => ..., initial)",
            .documentation = "Reduces array to a single accumulator value.",
            .insert_text = "${1:array}.reduce((${2:acc}, ${3:cur}) => ${0}, ${4:initial})",
            .sort_text = "1_reduce",
            .filter_text = "reduce"
        },
        Protocol::CompletionItem{
            .label = "promise",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "new Promise<T>((resolve, reject) => { ... })",
            .documentation = "Creates a new asynchronous Promise.",
            .insert_text = "new Promise<${1:void}>((resolve, reject) => {\n    $0\n})",
            .sort_text = "1_promise",
            .filter_text = "promise"
        }
    };

    if (is_jsx)
    {
        items.push_back(Protocol::CompletionItem{
            .label = "rfc",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "React.FC Component Template",
            .documentation = "Generates a React functional component with props interface.",
            .insert_text = "import React from 'react';\n\ninterface ${1:Component}Props {\n    ${2:title}?: string;\n}\n\nexport const ${1:Component}: React.FC<${1:Component}Props> = ({\n    ${2:title},\n}) => {\n    return (\n        <div>\n            $0\n        </div>\n    );\n};\n",
            .sort_text = "0_rfc",
            .filter_text = "rfc"
        });
        items.push_back(Protocol::CompletionItem{
            .label = "useState",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "const [state, setState] = useState(initial);",
            .documentation = "React useState state hook declaration.",
            .insert_text = "const [${1:state}, set${2:State}] = useState<${3:string}>(${4:\"\"});$0",
            .sort_text = "0_useState",
            .filter_text = "useState"
        });
        items.push_back(Protocol::CompletionItem{
            .label = "useEffect",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "useEffect(() => { ... }, [deps]);",
            .documentation = "React useEffect side-effect hook declaration.",
            .insert_text = "useEffect(() => {\n    $0\n    return () => {\n        // cleanup\n    };\n}, [${1}]);",
            .sort_text = "0_useEffect",
            .filter_text = "useEffect"
        });
        items.push_back(Protocol::CompletionItem{
            .label = "useCallback",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "const cb = useCallback((...) => { ... }, [deps]);",
            .documentation = "React useCallback memoized callback hook.",
            .insert_text = "const ${1:callback} = useCallback((${2}) => {\n    $0\n}, [${3}]);",
            .sort_text = "0_useCallback",
            .filter_text = "useCallback"
        });
        items.push_back(Protocol::CompletionItem{
            .label = "useMemo",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "const value = useMemo(() => { ... }, [deps]);",
            .documentation = "React useMemo memoized computation hook.",
            .insert_text = "const ${1:memoized} = useMemo(() => {\n    return $0;\n}, [${2}]);",
            .sort_text = "0_useMemo",
            .filter_text = "useMemo"
        });
        items.push_back(Protocol::CompletionItem{
            .label = "useRef",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "const ref = useRef<Element>(null);",
            .documentation = "React useRef mutable reference hook.",
            .insert_text = "const ${1:ref} = useRef<${2:HTMLDivElement}>(null);$0",
            .sort_text = "0_useRef",
            .filter_text = "useRef"
        });
    }

    return items;
}

std::vector<Protocol::CompletionItem> get_workspace_ts_imports(
    const std::filesystem::path& workspace_root,
    std::string_view current_filename)
{
    std::vector<Protocol::CompletionItem> results;
    std::unordered_set<std::string> seen;

    std::filesystem::path base_dir;
    if (!current_filename.empty())
    {
        base_dir = std::filesystem::path(current_filename).parent_path();
    }
    if (base_dir.empty() || !std::filesystem::exists(base_dir))
    {
        base_dir = workspace_root.empty() ? std::filesystem::current_path() : workspace_root;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(base_dir, ec))
    {
        if (entry.is_regular_file())
        {
            const auto ext = entry.path().extension().string();
            if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx" || ext == ".json" || ext == ".mts" || ext == ".cts")
            {
                std::string stem = entry.path().stem().string();
                if (stem == "index")
                {
                    stem = "./";
                }
                else
                {
                    stem = "./" + stem;
                }
                if (seen.insert(stem).second)
                {
                    results.push_back(Protocol::CompletionItem{
                        .label = stem,
                        .kind = Protocol::CompletionItemKind::Module,
                        .detail = "Local module: " + entry.path().filename().string(),
                        .documentation = "Relative import path: " + stem,
                        .insert_text = stem,
                        .sort_text = "0_" + stem,
                        .filter_text = stem
                    });
                }
            }
        }
        else if (entry.is_directory())
        {
            const std::string dir_name = "./" + entry.path().filename().string();
            if (dir_name != "./node_modules" && dir_name != "./.git" && dir_name != "./build" && dir_name != "./dist")
            {
                if (seen.insert(dir_name).second)
                {
                    results.push_back(Protocol::CompletionItem{
                        .label = dir_name,
                        .kind = Protocol::CompletionItemKind::Folder,
                        .detail = "Folder: " + entry.path().filename().string(),
                        .documentation = "Relative folder import path: " + dir_name,
                        .insert_text = dir_name,
                        .sort_text = "1_" + dir_name,
                        .filter_text = dir_name
                    });
                }
            }
        }
    }

    return results;
}

std::vector<Protocol::CompletionItem> get_jetbrains_html_templates()
{
    return {
        Protocol::CompletionItem{
            .label = "!",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "HTML5 Boilerplate Document",
            .documentation = "Generates a complete modern HTML5 starter boilerplate structure.",
            .insert_text = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    <meta charset=\"UTF-8\">\n    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n    <title>${1:Document}</title>\n</head>\n<body>\n    $0\n</body>\n</html>",
            .sort_text = "0_0_boilerplate",
            .filter_text = "!"
        },
        Protocol::CompletionItem{
            .label = "html5",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<!DOCTYPE html> ... </html>",
            .documentation = "Standard HTML5 skeleton with viewport and stylesheet link.",
            .insert_text = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    <meta charset=\"UTF-8\">\n    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n    <title>${1:Document}</title>\n    <link rel=\"stylesheet\" href=\"${2:style.css}\">\n</head>\n<body>\n    $0\n    <script src=\"${3:main.js}\"></script>\n</body>\n</html>",
            .sort_text = "0_0_html5",
            .filter_text = "html5"
        },
        Protocol::CompletionItem{
            .label = "link:css",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<link rel=\"stylesheet\" href=\"style.css\">",
            .documentation = "Links an external CSS stylesheet.",
            .insert_text = "<link rel=\"stylesheet\" href=\"${1:style.css}\">",
            .sort_text = "0_link_css",
            .filter_text = "link:css"
        },
        Protocol::CompletionItem{
            .label = "script:src",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<script src=\"app.js\"></script>",
            .documentation = "Imports an external JavaScript script file.",
            .insert_text = "<script src=\"${1:app.js}\"></script>",
            .sort_text = "0_script_src",
            .filter_text = "script:src"
        },
        Protocol::CompletionItem{
            .label = "div",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<div class=\"...\">...</div>",
            .documentation = "Standard HTML container division element.",
            .insert_text = "<div class=\"${1:container}\">\n    $0\n</div>",
            .sort_text = "1_div",
            .filter_text = "div"
        },
        Protocol::CompletionItem{
            .label = "a",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<a href=\"...\">...</a>",
            .documentation = "Hyperlink anchor element.",
            .insert_text = "<a href=\"${1:#}\"${2: target=\"_blank\"}>${3:Link}</a>$0",
            .sort_text = "1_a",
            .filter_text = "a"
        },
        Protocol::CompletionItem{
            .label = "button",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<button type=\"button\">...</button>",
            .documentation = "Clickable HTML button element.",
            .insert_text = "<button type=\"${1:button}\" class=\"${2:btn}\">${3:Click me}</button>$0",
            .sort_text = "1_button",
            .filter_text = "button"
        },
        Protocol::CompletionItem{
            .label = "form:post",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<form action=\"...\" method=\"POST\">",
            .documentation = "HTML Form with POST method.",
            .insert_text = "<form action=\"${1}\" method=\"POST\">\n    $0\n</form>",
            .sort_text = "1_form_post",
            .filter_text = "form:post"
        },
        Protocol::CompletionItem{
            .label = "form:get",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<form action=\"...\" method=\"GET\">",
            .documentation = "HTML Form with GET method.",
            .insert_text = "<form action=\"${1}\" method=\"GET\">\n    $0\n</form>",
            .sort_text = "1_form_get",
            .filter_text = "form:get"
        },
        Protocol::CompletionItem{
            .label = "input:text",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<input type=\"text\" name=\"...\" placeholder=\"...\">",
            .documentation = "Single-line text input field.",
            .insert_text = "<input type=\"text\" name=\"${1:name}\" id=\"${1:name}\" placeholder=\"${2:Enter text...}\">",
            .sort_text = "1_input_text",
            .filter_text = "input:text"
        },
        Protocol::CompletionItem{
            .label = "input:password",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<input type=\"password\" name=\"...\">",
            .documentation = "Password input field.",
            .insert_text = "<input type=\"password\" name=\"${1:password}\" id=\"${1:password}\" placeholder=\"${2:Password}\">",
            .sort_text = "1_input_password",
            .filter_text = "input:password"
        },
        Protocol::CompletionItem{
            .label = "input:email",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<input type=\"email\" name=\"...\">",
            .documentation = "Email input field with built-in validation.",
            .insert_text = "<input type=\"email\" name=\"${1:email}\" id=\"${1:email}\" placeholder=\"${2:name@example.com}\">",
            .sort_text = "1_input_email",
            .filter_text = "input:email"
        },
        Protocol::CompletionItem{
            .label = "input:checkbox",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<input type=\"checkbox\" name=\"...\">",
            .documentation = "Checkbox selection input.",
            .insert_text = "<label>\n    <input type=\"checkbox\" name=\"${1:agree}\" id=\"${1:agree}\">\n    ${2:Label}\n</label>",
            .sort_text = "1_input_checkbox",
            .filter_text = "input:checkbox"
        },
        Protocol::CompletionItem{
            .label = "input:submit",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<input type=\"submit\" value=\"Submit\">",
            .documentation = "Form submit button input.",
            .insert_text = "<input type=\"submit\" value=\"${1:Submit}\">",
            .sort_text = "1_input_submit",
            .filter_text = "input:submit"
        },
        Protocol::CompletionItem{
            .label = "img",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<img src=\"...\" alt=\"...\">",
            .documentation = "Embedded image element.",
            .insert_text = "<img src=\"${1:image.png}\" alt=\"${2:description}\" width=\"${3:100}\" height=\"${4:100}\">",
            .sort_text = "1_img",
            .filter_text = "img"
        },
        Protocol::CompletionItem{
            .label = "table",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<table><thead>...</thead><tbody>...</tbody></table>",
            .documentation = "Complete HTML table structure with headers and row.",
            .insert_text = "<table>\n    <thead>\n        <tr>\n            <th>${1:Header 1}</th>\n            <th>${2:Header 2}</th>\n        </tr>\n    </thead>\n    <tbody>\n        <tr>\n            <td>${3:Data 1}</td>\n            <td>${4:Data 2}</td>\n        </tr>\n    </tbody>\n</table>",
            .sort_text = "1_table",
            .filter_text = "table"
        },
        Protocol::CompletionItem{
            .label = "ul>li",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<ul><li>...</li></ul>",
            .documentation = "Unordered list with list items.",
            .insert_text = "<ul>\n    <li>${1:Item 1}</li>\n    <li>${2:Item 2}</li>\n    <li>${3:Item 3}</li>\n</ul>",
            .sort_text = "1_ul_li",
            .filter_text = "ul>li"
        },
        Protocol::CompletionItem{
            .label = "ol>li",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<ol><li>...</li></ol>",
            .documentation = "Ordered numbered list with list items.",
            .insert_text = "<ol>\n    <li>${1:Item 1}</li>\n    <li>${2:Item 2}</li>\n    <li>${3:Item 3}</li>\n</ol>",
            .sort_text = "1_ol_li",
            .filter_text = "ol>li"
        },
        Protocol::CompletionItem{
            .label = "select",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<select name=\"...\"><option value=\"...\">...</option></select>",
            .documentation = "Dropdown selection list with option items.",
            .insert_text = "<select name=\"${1:choice}\" id=\"${1:choice}\">\n    <option value=\"${2:val1}\">${3:Option 1}</option>\n    <option value=\"${4:val2}\">${5:Option 2}</option>\n</select>",
            .sort_text = "1_select",
            .filter_text = "select"
        },
        Protocol::CompletionItem{
            .label = "style",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<style>\n    ...\n</style>",
            .documentation = "Internal CSS stylesheet block.",
            .insert_text = "<style>\n    ${1:body} {\n        ${2:margin: 0;}\n    }\n</style>",
            .sort_text = "1_style",
            .filter_text = "style"
        },
        Protocol::CompletionItem{
            .label = "script",
            .kind = Protocol::CompletionItemKind::Snippet,
            .detail = "<script>\n    ...\n</script>",
            .documentation = "Inline JavaScript script execution block.",
            .insert_text = "<script>\n    ${0}\n</script>",
            .sort_text = "1_script",
            .filter_text = "script"
        }
    };
}

std::vector<Protocol::CompletionItem> get_html_attribute_completions()
{
    return {
        Protocol::CompletionItem{.label = "class", .kind = Protocol::CompletionItemKind::Property, .detail = "class=\"...\"", .documentation = "CSS class list attribute.", .insert_text = "class=\"${1}\"$0", .sort_text = "0_class", .filter_text = "class"},
        Protocol::CompletionItem{.label = "id", .kind = Protocol::CompletionItemKind::Property, .detail = "id=\"...\"", .documentation = "Unique element identifier attribute.", .insert_text = "id=\"${1}\"$0", .sort_text = "0_id", .filter_text = "id"},
        Protocol::CompletionItem{.label = "style", .kind = Protocol::CompletionItemKind::Property, .detail = "style=\"...\"", .documentation = "Inline CSS style rules.", .insert_text = "style=\"${1}\"$0", .sort_text = "0_style", .filter_text = "style"},
        Protocol::CompletionItem{.label = "href", .kind = Protocol::CompletionItemKind::Property, .detail = "href=\"...\"", .documentation = "Hyperlink destination URL.", .insert_text = "href=\"${1:#}\"$0", .sort_text = "0_href", .filter_text = "href"},
        Protocol::CompletionItem{.label = "src", .kind = Protocol::CompletionItemKind::Property, .detail = "src=\"...\"", .documentation = "External resource image/media/script source path.", .insert_text = "src=\"${1}\"$0", .sort_text = "0_src", .filter_text = "src"},
        Protocol::CompletionItem{.label = "alt", .kind = Protocol::CompletionItemKind::Property, .detail = "alt=\"...\"", .documentation = "Alternative text description for images.", .insert_text = "alt=\"${1}\"$0", .sort_text = "0_alt", .filter_text = "alt"},
        Protocol::CompletionItem{.label = "type", .kind = Protocol::CompletionItemKind::Property, .detail = "type=\"...\"", .documentation = "Element behavior/input type.", .insert_text = "type=\"${1:text}\"$0", .sort_text = "0_type", .filter_text = "type"},
        Protocol::CompletionItem{.label = "value", .kind = Protocol::CompletionItemKind::Property, .detail = "value=\"...\"", .documentation = "Default or current value of the form field.", .insert_text = "value=\"${1}\"$0", .sort_text = "0_value", .filter_text = "value"},
        Protocol::CompletionItem{.label = "name", .kind = Protocol::CompletionItemKind::Property, .detail = "name=\"...\"", .documentation = "Form submission field name.", .insert_text = "name=\"${1}\"$0", .sort_text = "0_name", .filter_text = "name"},
        Protocol::CompletionItem{.label = "placeholder", .kind = Protocol::CompletionItemKind::Property, .detail = "placeholder=\"...\"", .documentation = "Hint text displayed inside empty inputs.", .insert_text = "placeholder=\"${1}\"$0", .sort_text = "0_placeholder", .filter_text = "placeholder"},
        Protocol::CompletionItem{.label = "target", .kind = Protocol::CompletionItemKind::Property, .detail = "target=\"_blank\"", .documentation = "Link browsing context target.", .insert_text = "target=\"${1:_blank}\"$0", .sort_text = "0_target", .filter_text = "target"},
        Protocol::CompletionItem{.label = "rel", .kind = Protocol::CompletionItemKind::Property, .detail = "rel=\"noopener noreferrer\"", .documentation = "Relationship between linked resource and document.", .insert_text = "rel=\"${1:noopener noreferrer}\"$0", .sort_text = "0_rel", .filter_text = "rel"},
        Protocol::CompletionItem{.label = "disabled", .kind = Protocol::CompletionItemKind::Property, .detail = "disabled", .documentation = "Disables user interaction with the control.", .insert_text = "disabled", .sort_text = "1_disabled", .filter_text = "disabled"},
        Protocol::CompletionItem{.label = "required", .kind = Protocol::CompletionItemKind::Property, .detail = "required", .documentation = "Requires the input field before form submission.", .insert_text = "required", .sort_text = "1_required", .filter_text = "required"},
        Protocol::CompletionItem{.label = "readonly", .kind = Protocol::CompletionItemKind::Property, .detail = "readonly", .documentation = "Prevents user modification of the input value.", .insert_text = "readonly", .sort_text = "1_readonly", .filter_text = "readonly"},
        Protocol::CompletionItem{.label = "checked", .kind = Protocol::CompletionItemKind::Property, .detail = "checked", .documentation = "Specifies that the checkbox/radio is pre-selected.", .insert_text = "checked", .sort_text = "1_checked", .filter_text = "checked"},
        Protocol::CompletionItem{.label = "onclick", .kind = Protocol::CompletionItemKind::Event, .detail = "onclick=\"...\"", .documentation = "Mouse click JavaScript event handler.", .insert_text = "onclick=\"${1}\"$0", .sort_text = "2_onclick", .filter_text = "onclick"},
        Protocol::CompletionItem{.label = "onsubmit", .kind = Protocol::CompletionItemKind::Event, .detail = "onsubmit=\"...\"", .documentation = "Form submission JavaScript event handler.", .insert_text = "onsubmit=\"${1}\"$0", .sort_text = "2_onsubmit", .filter_text = "onsubmit"},
        Protocol::CompletionItem{.label = "onchange", .kind = Protocol::CompletionItemKind::Event, .detail = "onchange=\"...\"", .documentation = "Input change JavaScript event handler.", .insert_text = "onchange=\"${1}\"$0", .sort_text = "2_onchange", .filter_text = "onchange"}
    };
}

std::vector<Protocol::CompletionItem> get_templates_for_file(std::string_view filename)
{
    const std::filesystem::path p(filename);
    const std::string ext = p.extension().string();
    const std::string fname = p.filename().string();

    std::vector<Protocol::CompletionItem> results;
    std::unordered_set<std::string> seen_labels;

    auto add_items = [&](std::vector<Protocol::CompletionItem> items) {
        for (auto& it : items)
        {
            if (seen_labels.insert(it.label).second)
            {
                results.push_back(std::move(it));
            }
        }
    };

    const bool is_html = (ext == ".html" || ext == ".htm" || ext == ".xhtml");
    const bool is_ts = (ext == ".ts" || ext == ".tsx" || ext == ".mts" || ext == ".cts" ||
                        ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs");
    if (fname == "CMakeLists.txt" || fname == "cmakelists.txt" || ext == ".cmake")
    {
        add_items(CMake::CMakeLanguageDatabase::instance().get_all_completions());
    }
    else if (is_html)
    {
        add_items(get_jetbrains_html_templates());
        add_items(get_html_attribute_completions());
    }
    else if (is_ts)
    {
        add_items(get_jetbrains_typescript_templates(ext == ".tsx" || ext == ".jsx"));
    }
    else if (ext == ".rs")
    {
        add_items(get_jetbrains_rust_templates());
    }
    else if (ext == ".java" || ext == ".kt")
    {
        add_items(get_jetbrains_java_templates());
    }
    else if (fname == "meson.build" || fname == "meson_options.txt")
    {
        add_items(get_jetbrains_meson_templates());
    }
    else
    {
        add_items(get_jetbrains_cpp_templates());
    }

    // Populate all keywords from GrammarRegistry if registered
    const auto* grammar = Syntax::GrammarRegistry::instance().get_grammar_for_filename(filename);
    if (grammar != nullptr)
    {
        for (const auto& kw : grammar->keywords)
        {
            if (seen_labels.insert(kw).second)
            {
                results.push_back(Protocol::CompletionItem{
                    .label = kw,
                    .kind = Protocol::CompletionItemKind::Keyword,
                    .detail = "(Built-in) " + kw,
                    .documentation = "Language keyword / directive: " + kw,
                    .insert_text = kw,
                    .filter_text = kw
                });
            }
        }
        for (const auto& ty : grammar->types)
        {
            if (seen_labels.insert(ty).second)
            {
                results.push_back(Protocol::CompletionItem{
                    .label = ty,
                    .kind = Protocol::CompletionItemKind::Class,
                    .detail = "(Type) " + ty,
                    .documentation = "Standard type / property: " + ty,
                    .insert_text = ty,
                    .filter_text = ty
                });
            }
        }
    }

    return results;
}

std::vector<Protocol::CompletionItem> get_standard_cpp_headers()
{
    static const std::vector<std::pair<std::string_view, std::string_view>> s_headers = {
        {"algorithm", "Algorithms on ranges, sorting, searching, permutations"},
        {"any", "Type-safe container for single values of any type"},
        {"array", "Fixed-size sequence container"},
        {"atomic", "Atomic operations library for multithreading"},
        {"barrier", "Thread coordination barrier synchronization"},
        {"bit", "Bit manipulation utilities and endian access"},
        {"bitset", "Fixed-size bit array and boolean operations"},
        {"cassert", "C-style runtime diagnostic assertion macro"},
        {"cctype", "Character classification and case conversion functions"},
        {"cerrno", "C-style error numbers and errno variable"},
        {"cfloat", "Limits of floating-point types"},
        {"chrono", "Date and time utilities, duration, clocks, time_point"},
        {"cinttypes", "Formatting macros for exact-width integer types"},
        {"climits", "Limits of integral types"},
        {"clocale", "C-style localization and collation control"},
        {"cmath", "Mathematical functions, trigonometry, power, logarithms"},
        {"codecvt", "Unicode character conversion facets"},
        {"compare", "Three-way comparison (spaceship operator <=>) support"},
        {"complex", "Complex numbers and arithmetic"},
        {"concepts", "Fundamental language concepts for template constraints"},
        {"condition_variable", "Condition variable synchronization primitives"},
        {"coroutine", "Coroutine support library and promises"},
        {"csetjmp", "Execution flow non-local jumps (setjmp/longjmp)"},
        {"csignal", "Signal handling and raise facilities"},
        {"cstdarg", "Variable argument list handling (va_list, va_start)"},
        {"cstddef", "Standard types: size_t, ptrdiff_t, nullptr_t, byte"},
        {"cstdint", "Fixed-width integer types: int32_t, uint64_t, etc."},
        {"cstdio", "C-style standard I/O (printf, fopen, fread, etc.)"},
        {"cstdlib", "C-style general utilities (malloc, free, exit, atoi)"},
        {"cstring", "C-style string manipulation (strlen, memcpy, strcmp)"},
        {"ctime", "C-style time and date functions"},
        {"cuchar", "C-style Unicode character manipulation"},
        {"cwchar", "C-style wide character manipulation"},
        {"cwctype", "C-style wide character classification"},
        {"deque", "Double-ended queue sequence container"},
        {"exception", "Exception handling base classes and utilities"},
        {"execution", "Execution policies for parallel algorithms"},
        {"expected", "Monadic error handling container (std::expected)"},
        {"filesystem", "Filesystem navigation, paths, directory iteration"},
        {"flat_map", "Sorted vector based associative map container"},
        {"flat_set", "Sorted vector based set container"},
        {"format", "Modern type-safe text formatting library (std::format)"},
        {"forward_list", "Singly-linked list sequence container"},
        {"fstream", "File stream input and output classes"},
        {"functional", "Function objects, std::function, bind, invoke"},
        {"future", "Asynchronous operations support: std::future, async, promise"},
        {"generator", "Coroutine-based generator view"},
        {"hazard_pointer", "Hazard pointers for lock-free data structures"},
        {"initializer_list", "List-initialization syntax support"},
        {"iomanip", "Input/output stream manipulators"},
        {"ios", "Base class for all input/output streams"},
        {"iosfwd", "Forward declarations of all stream classes"},
        {"iostream", "Standard input/output stream objects: std::cin, std::cout, std::cerr"},
        {"istream", "Input stream classes"},
        {"iterator", "Iterator primitives, adaptors, and concepts"},
        {"latch", "Countdown latch synchronization primitive"},
        {"limits", "Numeric limits for all fundamental types"},
        {"list", "Doubly-linked list sequence container"},
        {"locale", "Localization facilities and facets"},
        {"map", "Sorted associative container (std::map, std::multimap)"},
        {"mdspan", "Multi-dimensional array view"},
        {"memory", "Smart pointers: std::unique_ptr, std::shared_ptr, allocators"},
        {"memory_resource", "Polymorphic memory resources and PMR containers"},
        {"mutex", "Mutual exclusion synchronization primitives: std::mutex, lock_guard"},
        {"new", "Low-level memory allocation and placement new operators"},
        {"numbers", "Mathematical constants (pi, e, sqrt2, etc.)"},
        {"numeric", "Generalized numeric operations (accumulate, iota, reduce)"},
        {"optional", "Optional value wrapper (std::optional, std::nullopt)"},
        {"ostream", "Output stream classes"},
        {"print", "Print to stdout/stderr formatted text (std::print, std::println)"},
        {"queue", "Queue and priority queue container adaptors"},
        {"random", "Random number generators and distributions"},
        {"ranges", "Range algorithms and view adaptors"},
        {"ratio", "Compile-time rational arithmetic"},
        {"rcu", "Read-Copy Update synchronization primitives"},
        {"regex", "Regular expressions matching and replacement"},
        {"scoped_allocator", "Nested allocator support"},
        {"semaphore", "Counting semaphore synchronization primitives"},
        {"set", "Sorted associative set container (std::set, std::multiset)"},
        {"shared_mutex", "Shared (reader-writer) mutual exclusion"},
        {"source_location", "Source code file/line capture (std::source_location)"},
        {"span", "Contiguous sequence view (std::span)"},
        {"spanstream", "In-memory stream backed by span"},
        {"sstream", "String stream classes (std::stringstream)"},
        {"stack", "LIFO stack container adaptor"},
        {"stacktrace", "Stack trace capture and formatting"},
        {"stdexcept", "Standard exception classes: std::runtime_error, std::invalid_argument"},
        {"stdfloat", "Extended floating-point types"},
        {"stop_token", "Cooperative thread cancellation primitives"},
        {"streambuf", "Stream buffer classes"},
        {"string", "Standard dynamic string class: std::string"},
        {"string_view", "Non-owning string reference (std::string_view)"},
        {"syncstream", "Synchronized output streams for multithreading"},
        {"system_error", "Operating system error codes and error_condition"},
        {"thread", "Thread management facilities: std::thread, jthread"},
        {"tuple", "Fixed-size heterogeneous collection of values (std::tuple)"},
        {"type_traits", "Compile-time type inspection and transformation"},
        {"typeindex", "Runtime type wrapper for associative containers"},
        {"typeinfo", "Runtime type identification (typeid operator)"},
        {"unordered_map", "Hash-table associative map container (std::unordered_map)"},
        {"unordered_set", "Hash-table associative set container (std::unordered_set)"},
        {"utility", "General utilities: std::pair, std::move, std::forward"},
        {"valarray", "Numeric vector for element-wise mathematical operations"},
        {"variant", "Type-safe tagged union container (std::variant)"},
        {"vector", "Dynamic array sequence container (std::vector)"},
        {"version", "C++ standard library feature-test macros"},
        // Linux / POSIX / X11 headers
        {"unistd.h", "Standard symbolic constants and types for POSIX"},
        {"fcntl.h", "File control options and flags (open, fcntl)"},
        {"pthread.h", "POSIX threads library"},
        {"signal.h", "ANSI/POSIX signal handling"},
        {"sys/types.h", "POSIX data types"},
        {"sys/stat.h", "File status and permissions (stat, mkdir)"},
        {"sys/time.h", "Time structures (timeval, gettimeofday)"},
        {"sys/socket.h", "Internet and UNIX socket definitions"},
        {"sys/wait.h", "Process termination waiting (waitpid)"},
        {"X11/Xlib.h", "X Window System C library interface"},
        {"X11/Xutil.h", "X Window System utility functions"},
        {"X11/Xatom.h", "X Window System predefined atom definitions"},
        {"X11/keysym.h", "X Window System key symbol definitions"},
        {"X11/cursorfont.h", "X Window System standard cursor definitions"}
    };

    std::vector<Protocol::CompletionItem> items;
    items.reserve(s_headers.size());
    for (const auto& [hdr, doc] : s_headers)
    {
        items.push_back(Protocol::CompletionItem{
            .label = std::string(hdr),
            .kind = Protocol::CompletionItemKind::File,
            .detail = "(Header) <" + std::string(hdr) + ">",
            .documentation = std::string(doc),
            .insert_text = std::string(hdr),
            .sort_text = "0_" + std::string(hdr),
            .filter_text = std::string(hdr)
        });
    }
    return items;
}

std::vector<Protocol::CompletionItem> get_workspace_headers(const std::filesystem::path& workspace_root)
{
    std::vector<Protocol::CompletionItem> items;
    if (workspace_root.empty()) return items;

    std::error_code ec;
    const std::filesystem::path search_dirs[] = {
        workspace_root / "Source",
        workspace_root / "Include",
        workspace_root / "include",
        workspace_root
    };

    std::unordered_set<std::string> seen;
    for (const auto& dir : search_dirs)
    {
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
        {
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
        {
            if (entry.is_regular_file(ec))
            {
                const auto ext = entry.path().extension().string();
                if (ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".inl" || ext == ".inc")
                {
                    std::filesystem::path rel;
                    if (dir == workspace_root / "Source")
                    {
                        rel = std::filesystem::relative(entry.path(), dir, ec);
                    }
                    else
                    {
                        rel = std::filesystem::relative(entry.path(), workspace_root, ec);
                    }
                    std::string rel_str = rel.generic_string();
                    if (!rel_str.empty() && seen.insert(rel_str).second)
                    {
                        items.push_back(Protocol::CompletionItem{
                            .label = rel_str,
                            .kind = Protocol::CompletionItemKind::File,
                            .detail = "(Project Header) \"" + rel_str + "\"",
                            .documentation = "Local project header: " + rel_str,
                            .insert_text = rel_str,
                            .sort_text = "0_" + rel_str,
                            .filter_text = rel_str
                        });
                    }
                }
            }
        }
    }
    return items;
}

} // namespace

void LanguageServerManager::request_completion(
    const std::string& uri,
    std::string_view filename,
    const Protocol::Position& pos,
    std::string_view line_text,
    std::function<void(std::vector<Protocol::CompletionItem>)> callback)
{
    const std::filesystem::path p(filename);
    const std::string ext = p.extension().string();
    const std::string fname = p.filename().string();
    const bool is_cmake = (fname == "CMakeLists.txt" || fname == "cmakelists.txt" || ext == ".cmake");

    if (is_cmake)
    {
        auto cmake_items = CMake::CMakeLanguageDatabase::instance().get_completions_for_context(line_text, pos.character);
        auto* client = get_or_start_client_for_file(filename);
        if (client != nullptr && client->is_active())
        {
            client->request_completion(uri, pos, [callback = std::move(callback), cmake_items = std::move(cmake_items)](std::vector<Protocol::CompletionItem> items) mutable {
                std::unordered_set<std::string> seen;
                for (const auto& it : items)
                {
                    seen.insert(it.label);
                }
                for (auto& ci : cmake_items)
                {
                    if (seen.insert(ci.label).second)
                    {
                        items.push_back(std::move(ci));
                    }
                }
                if (callback)
                {
                    callback(std::move(items));
                }
            });
            return;
        }

        if (callback)
        {
            callback(std::move(cmake_items));
        }
        return;
    }

    const std::string_view prefix_to_caret = line_text.substr(0, std::min(pos.character, line_text.size()));
    const bool is_ts = (ext == ".ts" || ext == ".tsx" || ext == ".mts" || ext == ".cts" ||
                        ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs");
    if (is_ts)
    {
        const bool is_import_or_require = (prefix_to_caret.find("import") != std::string_view::npos ||
                                           prefix_to_caret.find("require") != std::string_view::npos ||
                                           prefix_to_caret.find("from") != std::string_view::npos);
        const bool in_quotes = (std::count(prefix_to_caret.begin(), prefix_to_caret.end(), '\'') % 2 == 1 ||
                                std::count(prefix_to_caret.begin(), prefix_to_caret.end(), '"') % 2 == 1);
        if (is_import_or_require && in_quotes)
        {
            auto import_items = get_workspace_ts_imports(m_workspace_root, filename);
            auto* client = get_or_start_client_for_file(filename);
            if (client != nullptr && client->is_active())
            {
                client->request_completion(uri, pos, [callback = std::move(callback), import_items = std::move(import_items)](std::vector<Protocol::CompletionItem> lsp_items) mutable {
                    std::unordered_set<std::string> seen;
                    std::vector<Protocol::CompletionItem> merged;
                    merged.reserve(import_items.size() + lsp_items.size());
                    for (auto& it : lsp_items)
                    {
                        if (seen.insert(it.label).second) merged.push_back(std::move(it));
                    }
                    for (auto& ii : import_items)
                    {
                        if (seen.insert(ii.label).second) merged.push_back(std::move(ii));
                    }
                    if (callback) callback(std::move(merged));
                });
                return;
            }
            if (callback) callback(std::move(import_items));
            return;
        }
    }

    const bool is_html = (ext == ".html" || ext == ".htm" || ext == ".xhtml");
    if (is_html)
    {
        const std::size_t last_open = prefix_to_caret.rfind('<');
        const std::size_t last_close = prefix_to_caret.rfind('>');
        const bool inside_tag = (last_open != std::string_view::npos &&
                                 (last_close == std::string_view::npos || last_open > last_close));

        auto fallback_items = inside_tag ? get_html_attribute_completions() : get_jetbrains_html_templates();
        auto* client = get_or_start_client_for_file(filename);
        if (client != nullptr && client->is_active())
        {
            client->request_completion(uri, pos, [callback = std::move(callback), fallback_items = std::move(fallback_items)](std::vector<Protocol::CompletionItem> lsp_items) mutable {
                std::unordered_set<std::string> seen;
                std::vector<Protocol::CompletionItem> merged;
                merged.reserve(lsp_items.size() + fallback_items.size());
                for (auto& it : lsp_items)
                {
                    if (seen.insert(it.label).second)
                    {
                        merged.push_back(std::move(it));
                    }
                }
                for (auto& item : fallback_items)
                {
                    if (seen.insert(item.label).second)
                    {
                        merged.push_back(std::move(item));
                    }
                }
                if (callback)
                {
                    callback(std::move(merged));
                }
            });
            return;
        }

        if (callback)
        {
            callback(std::move(fallback_items));
        }
        return;
    }

    const std::size_t inc_pos = prefix_to_caret.find("#include");
    const std::size_t imp_pos = prefix_to_caret.find("#import");
    const bool is_include_line = (inc_pos != std::string_view::npos || imp_pos != std::string_view::npos);

    if (is_include_line)
    {
        const std::size_t last_lt = prefix_to_caret.rfind('<');
        const std::size_t last_gt = prefix_to_caret.rfind('>');
        const bool is_system_include = (last_lt != std::string_view::npos && (last_gt == std::string_view::npos || last_lt > last_gt));

        const std::size_t first_quote = prefix_to_caret.find('"');
        const bool is_local_include = (first_quote != std::string_view::npos && (std::count(prefix_to_caret.begin(), prefix_to_caret.end(), '"') % 2 == 1));

        std::vector<Protocol::CompletionItem> header_items;
        if (is_system_include)
        {
            header_items = get_standard_cpp_headers();
        }
        else if (is_local_include)
        {
            header_items = get_workspace_headers(m_workspace_root);
            auto sys = get_standard_cpp_headers();
            for (auto& s : sys) header_items.push_back(std::move(s));
        }
        else
        {
            header_items.push_back(Protocol::CompletionItem{
                .label = "<header>",
                .kind = Protocol::CompletionItemKind::Snippet,
                .detail = "#include <header>",
                .documentation = "System header include directive.",
                .insert_text = "<${1:header}>$0",
                .sort_text = "0_a_<header>",
                .filter_text = "<header>"
            });
            header_items.push_back(Protocol::CompletionItem{
                .label = "\"header\"",
                .kind = Protocol::CompletionItemKind::Snippet,
                .detail = "#include \"header\"",
                .documentation = "Local project header include directive.",
                .insert_text = "\"${1:header}\"$0",
                .sort_text = "0_b_\"header\"",
                .filter_text = "\"header\""
            });
            auto sys = get_standard_cpp_headers();
            for (auto& s : sys) header_items.push_back(std::move(s));
        }

        auto* client = get_or_start_client_for_file(filename);
        if (client != nullptr && client->is_active())
        {
            client->request_completion(uri, pos, [callback = std::move(callback), header_items = std::move(header_items)](std::vector<Protocol::CompletionItem> lsp_items) mutable {
                std::unordered_set<std::string> seen;
                std::vector<Protocol::CompletionItem> merged;
                merged.reserve(header_items.size() + lsp_items.size());
                for (auto& it : lsp_items)
                {
                    if (seen.insert(it.label).second)
                    {
                        merged.push_back(std::move(it));
                    }
                }
                for (auto& hi : header_items)
                {
                    if (seen.insert(hi.label).second)
                    {
                        merged.push_back(std::move(hi));
                    }
                }
                if (callback)
                {
                    callback(std::move(merged));
                }
            });
            return;
        }

        if (callback)
        {
            callback(std::move(header_items));
        }
        return;
    }

    const std::string file_str(filename);
    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr && client->is_active())
    {
        client->request_completion(uri, pos, [callback = std::move(callback), file_str](std::vector<Protocol::CompletionItem> lsp_items) {
            if (!callback) return;

            if (!lsp_items.empty())
            {
                // Full LSP-driven completion: LSP results take primary precedence
                std::unordered_set<std::string> lsp_labels;
                for (const auto& item : lsp_items)
                {
                    lsp_labels.insert(item.label);
                }

                auto templates = get_templates_for_file(file_str);
                std::vector<Protocol::CompletionItem> merged;
                merged.reserve(lsp_items.size() + templates.size());

                // 1. LSP items first
                for (auto& item : lsp_items)
                {
                    merged.push_back(std::move(item));
                }

                // 2. Secondary keyword/template snippets
                for (auto& tmpl : templates)
                {
                    if (!lsp_labels.contains(tmpl.label))
                    {
                        merged.push_back(std::move(tmpl));
                    }
                }

                callback(std::move(merged));
            }
            else
            {
                // Fallback to templates if LSP returned no items yet
                callback(get_templates_for_file(file_str));
            }
        });
    }
    else if (callback)
    {
        callback(get_templates_for_file(file_str));
    }
}

void LanguageServerManager::request_hover(
    const std::string& uri,
    std::string_view filename,
    const Protocol::Position& pos,
    std::string_view line_text,
    std::function<void(std::optional<Protocol::Hover>)> callback)
{
    const std::filesystem::path p(filename);
    const std::string ext = p.extension().string();
    const std::string fname = p.filename().string();
    const bool is_cmake = (fname == "CMakeLists.txt" || fname == "cmakelists.txt" || ext == ".cmake");

    if (is_cmake && !line_text.empty())
    {
        std::size_t col = std::min(pos.character, line_text.size());
        std::size_t start = col;
        while (start > 0)
        {
            const char ch = line_text[start - 1];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$' || ch == '{' || ch == '}')
            {
                --start;
            }
            else
            {
                break;
            }
        }
        std::size_t end = col;
        while (end < line_text.size())
        {
            const char ch = line_text[end];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$' || ch == '{' || ch == '}')
            {
                ++end;
            }
            else
            {
                break;
            }
        }

        if (start < end)
        {
            const std::string_view word = line_text.substr(start, end - start);
            auto hover = CMake::CMakeLanguageDatabase::instance().find_hover(word);
            if (hover.has_value())
            {
                if (callback)
                {
                    callback(hover);
                }
                return;
            }
        }
    }

    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        client->request_hover(uri, pos, std::move(callback));
    }
    else if (callback)
    {
        callback(std::nullopt);
    }
}

void LanguageServerManager::request_definition(
    const std::string& uri,
    std::string_view filename,
    const Protocol::Position& pos,
    std::function<void(std::vector<Protocol::Location>)> callback)
{
    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        client->request_definition(uri, pos, std::move(callback));
    }
    else if (callback)
    {
        callback({});
    }
}

void LanguageServerManager::request_signature_help(
    const std::string& uri,
    std::string_view filename,
    const Protocol::Position& pos,
    std::string_view line_text,
    std::function<void(std::optional<Protocol::SignatureHelp>)> callback)
{
    const std::filesystem::path p(filename);
    const std::string ext = p.extension().string();
    const std::string fname = p.filename().string();
    const bool is_cmake = (fname == "CMakeLists.txt" || fname == "cmakelists.txt" || ext == ".cmake");

    if (is_cmake && !line_text.empty())
    {
        std::size_t col = std::min(pos.character, line_text.size());
        const std::string_view prefix = line_text.substr(0, col);
        const std::size_t open_paren = prefix.rfind('(');
        if (open_paren != std::string_view::npos)
        {
            std::size_t cmd_end = open_paren;
            while (cmd_end > 0 && std::isspace(static_cast<unsigned char>(prefix[cmd_end - 1])))
            {
                --cmd_end;
            }
            std::size_t cmd_start = cmd_end;
            while (cmd_start > 0)
            {
                const char ch = prefix[cmd_start - 1];
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
                {
                    --cmd_start;
                }
                else
                {
                    break;
                }
            }

            if (cmd_start < cmd_end)
            {
                const std::string_view cmd_name = prefix.substr(cmd_start, cmd_end - cmd_start);
                auto sig = CMake::CMakeLanguageDatabase::instance().find_signature_help(cmd_name);
                if (sig.has_value())
                {
                    if (callback)
                    {
                        callback(sig);
                    }
                    return;
                }
            }
        }
    }

    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        client->request_signature_help(uri, pos, std::move(callback));
    }
    else if (callback)
    {
        callback(std::nullopt);
    }
}

void LanguageServerManager::request_semantic_tokens(
    const std::string& uri,
    std::string_view filename,
    std::function<void(std::vector<Syntax::SemanticTokenSpan>)> callback)
{
    auto* client = get_or_start_client_for_file(filename);
    if (client == nullptr)
    {
        if (callback) callback({});
        return;
    }

    client->request_semantic_tokens(uri, [this, uri, callback = std::move(callback)](std::optional<Protocol::SemanticTokens> tokens) {
        if (!tokens.has_value() || tokens->data.empty())
        {
            if (callback) callback({});
            return;
        }

        auto spans = Syntax::SemanticTokensManager::decode_lsp_tokens(tokens->data);
        m_semantic_tokens_manager.update_document_tokens(uri, spans);

        if (callback)
        {
            callback(std::move(spans));
        }
    });
}

void LanguageServerManager::set_diagnostics_callback(
    std::function<void(const std::string& uri, const std::vector<Protocol::Diagnostic>&)> callback)
{
    m_diagnostics_callback = std::move(callback);
}

std::vector<Protocol::Diagnostic> LanguageServerManager::get_diagnostics_for_document(const std::string& uri) const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_clients_mutex));
    if (const auto it = m_document_diagnostics.find(uri); it != m_document_diagnostics.end())
    {
        return it->second;
    }
    return {};
}

void LanguageServerManager::shutdown_all()
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    for (auto& [id, client] : m_clients)
    {
        if (client)
        {
            client->shutdown();
            client->exit();
        }
    }
    m_clients.clear();
}

} // namespace Zenvra::Language
