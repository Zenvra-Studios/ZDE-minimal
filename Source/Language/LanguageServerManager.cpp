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
        const std::string lang_id = profile != nullptr ? profile->language_id : "plaintext";

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

    if (fname == "CMakeLists.txt" || fname == "cmakelists.txt" || ext == ".cmake")
    {
        add_items(CMake::CMakeLanguageDatabase::instance().get_all_completions());
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

    const std::string file_str(filename);
    auto merge_with_templates = [file_str](std::vector<Protocol::CompletionItem> items) {
        auto templates = get_templates_for_file(file_str);
        std::unordered_set<std::string> template_labels;
        for (const auto& tmpl : templates)
        {
            template_labels.insert(tmpl.label);
        }

        std::vector<Protocol::CompletionItem> merged;
        merged.reserve(templates.size() + items.size());

        // 1. Put all rich IDE snippet templates at the top of the autocomplete list
        for (auto& tmpl : templates)
        {
            merged.push_back(std::move(tmpl));
        }

        // 2. Append LSP items, skipping plain keyword duplicates that match rich templates
        for (auto& it : items)
        {
            if (it.kind == Protocol::CompletionItemKind::Keyword && template_labels.contains(it.label))
            {
                continue;
            }
            merged.push_back(std::move(it));
        }

        return merged;
    };

    auto* client = get_or_start_client_for_file(filename);
    if (client != nullptr)
    {
        client->request_completion(uri, pos, [callback = std::move(callback), merge_with_templates](std::vector<Protocol::CompletionItem> items) {
            if (callback)
            {
                callback(merge_with_templates(std::move(items)));
            }
        });
    }
    else if (callback)
    {
        callback(merge_with_templates({}));
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
