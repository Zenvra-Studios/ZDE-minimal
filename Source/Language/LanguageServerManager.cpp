#include "Language/LanguageServerManager.h"
#include "Language/CMake/CMakeLanguageDatabase.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/Toolchain/ToolchainDetector.h"
#include "Language/Transport/StdioProcessTransport.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace Zenvra::Language {

namespace {
void lsp_debug_log(std::string_view msg) {
  std::error_code ec;
#ifdef _WIN32
  char tmp_buf[512] = {};
  std::size_t tmp_len = 0;
  std::filesystem::path log_path;
  if (getenv_s(&tmp_len, tmp_buf, sizeof(tmp_buf), "TEMP") == 0 &&
      tmp_len > 0) {
    log_path = std::filesystem::path(tmp_buf);
  } else {
    log_path = std::filesystem::current_path(ec);
  }
#else
  const char *tmp = std::getenv("TMPDIR");
  std::filesystem::path log_path =
      tmp ? std::filesystem::path(tmp) : std::filesystem::current_path(ec);
#endif
  log_path /= "zde-lsp.log";
  std::ofstream out(log_path, std::ios::app);
  if (out) {
    out << msg << '\n';
  }
}

// Built-in Language Snippets and Templates (JetBrains / VS Code standard
// snippets)
std::vector<Protocol::CompletionItem> get_jetbrains_cpp_templates() {
  return {
      Protocol::CompletionItem{
          .label = "struct",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(JetBrains Template) struct Name { ... };",
          .documentation =
              "Generates a C++ struct definition with body and semicolon.",
          .insert_text = "struct ${1:Name}\n{\n    $0\n};",
          .filter_text = "struct"},
      Protocol::CompletionItem{
          .label = "class",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(JetBrains Template) class Name { public: ... };",
          .documentation = "Generates a C++ class definition with constructor, "
                           "destructor and private sections.",
          .insert_text = "class ${1:Name}\n{\npublic:\n    ${1:Name}();\n    "
                         "~${1:Name}();\n\nprivate:\n    $0\n};",
          .filter_text = "class"},
      Protocol::CompletionItem{
          .label = "namespace",
          .kind = Protocol::CompletionItemKind::Module,
          .detail =
              "(JetBrains Template) namespace Name { ... } // namespace Name",
          .documentation =
              "Generates a C++ namespace block with matching closing comment.",
          .insert_text =
              "namespace ${1:Name}\n{\n\n$0\n\n} // namespace ${1:Name}",
          .filter_text = "namespace"},
      Protocol::CompletionItem{
          .label = "ns",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) namespace Name { ... }",
          .documentation = "Shortcut to generate a C++ namespace block with "
                           "closing comment.",
          .insert_text =
              "namespace ${1:Name}\n{\n\n$0\n\n} // namespace ${1:Name}",
          .filter_text = "ns"},
      Protocol::CompletionItem{
          .label = "enum class",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(JetBrains Template) enum class Name : uint32_t { ... };",
          .documentation = "Generates a strongly typed enum class definition.",
          .insert_text = "enum class ${1:Name}\n{\n    $0\n};",
          .filter_text = "enum class"},
      Protocol::CompletionItem{.label = "enum",
                               .kind = Protocol::CompletionItemKind::Class,
                               .detail =
                                   "(JetBrains Template) enum Name { ... };",
                               .documentation = "Generates an enum definition.",
                               .insert_text = "enum ${1:Name}\n{\n    $0\n};",
                               .filter_text = "enum"},
      Protocol::CompletionItem{
          .label = "interface",
          .kind = Protocol::CompletionItemKind::Interface,
          .detail = "(JetBrains Template) struct IInterface { virtual "
                    "~IInterface() = default; ... };",
          .documentation = "Generates an abstract C++ interface with virtual "
                           "default destructor.",
          .insert_text = "struct I${1:Interface}\n{\n    virtual "
                         "~I${1:Interface}() = default;\n    $0\n};",
          .filter_text = "interface"},
      Protocol::CompletionItem{
          .label = "template struct",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail =
              "(JetBrains Template) template <typename T> struct Name { ... };",
          .documentation = "Generates a templated struct definition.",
          .insert_text =
              "template <typename ${1:T}>\nstruct ${2:Name}\n{\n    $0\n};",
          .filter_text = "template struct"},
      Protocol::CompletionItem{
          .label = "template class",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail =
              "(JetBrains Template) template <typename T> class Name { ... };",
          .documentation = "Generates a templated class definition.",
          .insert_text =
              "template <typename ${1:T}>\nclass ${2:Name}\n{\npublic:\n    "
              "${2:Name}();\n    ~${2:Name}();\n\nprivate:\n    $0\n};",
          .filter_text = "template class"},
      Protocol::CompletionItem{
          .label = "template function",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail =
              "(JetBrains Template) template <typename T> void fn() { ... }",
          .documentation = "Generates a templated function signature and body.",
          .insert_text = "template <typename ${1:T}>\n${2:void} "
                         "${3:function_name}(${4:/*params*/})\n{\n    $0\n}",
          .filter_text = "template function"},
      Protocol::CompletionItem{
          .label = "fori",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) for (size_t i = 0; i < count; ++i)",
          .documentation = "Generates an index-based standard for loop.",
          .insert_text = "for (std::size_t ${1:i} = 0; ${1:i} < ${2:count}; "
                         "++${1:i})\n{\n    $0\n}",
          .filter_text = "fori"},
      Protocol::CompletionItem{
          .label = "foreach",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) for (const auto& item : collection)",
          .documentation = "Generates a range-based for loop.",
          .insert_text =
              "for (const auto& ${1:item} : ${2:collection})\n{\n    $0\n}",
          .filter_text = "foreach"},
      Protocol::CompletionItem{
          .label = "iter",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) for (auto it = c.begin(); it != "
                    "c.end(); ++it)",
          .documentation = "Generates an iterator-based loop.",
          .insert_text = "for (auto ${1:it} = ${2:collection}.begin(); ${1:it} "
                         "!= ${2:collection}.end(); ++${1:it})\n{\n    $0\n}",
          .filter_text = "iter"},
      Protocol::CompletionItem{
          .label = "switch",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) switch (condition) { case ...: "
                    "break; default: break; }",
          .documentation =
              "Generates a switch-case statement with default branch.",
          .insert_text = "switch (${1:condition})\n{\ncase ${2:value}:\n    "
                         "$0\n    break;\ndefault:\n    break;\n}",
          .filter_text = "switch"},
      Protocol::CompletionItem{
          .label = "try",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) try { ... } catch (const "
                    "std::exception& e) { ... }",
          .documentation =
              "Generates a try-catch block catching std::exception.",
          .insert_text = "try\n{\n    $0\n}\ncatch (const std::exception& "
                         "${1:e})\n{\n    $0\n}",
          .filter_text = "try"},
      Protocol::CompletionItem{
          .label = "lambda",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) [&]() { ... }",
          .documentation = "Generates a C++ lambda expression.",
          .insert_text = "[${1:&}](${2:/*params*/})\n{\n    $0\n}",
          .filter_text = "lambda"},
      Protocol::CompletionItem{
          .label = "main",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) int main(int argc, char* argv[])",
          .documentation =
              "Generates the standard C++ application entry point.",
          .insert_text =
              "int main(int argc, char* argv[])\n{\n    $0\n    return 0;\n}",
          .filter_text = "main"},
      Protocol::CompletionItem{
          .label = "guard",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) #pragma once",
          .documentation = "Generates a modern include guard directive.",
          .insert_text = "#pragma once\n\n$0",
          .filter_text = "guard"},
      Protocol::CompletionItem{
          .label = "singleton",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail =
              "(JetBrains Template) static ClassName& instance() noexcept;",
          .documentation =
              "Generates thread-safe Meyer's Singleton method pattern.",
          .insert_text =
              "static ${1:ClassName}& instance() noexcept\n{\n    static "
              "${1:ClassName} s_instance;\n    return s_instance;\n}",
          .filter_text = "singleton"},
      Protocol::CompletionItem{
          .label = "pimpl",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) PImpl idiom pointer & struct",
          .documentation = "Generates standard Pointer-to-Implementation "
                           "(PImpl) declaration.",
          .insert_text = "struct Impl;\nstd::unique_ptr<Impl> m_impl;",
          .filter_text = "pimpl"},
      Protocol::CompletionItem{
          .label = "castu",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) std::make_unique<Type>(...)",
          .documentation =
              "Creates and wraps object in modern std::unique_ptr.",
          .insert_text = "std::make_unique<${1:Type}>(${2})$0",
          .filter_text = "castu"},
      Protocol::CompletionItem{
          .label = "casts",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) std::make_shared<Type>(...)",
          .documentation =
              "Creates and wraps object in modern std::shared_ptr.",
          .insert_text = "std::make_shared<${1:Type}>(${2})$0",
          .filter_text = "casts"},

      // Preprocessor Directives
      Protocol::CompletionItem{.label = "#define",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Macro Definition",
                               .documentation =
                                   "Defines a preprocessor macro or constant.",
                               .insert_text = "#define ${1:NAME} ${2:VALUE}",
                               .filter_text = "#define"},
      Protocol::CompletionItem{.label = "#elif",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Else-If",
                               .documentation =
                                   "Conditional compilation branch.",
                               .insert_text = "#elif ${1:CONDITION}",
                               .filter_text = "#elif"},
      Protocol::CompletionItem{.label = "#elifdef",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Else-Ifdef (C++23)",
                               .documentation =
                                   "Conditional compilation if defined.",
                               .insert_text = "#elifdef ${1:MACRO}",
                               .filter_text = "#elifdef"},
      Protocol::CompletionItem{.label = "#elifndef",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Else-Ifndef (C++23)",
                               .documentation =
                                   "Conditional compilation if not defined.",
                               .insert_text = "#elifndef ${1:MACRO}",
                               .filter_text = "#elifndef"},
      Protocol::CompletionItem{
          .label = "#else",
          .kind = Protocol::CompletionItemKind::Keyword,
          .detail = "Preprocessor Else",
          .documentation = "Alternative conditional compilation branch.",
          .insert_text = "#else",
          .filter_text = "#else"},
      Protocol::CompletionItem{
          .label = "#embed",
          .kind = Protocol::CompletionItemKind::Keyword,
          .detail = "Preprocessor Binary Embed (C++26)",
          .documentation = "Embeds binary resources directly into source.",
          .insert_text = "#embed \"${1:file}\"",
          .filter_text = "#embed"},
      Protocol::CompletionItem{.label = "#endif",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor End-If",
                               .documentation =
                                   "Ends a conditional compilation block.",
                               .insert_text = "#endif",
                               .filter_text = "#endif"},
      Protocol::CompletionItem{.label = "#error",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Compile Error",
                               .documentation =
                                   "Emits a compile-time error diagnostic.",
                               .insert_text = "#error \"${1:message}\"",
                               .filter_text = "#error"},
      Protocol::CompletionItem{.label = "#ifdef",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor If Defined",
                               .documentation =
                                   "Compiles block if macro is defined.",
                               .insert_text = "#ifdef ${1:MACRO}\n$0\n#endif",
                               .filter_text = "#ifdef"},
      Protocol::CompletionItem{
          .label = "#ifndef",
          .kind = Protocol::CompletionItemKind::Keyword,
          .detail = "Preprocessor If Not Defined",
          .documentation = "Compiles block if macro is not defined.",
          .insert_text =
              "#ifndef ${1:MACRO}\n#define ${1:MACRO}\n\n$0\n\n#endif",
          .filter_text = "#ifndef"},
      Protocol::CompletionItem{
          .label = "#import",
          .kind = Protocol::CompletionItemKind::Keyword,
          .detail = "Preprocessor Header Import",
          .documentation = "Imports header file (Obj-C / C++ extension).",
          .insert_text = "#import <${1:header}>",
          .filter_text = "#import"},
      Protocol::CompletionItem{.label = "#include",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Header Inclusion",
                               .documentation =
                                   "Includes standard or user header file.",
                               .insert_text = "#include <${1:header}>",
                               .filter_text = "#include"},
      Protocol::CompletionItem{.label = "#line",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Line Numbering",
                               .documentation =
                                   "Sets source line number and filename.",
                               .insert_text = "#line ${1:number}",
                               .filter_text = "#line"},
      Protocol::CompletionItem{
          .label = "#pragma",
          .kind = Protocol::CompletionItemKind::Keyword,
          .detail = "Preprocessor Compiler Pragma",
          .documentation = "Issues implementation-defined compiler directive.",
          .insert_text = "#pragma ${1:directive}",
          .filter_text = "#pragma"},
      Protocol::CompletionItem{
          .label = "#pragma once",
          .kind = Protocol::CompletionItemKind::Keyword,
          .detail = "Preprocessor Modern Include Guard",
          .documentation =
              "Ensures the file is included only once during compilation.",
          .insert_text = "#pragma once\n\n$0",
          .filter_text = "#pragma once"},
      Protocol::CompletionItem{.label = "#undef",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Undefine Macro",
                               .documentation =
                                   "Undefines a previously defined macro.",
                               .insert_text = "#undef ${1:MACRO}",
                               .filter_text = "#undef"},
      Protocol::CompletionItem{.label = "#warning",
                               .kind = Protocol::CompletionItemKind::Keyword,
                               .detail = "Preprocessor Compile Warning",
                               .documentation =
                                   "Emits a compile-time warning diagnostic.",
                               .insert_text = "#warning \"${1:message}\"",
                               .filter_text = "#warning"},
  };
}

std::vector<Protocol::CompletionItem> get_jetbrains_rust_templates() {
  return {
      Protocol::CompletionItem{
          .label = "fn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(JetBrains Template) fn name(...) -> ... { ... }",
          .documentation = "Generates standard Rust function with body.",
          .insert_text =
              "fn ${1:name}(${2:/*params*/}) -> ${3:()} {\n    $0\n}",
          .filter_text = "fn"},
      Protocol::CompletionItem{
          .label = "pfn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(JetBrains Template) pub fn name(...) -> ... { ... }",
          .documentation = "Generates public Rust function with body.",
          .insert_text =
              "pub fn ${1:name}(${2:/*params*/}) -> ${3:()} {\n    $0\n}",
          .filter_text = "pfn"},
      Protocol::CompletionItem{
          .label = "afn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(JetBrains Template) async fn name(...) -> ... { ... }",
          .documentation = "Generates asynchronous Rust function with body.",
          .insert_text =
              "async fn ${1:name}(${2:/*params*/}) -> ${3:()} {\n    $0\n}",
          .filter_text = "afn"},
      Protocol::CompletionItem{
          .label = "struct",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(JetBrains Template) struct Name { ... }",
          .documentation = "Generates standard Rust struct with public fields.",
          .insert_text = "struct ${1:Name} {\n    $0\n}",
          .filter_text = "struct"},
      Protocol::CompletionItem{
          .label = "enum",
          .kind = Protocol::CompletionItemKind::Enum,
          .detail = "(JetBrains Template) enum Name { ... }",
          .documentation = "Generates standard Rust enum with variants.",
          .insert_text = "enum ${1:Name} {\n    $0\n}",
          .filter_text = "enum"},
      Protocol::CompletionItem{
          .label = "impl",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(JetBrains Template) impl Name { ... }",
          .documentation = "Generates implementation block for struct or enum.",
          .insert_text = "impl ${1:Name} {\n    $0\n}",
          .filter_text = "impl"},
      Protocol::CompletionItem{
          .label = "match",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) match expr { ... }",
          .documentation = "Generates exhaustive pattern match expression.",
          .insert_text = "match ${1:expr} {\n    ${2:pattern} => $0,\n}",
          .filter_text = "match"},
      Protocol::CompletionItem{
          .label = "iflet",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) if let Some(...) = ... { ... }",
          .documentation = "Generates if-let conditional pattern match.",
          .insert_text = "if let Some(${1:val}) = ${2:opt} {\n    $0\n}",
          .filter_text = "iflet"},
      Protocol::CompletionItem{
          .label = "println",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) println!(\"...\", ...);",
          .documentation = "Prints formatted string to stdout with newline.",
          .insert_text = "println!(\"${1:{}}\", $0);",
          .filter_text = "println"}};
}

std::vector<Protocol::CompletionItem> get_jetbrains_python_templates() {
  return {
      Protocol::CompletionItem{
          .label = "def",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(JetBrains Template) def function_name(...):",
          .documentation = "Generates Python function definition.",
          .insert_text = "def ${1:func_name}(${2:/*args*/}):\n    \"\"\""
                         "${3:Docstring}\"\"\"\n    $0",
          .filter_text = "def"},
      Protocol::CompletionItem{
          .label = "class",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(JetBrains Template) class ClassName:",
          .documentation =
              "Generates Python class definition with constructor.",
          .insert_text = "class ${1:ClassName}:\n    def __init__(self, "
                         "${2:/*args*/}):\n        $0",
          .filter_text = "class"},
      Protocol::CompletionItem{
          .label = "main",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) if __name__ == '__main__':",
          .documentation = "Standard Python executable script entry point.",
          .insert_text = "if __name__ == \"__main__\":\n    $0",
          .filter_text = "main"},
      Protocol::CompletionItem{
          .label = "try",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) try ... except Exception as e:",
          .documentation = "Generates Python try-except error handling block.",
          .insert_text = "try:\n    $0\nexcept Exception as ${1:e}:\n    raise",
          .filter_text = "try"}};
}

std::vector<Protocol::CompletionItem>
get_jetbrains_typescript_templates(bool is_jsx) {
  std::vector<Protocol::CompletionItem> templates = {
      Protocol::CompletionItem{
          .label = "interface",
          .kind = Protocol::CompletionItemKind::Interface,
          .detail = "(JetBrains Template) interface Name { ... }",
          .documentation = "Generates TypeScript interface definition.",
          .insert_text = "interface ${1:Name} {\n    $0\n}",
          .filter_text = "interface"},
      Protocol::CompletionItem{
          .label = "type",
          .kind = Protocol::CompletionItemKind::TypeParameter,
          .detail = "(JetBrains Template) type Name = ...;",
          .documentation = "Generates TypeScript type alias declaration.",
          .insert_text = "type ${1:Name} = $0;",
          .filter_text = "type"},
      Protocol::CompletionItem{
          .label = "afn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(JetBrains Template) const name = async (...) => { ... }",
          .documentation = "Generates asynchronous arrow function expression.",
          .insert_text =
              "const ${1:name} = async (${2:/*params*/}) => {\n    $0\n};",
          .filter_text = "afn"},
      Protocol::CompletionItem{
          .label = "clg",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) console.log(...)",
          .documentation = "Logs output to developer debugging console.",
          .insert_text = "console.log($0);",
          .filter_text = "clg"},
      Protocol::CompletionItem{
          .label = "import",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) import { ... } from '...';",
          .documentation = "Imports named module members from a package.",
          .insert_text = "import { ${1:name} } from '${2:module}';$0",
          .filter_text = "import"}};

  if (is_jsx) {
    templates.push_back(Protocol::CompletionItem{
        .label = "rfc",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JetBrains Template) export const Component: React.FC = () "
                  "=> { ... }",
        .documentation = "Generates React Functional Component with TypeScript "
                         "type signature.",
        .insert_text = "import React from 'react';\n\ninterface "
                       "${1:Component}Props {\n    $0\n}\n\nexport const "
                       "${1:Component}: React.FC<${1:Component}Props> = "
                       "(props) => {\n    return (\n        <div>\n          "
                       "  \n        </div>\n    );\n};",
        .filter_text = "rfc"});
  }

  return templates;
}

std::vector<Protocol::CompletionItem> get_jetbrains_html_templates() {
  return {
      Protocol::CompletionItem{
          .label = "html:5",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) HTML5 Document Boilerplate",
          .documentation =
              "Generates standard semantic HTML5 skeleton document.",
          .insert_text = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    "
                         "<meta charset=\"UTF-8\">\n    <meta "
                         "name=\"viewport\" content=\"width=device-width, "
                         "initial-scale=1.0\">\n    "
                         "<title>${1:Document}</title>\n</head>\n<body>\n    "
                         "$0\n</body>\n</html>",
          .filter_text = "html:5"},
      Protocol::CompletionItem{
          .label = "div",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) <div class=\"...\">...</div>",
          .documentation = "Generates HTML div container element.",
          .insert_text = "<div class=\"${1:name}\">\n    $0\n</div>",
          .filter_text = "div"},
      Protocol::CompletionItem{
          .label = "button",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(JetBrains Template) <button type=\"...\">...</button>",
          .documentation = "Generates interactive HTML button element.",
          .insert_text =
              "<button type=\"${1:button}\" class=\"${2:btn}\">$0</button>",
          .filter_text = "button"}};
}

} // namespace

std::vector<Protocol::CompletionItem>
LanguageServerManager::get_templates_for_filename(std::string_view filename) {
  const std::filesystem::path p(filename);
  const std::string ext = p.extension().string();

  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
      ext == ".hpp" || ext == ".hxx" || ext == ".c" || ext == ".inl") {
    return get_jetbrains_cpp_templates();
  }
  if (ext == ".rs") {
    return get_jetbrains_rust_templates();
  }
  if (ext == ".py" || ext == ".pyw") {
    return get_jetbrains_python_templates();
  }
  if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx" ||
      ext == ".mjs" || ext == ".cjs") {
    return get_jetbrains_typescript_templates(ext == ".tsx" || ext == ".jsx");
  }
  if (ext == ".html" || ext == ".htm" || ext == ".xhtml") {
    return get_jetbrains_html_templates();
  }
  return {};
}

LanguageServerManager &LanguageServerManager::instance() noexcept {
  static LanguageServerManager manager;
  return manager;
}

LanguageServerManager::LanguageServerManager() {}

LanguageServerManager::~LanguageServerManager() { shutdown_all(); }

void LanguageServerManager::set_workspace_root(
    std::filesystem::path root_path) {
  m_workspace_root = std::move(root_path);
}

Client::ILanguageClient *
LanguageServerManager::get_or_start_client_for_file(std::string_view filename) {
  std::string fname_str(filename);
  if (fname_str.empty() ||
      fname_str.find("Untitled") != std::string_view::npos ||
      fname_str.find("untitled") != std::string_view::npos) {
    fname_str = "untitled.cpp";
  }

  const auto *profile =
      Registry::ServerRegistry::instance().find_profile_for_filename(fname_str);
  if (profile == nullptr) {
    lsp_debug_log("[zde-lsp] NO PROFILE for " + fname_str);
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(m_clients_mutex);
  if (auto it = m_clients.find(profile->language_id); it != m_clients.end()) {
    return it->second.get();
  }

  // Locate the language server executable (e.g. clangd.exe, rust-analyzer.exe,
  // etc.)
  const std::filesystem::path exe_path =
      Registry::ServerRegistry::instance().find_executable_in_system(
          profile->executable_name);
  if (exe_path.empty()) {
    lsp_debug_log("[zde-lsp] EXE NOT FOUND for " +
                  std::string(profile->executable_name));
    return nullptr;
  }
  lsp_debug_log("[zde-lsp] exe=" + exe_path.generic_string());

  std::vector<std::string> args = profile->default_args;
  if (profile->language_id == "cpp") {
    std::error_code ec;
    std::vector<std::filesystem::path> search_roots;
    if (!m_workspace_root.empty()) {
      search_roots.push_back(m_workspace_root);
    }
    std::filesystem::path cur = std::filesystem::current_path(ec);
    for (int i = 0; i < 8 && !cur.empty(); ++i) {
      search_roots.push_back(cur);
      const auto parent = cur.parent_path();
      if (parent == cur)
        break;
      cur = parent;
    }

    std::filesystem::path found_compile_dir;
    for (const auto &root : search_roots) {
      if (root.empty())
        continue;
#if defined(_WIN32)
      const std::filesystem::path direct_candidates[] = {
          root / "compile_commands.json",
          root / "build" / "compile_commands.json",
          root / "build" / "windows-x64-clang-ninja-debug" /
              "compile_commands.json",
          root / "build" / "windows-x64-clang-ninja-release" /
              "compile_commands.json",
          root / "build" / "windows-x64-ninja-debug" / "compile_commands.json",
          root / "build" / "windows-x64-ninja-release" /
              "compile_commands.json",
          root / "build" / "windows-debug" / "compile_commands.json",
          root / "build" / "windows-release" / "compile_commands.json",
          root / "build" / "clang-debug" / "compile_commands.json",
          root / "build" / "clang-release" / "compile_commands.json",
          root / "build" / "ninja-debug" / "compile_commands.json",
          root / "build" / "ninja-release" / "compile_commands.json",
          root / "build" / "Debug" / "compile_commands.json",
          root / "build" / "Release" / "compile_commands.json",
      };
#elif defined(__APPLE__)
      const std::filesystem::path direct_candidates[] = {
          root / "compile_commands.json",
          root / "build" / "compile_commands.json",
          root / "build" / "macos-debug" / "compile_commands.json",
          root / "build" / "macos-release" / "compile_commands.json",
          root / "build" / "clang-debug" / "compile_commands.json",
          root / "build" / "clang-release" / "compile_commands.json",
          root / "build" / "ninja-debug" / "compile_commands.json",
          root / "build" / "ninja-release" / "compile_commands.json",
          root / "build" / "Debug" / "compile_commands.json",
          root / "build" / "Release" / "compile_commands.json",
      };
#else
      const std::filesystem::path direct_candidates[] = {
          root / "compile_commands.json",
          root / "build" / "compile_commands.json",
          root / "build" / "linux-debug" / "compile_commands.json",
          root / "build" / "linux-release" / "compile_commands.json",
          root / "build" / "clang-debug" / "compile_commands.json",
          root / "build" / "clang-release" / "compile_commands.json",
          root / "build" / "ninja-debug" / "compile_commands.json",
          root / "build" / "ninja-release" / "compile_commands.json",
          root / "build" / "Debug" / "compile_commands.json",
          root / "build" / "Release" / "compile_commands.json",
      };
#endif
      for (const auto &cand : direct_candidates) {
        if (std::filesystem::exists(cand, ec)) {
          found_compile_dir = cand.parent_path();
          break;
        }
      }
      if (!found_compile_dir.empty())
        break;

      const std::filesystem::path build_dir = root / "build";
      if (std::filesystem::exists(build_dir, ec) &&
          std::filesystem::is_directory(build_dir, ec)) {
        std::vector<std::filesystem::path> subdirs;
        for (const auto &entry :
             std::filesystem::directory_iterator(build_dir, ec)) {
          if (entry.is_directory()) {
            const auto sub_cc = entry.path() / "compile_commands.json";
            if (std::filesystem::exists(sub_cc, ec)) {
              subdirs.push_back(entry.path());
            }
          }
        }
        auto get_platform_score = [](const std::filesystem::path &p) -> int {
          std::string name = p.filename().string();
          std::transform(name.begin(), name.end(), name.begin(),
                         [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                         });
#if defined(_WIN32)
          if (name.find("win") != std::string::npos)
            return 100;
          if (name.find("clang") != std::string::npos ||
              name.find("ninja") != std::string::npos)
            return 50;
          if (name.find("linux") != std::string::npos ||
              name.find("macos") != std::string::npos ||
              name.find("darwin") != std::string::npos)
            return -100;
#elif defined(__APPLE__)
          if (name.find("mac") != std::string::npos ||
              name.find("darwin") != std::string::npos)
            return 100;
          if (name.find("clang") != std::string::npos ||
              name.find("ninja") != std::string::npos)
            return 50;
          if (name.find("linux") != std::string::npos ||
              name.find("win") != std::string::npos)
            return -100;
#else
          if (name.find("linux") != std::string::npos)
            return 100;
          if (name.find("clang") != std::string::npos ||
              name.find("ninja") != std::string::npos)
            return 50;
          if (name.find("win") != std::string::npos ||
              name.find("macos") != std::string::npos ||
              name.find("darwin") != std::string::npos)
            return -100;
#endif
          return 0;
        };
        std::sort(subdirs.begin(), subdirs.end(),
                  [&](const auto &a, const auto &b) {
                    return get_platform_score(a) > get_platform_score(b);
                  });
        if (!subdirs.empty() && get_platform_score(subdirs.front()) >= 0) {
          found_compile_dir = subdirs.front();
        }
      }
      if (!found_compile_dir.empty())
        break;
    }

    if (!found_compile_dir.empty()) {
      std::erase_if(args, [](const std::string &a) {
        return a.starts_with("--compile-commands-dir");
      });
      args.push_back("--compile-commands-dir=" +
                     found_compile_dir.generic_string());
    }

    // Auto-inject system include directories discovered by ToolchainDetector so
    // STL and Windows SDK headers always resolve const auto &toolchain =
    // Toolchain::ToolchainDetector::instance().get_active_toolchain(); for
    // (const auto &inc : toolchain.system_include_paths) {
    //   if (!inc.empty()) {
    //     args.push_back("--extra-arg=-isystem" + inc.generic_string());
    //   }
    // }

    lsp_debug_log(found_compile_dir.empty()
                      ? "[zde-lsp] no compile_commands.json found"
                      : "[zde-lsp] compile-dir=" +
                            found_compile_dir.generic_string());
  }

  auto transport = std::make_unique<Transport::StdioProcessTransport>(
      exe_path, std::move(args), m_workspace_root);
  transport->set_error_handler(
      [lang_id = profile->language_id](std::string_view err) {
        lsp_debug_log("[zde-lsp error " + lang_id + "] " + std::string(err));
      });

  auto client = std::make_unique<Client::LanguageClient>(
      profile->language_id, std::move(transport), m_workspace_root);

  client->set_diagnostics_handler(
      [this](const std::string &uri,
             const std::vector<Protocol::Diagnostic> &diags) {
        {
          std::lock_guard<std::mutex> lock(m_clients_mutex);
          m_document_diagnostics[uri] = diags;
        }
        if (m_diagnostics_callback) {
          m_diagnostics_callback(uri, diags);
        }
      });

  if (client->start()) {
    lsp_debug_log("[zde-lsp] client started for " + profile->language_id);
    auto *ptr = client.get();
    m_clients[profile->language_id] = std::move(client);
    return ptr;
  }

  lsp_debug_log("[zde-lsp] client START FAILED for " + profile->language_id);
  return nullptr;
}

static std::string
determine_lsp_language_id(std::string_view filename,
                          const std::string &default_lang_id) {
  const std::filesystem::path p(filename);
  const std::string ext = p.extension().string();
  if (ext == ".tsx")
    return "typescriptreact";
  if (ext == ".jsx")
    return "javascriptreact";
  if (ext == ".ts" || ext == ".mts" || ext == ".cts")
    return "typescript";
  if (ext == ".js" || ext == ".mjs" || ext == ".cjs")
    return "javascript";
  if (ext == ".html" || ext == ".htm" || ext == ".xhtml")
    return "html";
  return default_lang_id;
}

void LanguageServerManager::on_document_opened(const std::string &uri,
                                               std::string_view filename,
                                               int version,
                                               std::string_view content) {
  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    if (client->is_document_open(uri)) {
      return;
    }
    const auto *profile =
        Registry::ServerRegistry::instance().find_profile_for_filename(
            filename);
    const std::string base_lang_id =
        profile != nullptr ? profile->language_id : "plaintext";
    const std::string lang_id =
        determine_lsp_language_id(filename, base_lang_id);

    client->did_open(uri, lang_id, version, content);
    request_semantic_tokens(uri, filename);
  }
}

void LanguageServerManager::on_document_changed(const std::string &uri,
                                                std::string_view filename,
                                                int version,
                                                std::string_view content) {
  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    client->did_change(uri, version, content);
    request_semantic_tokens(uri, filename);
  }
}

void LanguageServerManager::on_document_saved(const std::string &uri,
                                              std::string_view filename) {
  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    client->did_save(uri);
  }
}

void LanguageServerManager::on_document_closed(const std::string &uri,
                                               std::string_view filename) {
  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    client->did_close(uri);
  }
  m_semantic_tokens_manager.clear_document_tokens(uri);
}

void LanguageServerManager::request_completion(
    const std::string &uri, std::string_view filename,
    const Protocol::Position &pos, std::string_view line_text,
    std::function<void(std::vector<Protocol::CompletionItem>)> callback) {
  const std::filesystem::path p(filename);
  const std::string ext = p.extension().string();
  const std::string fname = p.filename().string();
  const bool is_cmake = (fname == "CMakeLists.txt" ||
                         fname == "cmakelists.txt" || ext == ".cmake");

  if (is_cmake) {
    auto cmake_items =
        CMake::CMakeLanguageDatabase::instance().get_completions_for_context(
            line_text, pos.character);
    auto *client = get_or_start_client_for_file(filename);
    if (client != nullptr &&
        (client->is_active() ||
         client->get_state() == Client::ClientState::Initializing)) {
      client->request_completion(
          uri, pos,
          [callback = std::move(callback),
           cmake_items = std::move(cmake_items)](
              std::vector<Protocol::CompletionItem> items) mutable {
            std::unordered_set<std::string> seen;
            for (const auto &it : items) {
              seen.insert(it.label);
            }
            for (auto &ci : cmake_items) {
              if (seen.insert(ci.label).second) {
                items.push_back(std::move(ci));
              }
            }
            if (callback) {
              callback(std::move(items));
            }
          });
      return;
    }

    if (callback) {
      callback(std::move(cmake_items));
    }
    return;
  }

  // Retrieve base templates for the language
  auto templates = get_templates_for_filename(filename);

  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr &&
      (client->is_active() ||
       client->get_state() == Client::ClientState::Initializing)) {
    client->request_completion(
        uri, pos,
        [callback = std::move(callback), templates = std::move(templates)](
            std::vector<Protocol::CompletionItem> lsp_items) mutable {
          std::unordered_set<std::string> seen_labels;
          std::vector<Protocol::CompletionItem> combined;
          combined.reserve(lsp_items.size() + templates.size());

          // 1. LSP items first
          for (auto &it : lsp_items) {
            if (seen_labels.insert(it.label).second) {
              combined.push_back(std::move(it));
            }
          }

          // 2. Language templates & snippets
          for (auto &tpl : templates) {
            if (seen_labels.insert(tpl.label).second) {
              combined.push_back(std::move(tpl));
            }
          }

          if (callback) {
            callback(std::move(combined));
          }
        });
  } else if (callback) {
    callback(std::move(templates));
  }
}

void LanguageServerManager::request_hover(
    const std::string &uri, std::string_view filename,
    const Protocol::Position &pos, std::string_view line_text,
    std::function<void(std::optional<Protocol::Hover>)> callback) {
  const std::filesystem::path p(filename);
  const std::string ext = p.extension().string();
  const std::string fname = p.filename().string();
  const bool is_cmake = (fname == "CMakeLists.txt" ||
                         fname == "cmakelists.txt" || ext == ".cmake");

  if (is_cmake && !line_text.empty()) {
    std::size_t col = std::min(pos.character, line_text.size());
    std::size_t start = col;
    while (start > 0) {
      const char ch = line_text[start - 1];
      if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' ||
          ch == '$' || ch == '{' || ch == '}') {
        --start;
      } else {
        break;
      }
    }
    std::size_t end = col;
    while (end < line_text.size()) {
      const char ch = line_text[end];
      if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' ||
          ch == '$' || ch == '{' || ch == '}') {
        ++end;
      } else {
        break;
      }
    }

    if (start < end) {
      const std::string_view word = line_text.substr(start, end - start);
      auto hover = CMake::CMakeLanguageDatabase::instance().find_hover(word);
      if (hover.has_value()) {
        if (callback) {
          callback(hover);
        }
        return;
      }
    }
  }

  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    client->request_hover(uri, pos, std::move(callback));
  } else if (callback) {
    callback(std::nullopt);
  }
}

void LanguageServerManager::request_definition(
    const std::string &uri, std::string_view filename,
    const Protocol::Position &pos,
    std::function<void(std::vector<Protocol::Location>)> callback) {
  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    client->request_definition(uri, pos, std::move(callback));
  } else if (callback) {
    callback({});
  }
}

void LanguageServerManager::request_signature_help(
    const std::string &uri, std::string_view filename,
    const Protocol::Position &pos, std::string_view line_text,
    std::function<void(std::optional<Protocol::SignatureHelp>)> callback) {
  const std::filesystem::path p(filename);
  const std::string ext = p.extension().string();
  const std::string fname = p.filename().string();
  const bool is_cmake = (fname == "CMakeLists.txt" ||
                         fname == "cmakelists.txt" || ext == ".cmake");

  if (is_cmake && !line_text.empty()) {
    std::size_t col = std::min(pos.character, line_text.size());
    const std::string_view prefix = line_text.substr(0, col);
    const std::size_t open_paren = prefix.rfind('(');
    if (open_paren != std::string_view::npos) {
      std::size_t cmd_end = open_paren;
      while (cmd_end > 0 &&
             std::isspace(static_cast<unsigned char>(prefix[cmd_end - 1]))) {
        --cmd_end;
      }
      std::size_t cmd_start = cmd_end;
      while (cmd_start > 0) {
        const char ch = prefix[cmd_start - 1];
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
          --cmd_start;
        } else {
          break;
        }
      }

      if (cmd_start < cmd_end) {
        const std::string_view cmd_name =
            prefix.substr(cmd_start, cmd_end - cmd_start);
        auto sig = CMake::CMakeLanguageDatabase::instance().find_signature_help(
            cmd_name);
        if (sig.has_value()) {
          if (callback) {
            callback(sig);
          }
          return;
        }
      }
    }
  }

  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    client->request_signature_help(uri, pos, std::move(callback));
  } else if (callback) {
    callback(std::nullopt);
  }
}

void LanguageServerManager::request_semantic_tokens(
    const std::string &uri, std::string_view filename,
    std::function<void(std::vector<Syntax::SemanticTokenSpan>)> callback) {
  auto *client = get_or_start_client_for_file(filename);
  if (client == nullptr) {
    if (callback)
      callback({});
    return;
  }

  client->request_semantic_tokens(
      uri, [this, uri, callback = std::move(callback)](
               std::optional<Protocol::SemanticTokens> tokens) {
        if (!tokens.has_value() || tokens->data.empty()) {
          if (callback)
            callback({});
          return;
        }

        auto spans =
            Syntax::SemanticTokensManager::decode_lsp_tokens(tokens->data);
        m_semantic_tokens_manager.update_document_tokens(uri, spans);

        if (callback) {
          callback(std::move(spans));
        }
      });
}

void LanguageServerManager::set_diagnostics_callback(
    std::function<void(const std::string &uri,
                       const std::vector<Protocol::Diagnostic> &)>
        callback) {
  m_diagnostics_callback = std::move(callback);
}

std::vector<Protocol::Diagnostic>
LanguageServerManager::get_diagnostics_for_document(
    const std::string &uri) const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(m_clients_mutex));
  if (const auto it = m_document_diagnostics.find(uri);
      it != m_document_diagnostics.end()) {
    return it->second;
  }
  return {};
}

void LanguageServerManager::shutdown_all() {
  std::lock_guard<std::mutex> lock(m_clients_mutex);
  for (auto &[id, client] : m_clients) {
    if (client) {
      client->shutdown();
      client->exit();
    }
  }
  m_clients.clear();
}

} // namespace Zenvra::Language
