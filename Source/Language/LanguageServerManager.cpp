#include "Language/LanguageServerManager.h"
#include "Language/CMake/CMakeLanguageDatabase.h"
#include "Language/Definition/SymbolDefinitionResolver.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/Toolchain/ToolchainDetector.h"
#include "Language/Transport/StdioProcessTransport.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

// Built-in Language Snippets and Templates (Standard Language Templates)
std::vector<Protocol::CompletionItem> get_cpp_templates() {
  return {
      Protocol::CompletionItem{
          .label = "struct",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(Template) struct Name { ... };",
          .documentation =
              "Generates a C++ struct definition with body and semicolon.",
          .insert_text = "struct ${1:Name}\n{\n    $0\n};",
          .filter_text = "struct"},
      Protocol::CompletionItem{
          .label = "class",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(Template) class Name { public: ... };",
          .documentation = "Generates a C++ class definition with constructor, "
                           "destructor and private sections.",
          .insert_text = "class ${1:Name}\n{\npublic:\n    ${1:Name}();\n    "
                         "~${1:Name}();\n\nprivate:\n    $0\n};",
          .filter_text = "class"},
      Protocol::CompletionItem{
          .label = "namespace",
          .kind = Protocol::CompletionItemKind::Module,
          .detail = "(Template) namespace Name { ... } // namespace Name",
          .documentation =
              "Generates a C++ namespace block with matching closing comment.",
          .insert_text =
              "namespace ${1:Name}\n{\n\n$0\n\n} // namespace ${1:Name}",
          .filter_text = "namespace"},
      Protocol::CompletionItem{
          .label = "ns",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) namespace Name { ... }",
          .documentation = "Shortcut to generate a C++ namespace block with "
                           "closing comment.",
          .insert_text =
              "namespace ${1:Name}\n{\n\n$0\n\n} // namespace ${1:Name}",
          .filter_text = "ns"},
      Protocol::CompletionItem{
          .label = "enum class",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(Template) enum class Name : uint32_t { ... };",
          .documentation = "Generates a strongly typed enum class definition.",
          .insert_text = "enum class ${1:Name}\n{\n    $0\n};",
          .filter_text = "enum class"},
      Protocol::CompletionItem{.label = "enum",
                               .kind = Protocol::CompletionItemKind::Class,
                               .detail = "(Template) enum Name { ... };",
                               .documentation = "Generates an enum definition.",
                               .insert_text = "enum ${1:Name}\n{\n    $0\n};",
                               .filter_text = "enum"},
      Protocol::CompletionItem{
          .label = "interface",
          .kind = Protocol::CompletionItemKind::Interface,
          .detail = "(Template) struct IInterface { virtual "
                    "~IInterface() = default; ... };",
          .documentation = "Generates an abstract C++ interface with virtual "
                           "default destructor.",
          .insert_text = "struct I${1:Interface}\n{\n    virtual "
                         "~I${1:Interface}() = default;\n    $0\n};",
          .filter_text = "interface"},
      Protocol::CompletionItem{
          .label = "template struct",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) template <typename T> struct Name { ... };",
          .documentation = "Generates a templated struct definition.",
          .insert_text =
              "template <typename ${1:T}>\nstruct ${2:Name}\n{\n    $0\n};",
          .filter_text = "template struct"},
      Protocol::CompletionItem{
          .label = "template class",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) template <typename T> class Name { ... };",
          .documentation = "Generates a templated class definition.",
          .insert_text =
              "template <typename ${1:T}>\nclass ${2:Name}\n{\npublic:\n    "
              "${2:Name}();\n    ~${2:Name}();\n\nprivate:\n    $0\n};",
          .filter_text = "template class"},
      Protocol::CompletionItem{
          .label = "template function",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) template <typename T> void fn() { ... }",
          .documentation = "Generates a templated function signature and body.",
          .insert_text = "template <typename ${1:T}>\n${2:void} "
                         "${3:function_name}(${4:/*params*/})\n{\n    $0\n}",
          .filter_text = "template function"},
      Protocol::CompletionItem{
          .label = "fori",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) for (size_t i = 0; i < count; ++i)",
          .documentation = "Generates an index-based standard for loop.",
          .insert_text = "for (std::size_t ${1:i} = 0; ${1:i} < ${2:count}; "
                         "++${1:i})\n{\n    $0\n}",
          .filter_text = "fori"},
      Protocol::CompletionItem{
          .label = "foreach",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) for (const auto& item : collection)",
          .documentation = "Generates a range-based for loop.",
          .insert_text =
              "for (const auto& ${1:item} : ${2:collection})\n{\n    $0\n}",
          .filter_text = "foreach"},
      Protocol::CompletionItem{
          .label = "iter",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) for (auto it = c.begin(); it != "
                    "c.end(); ++it)",
          .documentation = "Generates an iterator-based loop.",
          .insert_text = "for (auto ${1:it} = ${2:collection}.begin(); ${1:it} "
                         "!= ${2:collection}.end(); ++${1:it})\n{\n    $0\n}",
          .filter_text = "iter"},
      Protocol::CompletionItem{
          .label = "switch",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) switch (condition) { case ...: "
                    "break; default: break; }",
          .documentation =
              "Generates a switch-case statement with default branch.",
          .insert_text = "switch (${1:condition})\n{\ncase ${2:value}:\n    "
                         "$0\n    break;\ndefault:\n    break;\n}",
          .filter_text = "switch"},
      Protocol::CompletionItem{
          .label = "try",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) try { ... } catch (const "
                    "std::exception& e) { ... }",
          .documentation =
              "Generates a try-catch block catching std::exception.",
          .insert_text = "try\n{\n    $0\n}\ncatch (const std::exception& "
                         "${1:e})\n{\n    $0\n}",
          .filter_text = "try"},
      Protocol::CompletionItem{
          .label = "lambda",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) [&]() { ... }",
          .documentation = "Generates a C++ lambda expression.",
          .insert_text = "[${1:&}](${2:/*params*/})\n{\n    $0\n}",
          .filter_text = "lambda"},
      Protocol::CompletionItem{
          .label = "main",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) int main(int argc, char* argv[])",
          .documentation =
              "Generates the standard C++ application entry point.",
          .insert_text =
              "int main(int argc, char* argv[])\n{\n    $0\n    return 0;\n}",
          .filter_text = "main"},
      Protocol::CompletionItem{
          .label = "guard",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) #pragma once",
          .documentation = "Generates a modern include guard directive.",
          .insert_text = "#pragma once\n\n$0",
          .filter_text = "guard"},
      Protocol::CompletionItem{
          .label = "singleton",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) static ClassName& instance() noexcept;",
          .documentation =
              "Generates thread-safe Meyer's Singleton method pattern.",
          .insert_text =
              "static ${1:ClassName}& instance() noexcept\n{\n    static "
              "${1:ClassName} s_instance;\n    return s_instance;\n}",
          .filter_text = "singleton"},
      Protocol::CompletionItem{
          .label = "pimpl",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) PImpl idiom pointer & struct",
          .documentation = "Generates standard Pointer-to-Implementation "
                           "(PImpl) declaration.",
          .insert_text = "struct Impl;\nstd::unique_ptr<Impl> m_impl;",
          .filter_text = "pimpl"},
      Protocol::CompletionItem{
          .label = "castu",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) std::make_unique<Type>(...)",
          .documentation =
              "Creates and wraps object in modern std::unique_ptr.",
          .insert_text = "std::make_unique<${1:Type}>(${2})$0",
          .filter_text = "castu"},
      Protocol::CompletionItem{
          .label = "casts",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) std::make_shared<Type>(...)",
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

std::vector<Protocol::CompletionItem> get_rust_templates() {
  return {
      Protocol::CompletionItem{
          .label = "fn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(Template) fn name(...) -> ... { ... }",
          .documentation = "Generates standard Rust function with body.",
          .insert_text =
              "fn ${1:name}(${2:/*params*/}) -> ${3:()} {\n    $0\n}",
          .filter_text = "fn"},
      Protocol::CompletionItem{
          .label = "pfn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(Template) pub fn name(...) -> ... { ... }",
          .documentation = "Generates public Rust function with body.",
          .insert_text =
              "pub fn ${1:name}(${2:/*params*/}) -> ${3:()} {\n    $0\n}",
          .filter_text = "pfn"},
      Protocol::CompletionItem{
          .label = "afn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(Template) async fn name(...) -> ... { ... }",
          .documentation = "Generates asynchronous Rust function with body.",
          .insert_text =
              "async fn ${1:name}(${2:/*params*/}) -> ${3:()} {\n    $0\n}",
          .filter_text = "afn"},
      Protocol::CompletionItem{
          .label = "struct",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(Template) struct Name { ... }",
          .documentation = "Generates standard Rust struct with public fields.",
          .insert_text = "struct ${1:Name} {\n    $0\n}",
          .filter_text = "struct"},
      Protocol::CompletionItem{
          .label = "enum",
          .kind = Protocol::CompletionItemKind::Enum,
          .detail = "(Template) enum Name { ... }",
          .documentation = "Generates standard Rust enum with variants.",
          .insert_text = "enum ${1:Name} {\n    $0\n}",
          .filter_text = "enum"},
      Protocol::CompletionItem{
          .label = "impl",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(Template) impl Name { ... }",
          .documentation = "Generates implementation block for struct or enum.",
          .insert_text = "impl ${1:Name} {\n    $0\n}",
          .filter_text = "impl"},
      Protocol::CompletionItem{
          .label = "match",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) match expr { ... }",
          .documentation = "Generates exhaustive pattern match expression.",
          .insert_text = "match ${1:expr} {\n    ${2:pattern} => $0,\n}",
          .filter_text = "match"},
      Protocol::CompletionItem{
          .label = "iflet",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) if let Some(...) = ... { ... }",
          .documentation = "Generates if-let conditional pattern match.",
          .insert_text = "if let Some(${1:val}) = ${2:opt} {\n    $0\n}",
          .filter_text = "iflet"},
      Protocol::CompletionItem{
          .label = "println",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) println!(\"...\", ...);",
          .documentation = "Prints formatted string to stdout with newline.",
          .insert_text = "println!(\"${1:{}}\", $0);",
          .filter_text = "println"}};
}

std::vector<Protocol::CompletionItem> get_python_templates() {
  return {
      Protocol::CompletionItem{
          .label = "def",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(Template) def function_name(...):",
          .documentation = "Generates Python function definition.",
          .insert_text = "def ${1:func_name}(${2:/*args*/}):\n    \"\"\""
                         "${3:Docstring}\"\"\"\n    $0",
          .filter_text = "def"},
      Protocol::CompletionItem{
          .label = "class",
          .kind = Protocol::CompletionItemKind::Class,
          .detail = "(Template) class ClassName:",
          .documentation =
              "Generates Python class definition with constructor.",
          .insert_text = "class ${1:ClassName}:\n    def __init__(self, "
                         "${2:/*args*/}):\n        $0",
          .filter_text = "class"},
      Protocol::CompletionItem{
          .label = "main",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) if __name__ == '__main__':",
          .documentation = "Standard Python executable script entry point.",
          .insert_text = "if __name__ == \"__main__\":\n    $0",
          .filter_text = "main"},
      Protocol::CompletionItem{
          .label = "try",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) try ... except Exception as e:",
          .documentation = "Generates Python try-except error handling block.",
          .insert_text = "try:\n    $0\nexcept Exception as ${1:e}:\n    raise",
          .filter_text = "try"}};
}

std::vector<Protocol::CompletionItem> get_typescript_templates(bool is_jsx) {
  std::vector<Protocol::CompletionItem> templates = {
      Protocol::CompletionItem{
          .label = "interface",
          .kind = Protocol::CompletionItemKind::Interface,
          .detail = "(Template) interface Name { ... }",
          .documentation = "Generates TypeScript interface definition.",
          .insert_text = "interface ${1:Name} {\n    $0\n}",
          .filter_text = "interface"},
      Protocol::CompletionItem{
          .label = "type",
          .kind = Protocol::CompletionItemKind::TypeParameter,
          .detail = "(Template) type Name = ...;",
          .documentation = "Generates TypeScript type alias declaration.",
          .insert_text = "type ${1:Name} = $0;",
          .filter_text = "type"},
      Protocol::CompletionItem{
          .label = "afn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(Template) const name = async (...) => { ... }",
          .documentation = "Generates asynchronous arrow function expression.",
          .insert_text =
              "const ${1:name} = async (${2:/*params*/}) => {\n    $0\n};",
          .filter_text = "afn"},
      Protocol::CompletionItem{
          .label = "fn",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(Template) const name = (...) => { ... }",
          .documentation = "Generates arrow function expression.",
          .insert_text =
              "const ${1:name} = (${2:/*params*/}) => {\n    $0\n};",
          .filter_text = "fn"},
      Protocol::CompletionItem{
          .label = "clg",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) console.log(...)",
          .documentation = "Logs output to developer debugging console.",
          .insert_text = "console.log($0);",
          .filter_text = "clg"},
      Protocol::CompletionItem{
          .label = "import",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) import { ... } from '...';",
          .documentation = "Imports named module members from a package.",
          .insert_text = "import { ${1:name} } from '${2:module}';$0",
          .filter_text = "import"}};

  if (is_jsx) {
    templates.push_back(Protocol::CompletionItem{
        .label = "rfc",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Template) React Functional Component",
        .documentation = "Generates React Functional Component with TypeScript props interface.",
        .insert_text = "import React from 'react';\n\ninterface ${1:Component}Props {\n    $0\n}\n\nexport const ${1:Component}: React.FC<${1:Component}Props> = (props) => {\n    return (\n        <div className=\"${2:container}\">\n            \n        </div>\n    );\n};",
        .filter_text = "rfc"});
    templates.push_back(Protocol::CompletionItem{
        .label = "rfce",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Template) React Functional Component (Export Default)",
        .documentation = "Generates standard React Functional Component with default export.",
        .insert_text = "import React from 'react';\n\nfunction ${1:Component}() {\n    return (\n        <div>\n            $0\n        </div>\n    );\n}\n\nexport default ${1:Component};",
        .filter_text = "rfce"});
    templates.push_back(Protocol::CompletionItem{
        .label = "rafce",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Template) React Arrow Function Component (Export Default)",
        .documentation = "Generates React arrow function component with default export.",
        .insert_text = "import React from 'react';\n\nconst ${1:Component} = () => {\n    return (\n        <div>\n            $0\n        </div>\n    );\n};\n\nexport default ${1:Component};",
        .filter_text = "rafce"});

    // React Hooks
    templates.push_back(Protocol::CompletionItem{
        .label = "useState",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Hook) const [state, setState] = useState(...)",
        .documentation = "Declares a React state variable with updater function.",
        .insert_text = "const [${1:state}, set${1:State}] = useState(${2:initialState});",
        .filter_text = "useState"});
    templates.push_back(Protocol::CompletionItem{
        .label = "useEffect",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Hook) useEffect(() => { ... }, [])",
        .documentation = "Runs side-effects in React component lifecycle.",
        .insert_text = "useEffect(() => {\n    $0\n}, [${1}]);",
        .filter_text = "useEffect"});
    templates.push_back(Protocol::CompletionItem{
        .label = "useRef",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Hook) const ref = useRef(null)",
        .documentation = "Creates a persistent mutable React reference.",
        .insert_text = "const ${1:ref} = useRef<${2:HTMLDivElement | null}>(${3:null});",
        .filter_text = "useRef"});
    templates.push_back(Protocol::CompletionItem{
        .label = "useMemo",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Hook) const val = useMemo(() => ..., [])",
        .documentation = "Memoizes expensive computed value between renders.",
        .insert_text = "const ${1:memoized} = useMemo(() => {\n    return $0;\n}, [${2}]);",
        .filter_text = "useMemo"});
    templates.push_back(Protocol::CompletionItem{
        .label = "useCallback",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Hook) const fn = useCallback((...) => { ... }, [])",
        .documentation = "Memoizes callback function instance between renders.",
        .insert_text = "const ${1:handleClick} = useCallback((${2}) => {\n    $0\n}, [${3}]);",
        .filter_text = "useCallback"});
    templates.push_back(Protocol::CompletionItem{
        .label = "useContext",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Hook) const ctx = useContext(Context)",
        .documentation = "Subscribes to nearest React Context provider.",
        .insert_text = "const ${1:value} = useContext(${2:MyContext});",
        .filter_text = "useContext"});
    templates.push_back(Protocol::CompletionItem{
        .label = "useReducer",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(Hook) const [state, dispatch] = useReducer(reducer, init)",
        .documentation = "Manages complex state with reducer dispatch pattern.",
        .insert_text = "const [${1:state}, ${2:dispatch}] = useReducer(${3:reducer}, ${4:initialState});",
        .filter_text = "useReducer"});

    // Emmet & JSX Elements Auto-Generation
    templates.push_back(Protocol::CompletionItem{
        .label = "div",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <div className=\"...\">...</div>",
        .documentation = "Generates HTML div container element.",
        .insert_text = "<div className=\"${1:className}\">\n    $0\n</div>",
        .filter_text = "div"});
    templates.push_back(Protocol::CompletionItem{
        .label = "span",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <span>...</span>",
        .documentation = "Generates inline span element.",
        .insert_text = "<span className=\"${1:className}\">$0</span>",
        .filter_text = "span"});
    templates.push_back(Protocol::CompletionItem{
        .label = "button",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <button className=\"...\" onClick={...}>...</button>",
        .documentation = "Generates button element with click handler.",
        .insert_text = "<button type=\"${1:button}\" className=\"${2:btn}\" onClick={${3:handleClick}}>\n    $0\n</button>",
        .filter_text = "button"});
    templates.push_back(Protocol::CompletionItem{
        .label = "input",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <input type=\"...\" value={...} onChange={...} />",
        .documentation = "Generates controlled form input element.",
        .insert_text = "<input type=\"${1:text}\" placeholder=\"${2:Enter value...}\" value={${3:value}} onChange={${4:onChange}} />",
        .filter_text = "input"});
    templates.push_back(Protocol::CompletionItem{
        .label = "form",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <form onSubmit={...}>...</form>",
        .documentation = "Generates form element with submit handler.",
        .insert_text = "<form onSubmit={${1:handleSubmit}}>\n    $0\n</form>",
        .filter_text = "form"});
    templates.push_back(Protocol::CompletionItem{
        .label = "p",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <p>...</p>",
        .documentation = "Generates paragraph text element.",
        .insert_text = "<p>$0</p>",
        .filter_text = "p"});
    templates.push_back(Protocol::CompletionItem{
        .label = "h1",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <h1>...</h1>",
        .documentation = "Generates level 1 heading element.",
        .insert_text = "<h1>$0</h1>",
        .filter_text = "h1"});
    templates.push_back(Protocol::CompletionItem{
        .label = "h2",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <h2>...</h2>",
        .documentation = "Generates level 2 heading element.",
        .insert_text = "<h2>$0</h2>",
        .filter_text = "h2"});
    templates.push_back(Protocol::CompletionItem{
        .label = "h3",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <h3>...</h3>",
        .documentation = "Generates level 3 heading element.",
        .insert_text = "<h3>$0</h3>",
        .filter_text = "h3"});
    templates.push_back(Protocol::CompletionItem{
        .label = "a",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <a href=\"...\">...</a>",
        .documentation = "Generates hyperlink anchor element.",
        .insert_text = "<a href=\"${1:#}\">$0</a>",
        .filter_text = "a"});
    templates.push_back(Protocol::CompletionItem{
        .label = "img",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <img src=\"...\" alt=\"...\" />",
        .documentation = "Generates self-closing image element.",
        .insert_text = "<img src=\"${1}\" alt=\"${2}\" className=\"${3}\" />",
        .filter_text = "img"});
    templates.push_back(Protocol::CompletionItem{
        .label = "ul",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <ul><li>...</li></ul>",
        .documentation = "Generates unordered list element.",
        .insert_text = "<ul>\n    <li>$0</li>\n</ul>",
        .filter_text = "ul"});
    templates.push_back(Protocol::CompletionItem{
        .label = "li",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <li>...</li>",
        .documentation = "Generates list item element.",
        .insert_text = "<li>$0</li>",
        .filter_text = "li"});
    templates.push_back(Protocol::CompletionItem{
        .label = "ol",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <ol><li>...</li></ol>",
        .documentation = "Generates ordered list element.",
        .insert_text = "<ol>\n    <li>$0</li>\n</ol>",
        .filter_text = "ol"});
    templates.push_back(Protocol::CompletionItem{
        .label = "select",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <select>...</select>",
        .documentation = "Generates dropdown select element.",
        .insert_text = "<select value={${1:value}} onChange={${2:onChange}}>\n    <option value=\"${3}\">${4}</option>\n</select>",
        .filter_text = "select"});
    templates.push_back(Protocol::CompletionItem{
        .label = "option",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <option value=\"...\">...</option>",
        .documentation = "Generates select option element.",
        .insert_text = "<option value=\"${1:value}\">$0</option>",
        .filter_text = "option"});
    templates.push_back(Protocol::CompletionItem{
        .label = "textarea",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <textarea ... />",
        .documentation = "Generates multi-line text input area.",
        .insert_text = "<textarea placeholder=\"${1:Enter text...}\" value={${2:value}} onChange={${3:onChange}} />",
        .filter_text = "textarea"});
    templates.push_back(Protocol::CompletionItem{
        .label = "label",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <label htmlFor=\"...\">...</label>",
        .documentation = "Generates accessible form label element.",
        .insert_text = "<label htmlFor=\"${1:id}\">$0</label>",
        .filter_text = "label"});
    templates.push_back(Protocol::CompletionItem{
        .label = "table",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <table>...</table>",
        .documentation = "Generates table structure element.",
        .insert_text = "<table>\n    <thead>\n        <tr>\n            <th>$1</th>\n        </tr>\n    </thead>\n    <tbody>\n        <tr>\n            <td>$0</td>\n        </tr>\n    </tbody>\n</table>",
        .filter_text = "table"});
    templates.push_back(Protocol::CompletionItem{
        .label = "nav",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <nav>...</nav>",
        .documentation = "Generates navigation section element.",
        .insert_text = "<nav className=\"${1:navbar}\">\n    $0\n</nav>",
        .filter_text = "nav"});
    templates.push_back(Protocol::CompletionItem{
        .label = "header",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <header>...</header>",
        .documentation = "Generates header landmark container element.",
        .insert_text = "<header className=\"${1:header}\">\n    $0\n</header>",
        .filter_text = "header"});
    templates.push_back(Protocol::CompletionItem{
        .label = "footer",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <footer>...</footer>",
        .documentation = "Generates footer landmark container element.",
        .insert_text = "<footer className=\"${1:footer}\">\n    $0\n</footer>",
        .filter_text = "footer"});
    templates.push_back(Protocol::CompletionItem{
        .label = "main",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <main>...</main>",
        .documentation = "Generates main landmark container element.",
        .insert_text = "<main className=\"${1:main}\">\n    $0\n</main>",
        .filter_text = "main"});
    templates.push_back(Protocol::CompletionItem{
        .label = "section",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <section>...</section>",
        .documentation = "Generates section container element.",
        .insert_text = "<section className=\"${1:section}\">\n    $0\n</section>",
        .filter_text = "section"});
    templates.push_back(Protocol::CompletionItem{
        .label = "frag",
        .kind = Protocol::CompletionItemKind::Snippet,
        .detail = "(JSX) <>...</>",
        .documentation = "Generates React Fragment shorthand container.",
        .insert_text = "<>\n    $0\n</>",
        .filter_text = "frag"});

    // JSX Attributes
    templates.push_back(Protocol::CompletionItem{
        .label = "className",
        .kind = Protocol::CompletionItemKind::Property,
        .detail = "(JSX Attr) className=\"...\"",
        .documentation = "CSS class names for JSX element.",
        .insert_text = "className=\"$0\"",
        .filter_text = "className"});
    templates.push_back(Protocol::CompletionItem{
        .label = "style",
        .kind = Protocol::CompletionItemKind::Property,
        .detail = "(JSX Attr) style={{ ... }}",
        .documentation = "Inline CSS style object for JSX element.",
        .insert_text = "style={{ $0 }}",
        .filter_text = "style"});
    templates.push_back(Protocol::CompletionItem{
        .label = "onClick",
        .kind = Protocol::CompletionItemKind::Event,
        .detail = "(JSX Attr) onClick={...}",
        .documentation = "Click event handler function.",
        .insert_text = "onClick={${1:handleClick}}",
        .filter_text = "onClick"});
    templates.push_back(Protocol::CompletionItem{
        .label = "onChange",
        .kind = Protocol::CompletionItemKind::Event,
        .detail = "(JSX Attr) onChange={(e) => ...}",
        .documentation = "Input change event handler function.",
        .insert_text = "onChange={(e) => $0}",
        .filter_text = "onChange"});
    templates.push_back(Protocol::CompletionItem{
        .label = "onSubmit",
        .kind = Protocol::CompletionItemKind::Event,
        .detail = "(JSX Attr) onSubmit={(e) => ...}",
        .documentation = "Form submission event handler function.",
        .insert_text = "onSubmit={(e) => {\n    e.preventDefault();\n    $0\n}}",
        .filter_text = "onSubmit"});
  }

  return templates;
}

std::vector<Protocol::CompletionItem> get_html_templates() {
  return {Protocol::CompletionItem{
              .label = "html:5",
              .kind = Protocol::CompletionItemKind::Snippet,
              .detail = "(Template) HTML5 Document Boilerplate",
              .documentation =
                  "Generates standard semantic HTML5 skeleton document.",
              .insert_text =
                  "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    "
                  "<meta charset=\"UTF-8\">\n    <meta "
                  "name=\"viewport\" content=\"width=device-width, "
                  "initial-scale=1.0\">\n    "
                  "<title>${1:Document}</title>\n</head>\n<body>\n    "
                  "$0\n</body>\n</html>",
              .filter_text = "html:5"},
          Protocol::CompletionItem{
              .label = "div",
              .kind = Protocol::CompletionItemKind::Snippet,
              .detail = "(Template) <div class=\"...\">...</div>",
              .documentation = "Generates HTML div container element.",
              .insert_text = "<div class=\"${1:name}\">\n    $0\n</div>",
              .filter_text = "div"},
          Protocol::CompletionItem{
              .label = "button",
              .kind = Protocol::CompletionItemKind::Snippet,
              .detail = "(Template) <button type=\"...\">...</button>",
              .documentation = "Generates interactive HTML button element.",
              .insert_text =
                  "<button type=\"${1:button}\" class=\"${2:btn}\">$0</button>",
              .filter_text = "button"}};
}

std::vector<Protocol::CompletionItem> get_go_templates() {
  return {
      Protocol::CompletionItem{
          .label = "func",
          .kind = Protocol::CompletionItemKind::Function,
          .detail = "(Template) func Name(...) ... { ... }",
          .documentation = "Generates a Go function declaration.",
          .insert_text = "func ${1:name}(${2:/*args*/}) ${3:error} {\n\t$0\n}",
          .filter_text = "func"},
      Protocol::CompletionItem{
          .label = "main",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) package main ... func main()",
          .documentation = "Generates a Go main package entry point.",
          .insert_text = "package main\n\nimport (\n\t\"fmt\"\n)\n\nfunc "
                         "main() {\n\tfmt.Println(\"$0\")\n}",
          .filter_text = "main"},
      Protocol::CompletionItem{
          .label = "struct",
          .kind = Protocol::CompletionItemKind::Struct,
          .detail = "(Template) type Name struct { ... }",
          .documentation = "Generates a Go struct declaration.",
          .insert_text = "type ${1:Name} struct {\n\t$0\n}",
          .filter_text = "struct"},
      Protocol::CompletionItem{
          .label = "interface",
          .kind = Protocol::CompletionItemKind::Interface,
          .detail = "(Template) type Name interface { ... }",
          .documentation = "Generates a Go interface declaration.",
          .insert_text = "type ${1:Name} interface {\n\t$0\n}",
          .filter_text = "interface"},
      Protocol::CompletionItem{
          .label = "iferr",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) if err != nil { return err }",
          .documentation = "Standard Go error check and return block.",
          .insert_text = "if err != nil {\n\treturn ${1:err}\n}\n$0",
          .filter_text = "iferr"},
      Protocol::CompletionItem{
          .label = "forr",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) for i, item := range items { ... }",
          .documentation = "Generates a Go range loop over a collection.",
          .insert_text =
              "for ${1:i}, ${2:v} := range ${3:collection} {\n\t$0\n}",
          .filter_text = "forr"},
      Protocol::CompletionItem{
          .label = "fori",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Template) for i := 0; i < count; i++ { ... }",
          .documentation = "Generates a standard Go indexed for loop.",
          .insert_text =
              "for ${1:i} := 0; ${1:i} < ${2:count}; ${1:i}++ {\n\t$0\n}",
          .filter_text = "fori"},
      Protocol::CompletionItem{.label = "goroutine",
                               .kind = Protocol::CompletionItemKind::Snippet,
                               .detail = "(Template) go func() { ... }()",
                               .documentation =
                                   "Spawns an anonymous concurrent Goroutine.",
                               .insert_text = "go func() {\n\t$0\n}()",
                               .filter_text = "goroutine"},
      Protocol::CompletionItem{
          .label = "type",
          .kind = Protocol::CompletionItemKind::TypeParameter,
          .detail = "(Template) type Name Type",
          .documentation = "Generates a Go type alias definition.",
          .insert_text = "type ${1:Name} ${2:string}",
          .filter_text = "type"}};
}

std::vector<Protocol::CompletionItem> get_asm_templates() {
  return {
      // --- x86 64-Bit (AMD64 / x86_64) Architecture ---
      Protocol::CompletionItem{
          .label = "x86_64:main (NASM 64-bit)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 64-bit NASM) _start entry point with 64-bit syscall",
          .documentation = "x86 64-bit POSIX/Linux executable entry point using System V AMD64 ABI syscalls (sys_write, sys_exit).",
          .insert_text =
              "default rel\n"
              "global _start\n\n"
              "section .rodata\n"
              "    msg db \"Hello from x86_64 Assembly!\", 10\n"
              "    msg_len equ $ - msg\n\n"
              "section .text\n"
              "_start:\n"
              "    ; write(1, msg, msg_len)\n"
              "    mov rax, 1          ; sys_write\n"
              "    mov rdi, 1          ; stdout\n"
              "    lea rsi, [msg]      ; buffer\n"
              "    mov rdx, msg_len    ; length\n"
              "    syscall\n\n"
              "    ; exit(0)\n"
              "    mov rax, 60         ; sys_exit\n"
              "    xor rdi, rdi        ; status 0\n"
              "    syscall\n",
          .filter_text = "x86_64:main"},
      Protocol::CompletionItem{
          .label = "x86_64:gas (GAS 64-bit)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 64-bit GAS) GNU Assembler entry point",
          .documentation = "x86 64-bit GNU Assembler (.s) Intel syntax entry point with standard Linux 64-bit syscalls.",
          .insert_text =
              ".intel_syntax noprefix\n"
              ".global _start\n\n"
              ".section .rodata\n"
              "msg:\n"
              "    .ascii \"Hello from GAS x86_64!\\n\"\n"
              "    msg_len = . - msg\n\n"
              ".section .text\n"
              "_start:\n"
              "    mov rax, 1          # sys_write\n"
              "    mov rdi, 1          # stdout\n"
              "    lea rsi, [msg]      # buffer\n"
              "    mov rdx, msg_len    # count\n"
              "    syscall\n\n"
              "    mov rax, 60         # sys_exit\n"
              "    xor rdi, rdi        # status = 0\n"
              "    syscall\n",
          .filter_text = "x86_64:gas"},
      Protocol::CompletionItem{
          .label = "x86_64:frame (Stack Frame)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 64-bit) Standard Function Prologue & Epilogue",
          .documentation = "Standard x86_64 stack frame setup and teardown preserving RBP.",
          .insert_text =
              "${1:function_name}:\n"
              "    push rbp\n"
              "    mov rbp, rsp\n"
              "    sub rsp, ${2:16}       ; allocate stack space\n\n"
              "    $0\n\n"
              "    mov rsp, rbp\n"
              "    pop rbp\n"
              "    ret\n",
          .filter_text = "x86_64:frame"},
      Protocol::CompletionItem{
          .label = "x86_64:avx2 (AVX2 SIMD)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 64-bit AVX2) 256-bit Vector Arithmetic",
          .documentation = "AVX2 SIMD vector operations on 256-bit YMM registers with vzeroupper.",
          .insert_text =
              "; AVX2 256-bit Floating-Point Vector Addition\n"
              "vmovaps ymm0, [rdi]        ; load 8x float from src1\n"
              "vmovaps ymm1, [rsi]        ; load 8x float from src2\n"
              "vaddps  ymm2, ymm0, ymm1   ; ymm2 = ymm0 + ymm1\n"
              "vmovaps [rdx], ymm2        ; store result to dest\n"
              "vzeroupper                 ; clear upper 128-bits state\n$0",
          .filter_text = "x86_64:avx2"},
      Protocol::CompletionItem{
          .label = "x86_64:sse (SSE2 Vector)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 64-bit SSE2) 128-bit Vector Processing",
          .documentation = "128-bit SSE2 vector mathematical operation on XMM registers.",
          .insert_text =
              "; SSE2 128-bit Single-Precision Vector Math\n"
              "movaps xmm0, [rdi]         ; load 4x float\n"
              "movaps xmm1, [rsi]         ; load 4x float\n"
              "mulps  xmm0, xmm1          ; xmm0 = xmm0 * xmm1\n"
              "movaps [rdx], xmm0         ; store result\n$0",
          .filter_text = "x86_64:sse"},

      // --- x86 32-Bit (i386 / IA-32) Architecture ---
      Protocol::CompletionItem{
          .label = "x86_32:main (NASM 32-bit)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 32-bit NASM) _start entry point with int 0x80",
          .documentation = "x86 32-bit POSIX/Linux executable entry point using 32-bit system interrupts (int 0x80).",
          .insert_text =
              "global _start\n\n"
              "section .rodata\n"
              "    msg db \"Hello from x86 32-bit Assembly!\", 10\n"
              "    msg_len equ $ - msg\n\n"
              "section .text\n"
              "_start:\n"
              "    ; write(1, msg, msg_len)\n"
              "    mov eax, 4          ; sys_write\n"
              "    mov ebx, 1          ; stdout\n"
              "    mov ecx, msg        ; buffer\n"
              "    mov edx, msg_len    ; count\n"
              "    int 0x80\n\n"
              "    ; exit(0)\n"
              "    mov eax, 1          ; sys_exit\n"
              "    xor ebx, ebx        ; status 0\n"
              "    int 0x80\n",
          .filter_text = "x86_32:main"},
      Protocol::CompletionItem{
          .label = "x86_32:cdecl (cdecl Function)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 32-bit) Standard cdecl Function",
          .documentation = "Standard 32-bit cdecl calling convention function with stack parameter access.",
          .insert_text =
              "${1:func_name}:\n"
              "    push ebp\n"
              "    mov ebp, esp\n"
              "    push ebx\n"
              "    push esi\n"
              "    push edi\n\n"
              "    mov eax, [ebp + 8]   ; arg1\n"
              "    mov edx, [ebp + 12]  ; arg2\n"
              "    $0\n\n"
              "    pop edi\n"
              "    pop esi\n"
              "    pop ebx\n"
              "    mov esp, ebp\n"
              "    pop ebp\n"
              "    ret\n",
          .filter_text = "x86_32:cdecl"},

      // --- x86 16-Bit (Real Mode / BIOS) Architecture ---
      Protocol::CompletionItem{
          .label = "x86_16:realmode (16-bit Real Mode)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(x86 16-bit) Real Mode BIOS entry",
          .documentation = "x86 16-bit real mode BIOS teletype output and execution.",
          .insert_text =
              "bits 16\n"
              "org 0x7c00\n\n"
              "start:\n"
              "    xor ax, ax\n"
              "    mov ds, ax\n"
              "    mov es, ax\n"
              "    mov ss, ax\n"
              "    mov sp, 0x7c00\n\n"
              "    lea si, [msg]\n"
              "print_loop:\n"
              "    lodsb\n"
              "    test al, al\n"
              "    jz halt\n"
              "    mov ah, 0x0e        ; BIOS teletype output\n"
              "    int 0x10\n"
              "    jmp print_loop\n\n"
              "halt:\n"
              "    hlt\n"
              "    jmp halt\n\n"
              "msg db \"Hello from 16-bit Real Mode!\", 0\n",
          .filter_text = "x86_16:realmode"},

      // --- ARM 64-Bit (AArch64 / ARMv8-A / ARMv9) Architecture ---
      Protocol::CompletionItem{
          .label = "arm64:main (AArch64 64-bit)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(ARM 64-bit AArch64) _start entry point with svc #0",
          .documentation = "64-bit ARM AArch64 Linux/POSIX entry point using 64-bit system calls (x8=64 write, x8=93 exit).",
          .insert_text =
              ".global _start\n"
              ".section .rodata\n"
              "msg:\n"
              "    .ascii \"Hello from ARM 64-bit (AArch64)!\\n\"\n"
              "    msg_len = . - msg\n\n"
              ".section .text\n"
              "_start:\n"
              "    // write(1, msg, msg_len)\n"
              "    mov x0, #1          // stdout\n"
              "    adr x1, msg         // buffer address\n"
              "    mov x2, #msg_len    // length\n"
              "    mov x8, #64         // sys_write (Linux AArch64)\n"
              "    svc #0\n\n"
              "    // exit(0)\n"
              "    mov x0, #0          // status\n"
              "    mov x8, #93         // sys_exit (Linux AArch64)\n"
              "    svc #0\n",
          .filter_text = "arm64:main"},
      Protocol::CompletionItem{
          .label = "arm64:frame (AArch64 Function Frame)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(ARM 64-bit) AArch64 Stack Frame with Link Register",
          .documentation = "Standard AArch64 function prologue/epilogue preserving Frame Pointer (x29) and Link Register (x30).",
          .insert_text =
              ".global ${1:function_name}\n"
              ".type ${1:function_name}, %function\n"
              "${1:function_name}:\n"
              "    stp x29, x30, [sp, #-16]!  // save FP and LR\n"
              "    mov x29, sp               // set frame pointer\n\n"
              "    $0\n\n"
              "    ldp x29, x30, [sp], #16   // restore FP and LR\n"
              "    ret\n",
          .filter_text = "arm64:frame"},
      Protocol::CompletionItem{
          .label = "arm64:neon (ARM NEON SIMD)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(ARM 64-bit NEON) 128-bit Vector Processing",
          .documentation = "ARM AArch64 Advanced SIMD (NEON) 128-bit vector floating point arithmetic.",
          .insert_text =
              "// ARM NEON 4x Float Vector Addition\n"
              "ld1 {v0.4s}, [x0]         // load 4x 32-bit floats from [x0]\n"
              "ld1 {v1.4s}, [x1]         // load 4x 32-bit floats from [x1]\n"
              "fadd v2.4s, v0.4s, v1.4s  // v2 = v0 + v1\n"
              "st1 {v2.4s}, [x2]         // store result to [x2]\n$0",
          .filter_text = "arm64:neon"},

      // --- ARM 32-Bit (ARMv7-A / Thumb-2 / AArch32) Architecture ---
      Protocol::CompletionItem{
          .label = "arm32:main (ARM 32-bit)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(ARM 32-bit A32) _start entry point with svc #0",
          .documentation = "32-bit ARM (ARMv7-A) Linux entry point using svc #0 (r7=4 write, r7=1 exit).",
          .insert_text =
              ".syntax unified\n"
              ".arch armv7-a\n"
              ".global _start\n\n"
              ".section .rodata\n"
              "msg:\n"
              "    .ascii \"Hello from ARM 32-bit!\\n\"\n"
              "    msg_len = . - msg\n\n"
              ".section .text\n"
              "_start:\n"
              "    @ write(1, msg, msg_len)\n"
              "    mov r0, #1          @ stdout\n"
              "    ldr r1, =msg        @ buffer\n"
              "    mov r2, #msg_len    @ count\n"
              "    mov r7, #4          @ sys_write\n"
              "    svc #0\n\n"
              "    @ exit(0)\n"
              "    mov r0, #0          @ status\n"
              "    mov r7, #1          @ sys_exit\n"
              "    svc #0\n",
          .filter_text = "arm32:main"},
      Protocol::CompletionItem{
          .label = "arm32:thumb2 (ARM Thumb-2)",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(ARM 32-bit Thumb-2) Function Definition",
          .documentation = "ARM Thumb-2 high-density instruction set function declaration.",
          .insert_text =
              ".syntax unified\n"
              ".thumb\n"
              ".thumb_func\n"
              ".global ${1:thumb_func}\n"
              "${1:thumb_func}:\n"
              "    push {r4-r7, lr}\n"
              "    $0\n"
              "    pop {r4-r7, pc}\n",
          .filter_text = "arm32:thumb2"},

      // --- Common Sections & Directives ---
      Protocol::CompletionItem{
          .label = "section .data",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Directive) Initialized Data Section",
          .documentation = "Declares an initialized data section.",
          .insert_text = "section .data\n    ${1:var_name} ${2:db} ${3:\"string\"}, 0\n$0",
          .filter_text = "section .data"},
      Protocol::CompletionItem{
          .label = "section .bss",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Directive) Uninitialized Data Section",
          .documentation = "Declares a block started by symbol (BSS) memory reserve section.",
          .insert_text = "section .bss\n    ${1:buffer} resb ${2:4096}\n$0",
          .filter_text = "section .bss"},
      Protocol::CompletionItem{
          .label = "section .text",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Directive) Code Section",
          .documentation = "Declares a code/text executable section.",
          .insert_text = "section .text\n    global ${1:entry_point}\n${1:entry_point}:\n    $0",
          .filter_text = "section .text"},
      Protocol::CompletionItem{
          .label = "%macro",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Macro) Preprocessor Macro Definition",
          .documentation = "Defines a multi-line preprocessor macro in NASM.",
          .insert_text = "%macro ${1:name} ${2:1}\n    $0\n%endmacro",
          .filter_text = "%macro"},
      Protocol::CompletionItem{
          .label = "struc",
          .kind = Protocol::CompletionItemKind::Snippet,
          .detail = "(Structure) Data Structure Declaration",
          .documentation = "Defines a memory structure layout.",
          .insert_text = "struc ${1:StructName}\n    .${2:field1}: resd 1\n    .${3:field2}: resq 1\nendstruc\n$0",
          .filter_text = "struc"}};
}

} // namespace

std::vector<Protocol::CompletionItem>
LanguageServerManager::get_templates_for_filename(std::string_view filename) {
  const std::filesystem::path p(filename);
  const std::string ext = p.extension().string();
  const std::string fname = p.filename().string();

  if (fname == "CMakeLists.txt" || fname == "cmakelists.txt" ||
      ext == ".cmake") {
    return CMake::CMakeLanguageDatabase::instance().get_all_completions();
  }
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
      ext == ".hpp" || ext == ".hxx" || ext == ".c" || ext == ".inl") {
    return get_cpp_templates();
  }
  if (ext == ".rs") {
    return get_rust_templates();
  }
  if (ext == ".go") {
    return get_go_templates();
  }
  if (ext == ".py" || ext == ".pyw") {
    return get_python_templates();
  }
  if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx" ||
      ext == ".mjs" || ext == ".cjs") {
    return get_typescript_templates(ext == ".tsx" || ext == ".jsx");
  }
  if (ext == ".html" || ext == ".htm" || ext == ".xhtml") {
    return get_html_templates();
  }
  if (ext == ".asm" || ext == ".s" || ext == ".S" || ext == ".nasm" ||
      ext == ".inc" || ext == ".a51") {
    return get_asm_templates();
  }
  return {};
}

namespace {
static std::mutex& get_header_cache_mutex() {
  static auto* m = new std::mutex();
  return *m;
}
static std::unordered_map<std::string, std::vector<Protocol::CompletionItem>>
    s_header_cache;
} // namespace

void LanguageServerManager::clear_header_cache() noexcept {
  std::lock_guard<std::mutex> lock(get_header_cache_mutex());
  s_header_cache.clear();
}

std::vector<Protocol::CompletionItem>
LanguageServerManager::get_header_completions(
    std::string_view line_prefix, const std::filesystem::path &workspace_root) {
  // Determine if it is <system> or "quoted" include
  bool is_system = false;
  std::string_view path_after_delim;

  const auto lt_pos = line_prefix.rfind('<');
  const auto qt_pos = line_prefix.rfind('"');

  if (lt_pos != std::string_view::npos &&
      (qt_pos == std::string_view::npos || lt_pos > qt_pos)) {
    is_system = true;
    path_after_delim = line_prefix.substr(lt_pos + 1);
  } else if (qt_pos != std::string_view::npos) {
    is_system = false;
    path_after_delim = line_prefix.substr(qt_pos + 1);
  } else {
    return {};
  }

  // Extract subdirectory prefix if user typed e.g. "X11/" or "Platform/X11/"
  std::string sub_dir;
  const auto last_slash = path_after_delim.rfind('/');
  if (last_slash != std::string_view::npos) {
    sub_dir = std::string(path_after_delim.substr(0, last_slash + 1));
  }

  const std::string cache_key =
      (is_system ? "sys:" : "quote:") + sub_dir + "|" + workspace_root.string();
  {
    std::lock_guard<std::mutex> lock(get_header_cache_mutex());
    auto it = s_header_cache.find(cache_key);
    if (it != s_header_cache.end()) {
      return it->second;
    }
  }

  std::vector<Protocol::CompletionItem> items;
  std::unordered_set<std::string> seen;

  const auto &toolchain =
      Toolchain::ToolchainDetector::instance().get_active_toolchain();

  auto add_entry = [&](const std::filesystem::directory_entry &entry) {
    std::error_code ec;
    const auto name = entry.path().filename().string();
    if (name.empty() || name.front() == '.')
      return;

    const auto status = std::filesystem::status(entry.path(), ec);
    if (std::filesystem::is_regular_file(status)) {
      const auto ext = entry.path().extension().string();
      if (ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".inl" ||
          ext.empty()) {
        if (seen.insert(name).second) {
          Protocol::CompletionItem it{};
          it.label = name;
          it.kind = Protocol::CompletionItemKind::File;
          it.detail = is_system ? "System Header" : "Project Header";
          // Close with > or " automatically
          it.insert_text = name + (is_system ? ">" : "\"");
          it.filter_text = name;
          items.push_back(std::move(it));
        }
      }
    } else if (std::filesystem::is_directory(status) && sub_dir.empty()) {
      // Only show directory candidates at top-level; once inside a folder, show
      // only header files
      const std::string dir_label = name + "/";
      if (seen.insert(dir_label).second) {
        Protocol::CompletionItem it{};
        it.label = dir_label;
        it.kind = Protocol::CompletionItemKind::Folder;
        it.detail = "Directory";
        it.insert_text = dir_label;
        it.filter_text = name;
        items.push_back(std::move(it));
      }
    }
  };

  // 1. If scanning a specific sub_dir (e.g. "X11/" or "Platform/X11/"):
  if (!sub_dir.empty()) {
    bool found_dir = false;

    // Scan toolchain system include paths + sub_dir
    for (const auto &sys_inc : toolchain.system_include_paths) {
      std::error_code ec;
      const auto target = sys_inc / sub_dir;
      if (std::filesystem::exists(target, ec) &&
          std::filesystem::is_directory(target, ec)) {
        for (const auto &entry : std::filesystem::directory_iterator(
                 target,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec)) {
          add_entry(entry);
        }
        found_dir = true;
      }
    }

    // Also scan workspace roots + sub_dir
    if (!workspace_root.empty()) {
      const std::filesystem::path roots[] = {
          workspace_root / sub_dir,
          workspace_root / "Source" / sub_dir,
          workspace_root / "Include" / sub_dir,
          workspace_root / "include" / sub_dir,
      };
      for (const auto &target : roots) {
        std::error_code ec;
        if (std::filesystem::exists(target, ec) &&
            std::filesystem::is_directory(target, ec)) {
          for (const auto &entry : std::filesystem::directory_iterator(
                   target,
                   std::filesystem::directory_options::skip_permission_denied,
                   ec)) {
            add_entry(entry);
          }
          found_dir = true;
        }
      }
    }

    // If no matching sub_dir was found anywhere, return empty
    if (!found_dir) {
      return items;
    }
  } else {
    // Top-level include scanning
    // A. System include top-level
    for (const auto &sys_inc : toolchain.system_include_paths) {
      std::error_code ec;
      if (std::filesystem::exists(sys_inc, ec) &&
          std::filesystem::is_directory(sys_inc, ec)) {
        for (const auto &entry : std::filesystem::directory_iterator(
                 sys_inc,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec)) {
          add_entry(entry);
        }
      }
    }
    // B. Workspace include scanning
    if (!workspace_root.empty()) {
      const std::filesystem::path roots[] = {
          workspace_root / "Source",
          workspace_root / "include",
          workspace_root / "Include",
          workspace_root,
      };
      for (const auto &r : roots) {
        std::error_code ec;
        if (std::filesystem::exists(r, ec) &&
            std::filesystem::is_directory(r, ec)) {
          for (const auto &entry : std::filesystem::directory_iterator(
                   r,
                   std::filesystem::directory_options::skip_permission_denied,
                   ec)) {
            add_entry(entry);
          }
        }
      }
    }
  }

  // Sort items: header files first, then subdirectories alphabetically
  std::sort(
      items.begin(), items.end(),
      [](const Protocol::CompletionItem &a, const Protocol::CompletionItem &b) {
        if (a.kind != b.kind) {
          return a.kind == Protocol::CompletionItemKind::File;
        }
        return a.label < b.label;
      });

  return items;
}

LanguageServerManager &LanguageServerManager::instance() noexcept {
  static auto* manager = new LanguageServerManager();
  return *manager;
}

LanguageServerManager::LanguageServerManager() {}

LanguageServerManager::~LanguageServerManager() { shutdown_all(); }

void LanguageServerManager::set_workspace_root(
    std::filesystem::path root_path) {
  if (m_workspace_root != root_path) {
    m_workspace_root = std::move(root_path);
    shutdown_all();
  }
}

Client::ILanguageClient *
LanguageServerManager::get_or_start_client_for_file(std::string_view filename) {
  if (filename.empty()) {
    return nullptr;
  }
  std::string fname_str(filename);
  const std::filesystem::path p(fname_str);
  std::string ext = p.extension().string();
  for (char &c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::string base_name = p.filename().string();
  for (char &c : base_name)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  const bool is_cmake = (base_name == "cmakelists.txt" || ext == ".cmake");

  // Plain text, notes, log files, and markdown must NEVER start or trigger LSP
  if (!is_cmake &&
      (ext == ".txt" || ext == ".log" || ext == ".note" || ext == ".notes" ||
       ext == ".md" || ext == ".markdown" || ext == ".doc" || ext == ".rtf")) {
    return nullptr;
  }

  // If untitled without an explicit code extension, do not start LSP
  if (base_name.starts_with("untitled") || base_name.empty()) {
    if (ext.empty() ||
        (ext != ".cpp" && ext != ".c" && ext != ".h" && ext != ".hpp" &&
         ext != ".cc" && ext != ".cxx" && ext != ".rs" && ext != ".py" &&
         ext != ".go" && ext != ".js" && ext != ".ts" && ext != ".jsx" &&
         ext != ".tsx" && ext != ".mjs" && ext != ".cjs" && ext != ".mts" &&
         ext != ".cts" && ext != ".cmake" && ext != ".html" && ext != ".htm" &&
         ext != ".xhtml" && ext != ".css" && ext != ".json" && ext != ".asm" &&
         ext != ".s" && ext != ".S" && ext != ".nasm" && ext != ".inc")) {
      return nullptr;
    }
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
  if (m_unavailable_languages.contains(profile->language_id)) {
    return nullptr;
  }

  // Locate the language server executable (e.g. clangd.exe, rust-analyzer.exe,
  // etc.)
  const std::filesystem::path exe_path =
      Registry::ServerRegistry::instance().find_executable_in_system(
          profile->executable_name);
  if (exe_path.empty()) {
    lsp_debug_log("[zde-lsp] EXE NOT FOUND for " +
                  std::string(profile->executable_name));
    m_unavailable_languages.insert(profile->language_id);
    return nullptr;
  }
  lsp_debug_log("[zde-lsp] exe=" + exe_path.generic_string());

  std::vector<std::string> args = profile->default_args;
  std::vector<std::string> fallback_flags;
  if (profile->language_id == "cpp") {
    std::error_code ec;
    std::vector<std::filesystem::path> search_roots;
    if (!m_workspace_root.empty()) {
      search_roots.push_back(m_workspace_root);
    }
    if (!fname_str.empty() && fname_str != "untitled.cpp") {
      std::filesystem::path file_p(fname_str);
      if (file_p.is_relative()) {
        file_p = std::filesystem::absolute(file_p, ec);
      }
      std::filesystem::path parent = file_p.parent_path();
      for (int i = 0; i < 8 && !parent.empty(); ++i) {
        search_roots.push_back(parent);
        const auto next_parent = parent.parent_path();
        if (next_parent == parent)
          break;
        parent = next_parent;
      }
    }
    std::filesystem::path cur = std::filesystem::current_path(ec);
    for (int i = 0; i < 8 && !cur.empty(); ++i) {
      search_roots.push_back(cur);
      const auto parent = cur.parent_path();
      if (parent == cur)
        break;
      cur = parent;
    }

    std::vector<std::filesystem::path> unique_search_roots;
    for (const auto &r : search_roots) {
      if (r.empty())
        continue;
      bool exists_already = false;
      for (const auto &u : unique_search_roots) {
        if (std::filesystem::equivalent(r, u, ec)) {
          exists_already = true;
          break;
        }
      }
      if (!exists_already) {
        unique_search_roots.push_back(r);
      }
    }

    std::filesystem::path best_compile_dir;
    std::uintmax_t best_file_size = 0;
    std::filesystem::file_time_type best_mtime{};

    auto evaluate_candidate = [&](const std::filesystem::path &cand_path) {
      std::error_code ec_cand;
      if (std::filesystem::exists(cand_path, ec_cand) &&
          std::filesystem::is_regular_file(cand_path, ec_cand)) {
        const auto sz = std::filesystem::file_size(cand_path, ec_cand);
        if (sz > 0) {
          const auto mtime =
              std::filesystem::last_write_time(cand_path, ec_cand);
          if (best_compile_dir.empty() || mtime > best_mtime ||
              sz > best_file_size) {
            best_file_size = sz;
            best_mtime = mtime;
            best_compile_dir = cand_path.parent_path();
          }
        }
      }
    };

    auto should_skip_dir = [](std::string_view dirname) {
      return dirname == ".git" || dirname == ".svn" || dirname == ".hg" ||
             dirname == ".vscode" || dirname == ".vs" || dirname == ".gemini" ||
             dirname == ".antigravity" || dirname == "node_modules" ||
             dirname == ".cache" || dirname == "CMakeFiles" ||
             dirname == "Testing" || dirname == ".idea";
    };

    for (const auto &root : unique_search_roots) {
      // 1. Immediate root candidates (standard CMake, Meson, Visual Studio,
      // CLion output directories)
      evaluate_candidate(root / "compile_commands.json");
      evaluate_candidate(root / "build" / "compile_commands.json");
      evaluate_candidate(root / "build" / "linux-debug" /
                         "compile_commands.json");
      evaluate_candidate(root / "build" / "linux-release" /
                         "compile_commands.json");
      evaluate_candidate(root / "build" / "x86_64-debug" /
                         "compile_commands.json");
      evaluate_candidate(root / "build" / "x86_64-release" /
                         "compile_commands.json");
      evaluate_candidate(root / "out" / "compile_commands.json");
      evaluate_candidate(root / "out" / "build" / "compile_commands.json");
      evaluate_candidate(root / "cmake-build-debug" / "compile_commands.json");
      evaluate_candidate(root / "cmake-build-release" /
                         "compile_commands.json");
      evaluate_candidate(root / ".build" / "compile_commands.json");
      evaluate_candidate(root / "builddir" / "compile_commands.json");

      // 2. Scan immediate direct child directories up to depth 1 (e.g. build
      // subfolders)
      if (best_compile_dir.empty()) {
        std::error_code ec_iter;
        for (const auto &entry : std::filesystem::directory_iterator(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec_iter)) {
          if (entry.is_directory(ec_iter)) {
            const std::string name = entry.path().filename().string();
            if (!should_skip_dir(name)) {
              evaluate_candidate(entry.path() / "compile_commands.json");
              evaluate_candidate(entry.path() / "build" /
                                 "compile_commands.json");
              if (!best_compile_dir.empty())
                break;
            }
          }
        }
      }
    }

    std::erase_if(args, [](const std::string &a) {
      return a.starts_with("--compile-commands-dir") ||
             a.starts_with("--query-driver");
    });
    if (!best_compile_dir.empty()) {
      args.push_back("--compile-commands-dir=" +
                     best_compile_dir.generic_string());
    }

    // Auto-detect and configure toolchain fallback flags
    const auto &toolchain =
        Toolchain::ToolchainDetector::instance().get_active_toolchain();

    // Standard C++ specification
    fallback_flags.push_back("-std=c++20");

#if defined(_WIN32)
    // Target configuration based on detected toolchain (GCC / MSVC)
    if (toolchain.kind == Toolchain::ToolchainKind::MinGW_GCC) {
      fallback_flags.push_back("--target=x86_64-w64-windows-gnu");
    } else if (toolchain.kind == Toolchain::ToolchainKind::MSVC) {
      fallback_flags.push_back("--target=x86_64-pc-windows-msvc");
      fallback_flags.push_back("-fms-extensions");
      fallback_flags.push_back("-fms-compatibility");
    }
    fallback_flags.push_back("-DWIN32");
    fallback_flags.push_back("-D_WINDOWS");
    fallback_flags.push_back("-DUNICODE");
    fallback_flags.push_back("-D_UNICODE");
    fallback_flags.push_back("-DNOMINMAX");
    fallback_flags.push_back("-DWIN32_LEAN_AND_MEAN");
#else
    fallback_flags.push_back("-D__linux__");
    fallback_flags.push_back("-D_GNU_SOURCE");
    fallback_flags.push_back("-D_POSIX_C_SOURCE=200809L");
#endif

    // Fallback project include directories from search roots
    for (const auto &root : unique_search_roots) {
      const std::filesystem::path sub_candidates[] = {
          root / "Source",   root / "src",
          root / "include",  root / "Include",
          root / "Drivers",  root / "ThirdParty",
          root / "Utility",  root / "UI",
          root / "Platform", root,
      };
      for (const auto &sub : sub_candidates) {
        if (std::filesystem::exists(sub, ec) &&
            std::filesystem::is_directory(sub, ec)) {
          fallback_flags.push_back("-I" + sub.generic_string());
        }
      }
    }

    // Auto-inject system include directories discovered by ToolchainDetector
    for (const auto &inc : toolchain.system_include_paths) {
      if (!inc.empty() && std::filesystem::exists(inc, ec)) {
        fallback_flags.push_back("-isystem" + inc.generic_string());
      }
    }

    // Direct query-driver pointing to the detected compiler and standard paths
    std::string comp_pattern;
    if (!toolchain.compiler_path.empty()) {
      comp_pattern = toolchain.compiler_path.generic_string() + "*,";
    }
#if defined(_WIN32)
    args.push_back("--query-driver=" + comp_pattern +
                   "*,*/*,**/*,C:/*,C:/**,D:/*,D:/**,E:/*,E:/**");
#else
    args.push_back("--query-driver=" + comp_pattern +
                   "/usr/bin/*,/usr/local/bin/*,/opt/**,*,*/*,**/*");
#endif

    lsp_debug_log(best_compile_dir.empty()
                      ? "[zde-lsp] no compile_commands.json found, using "
                        "fallback includes"
                      : "[zde-lsp] compile-dir=" +
                            best_compile_dir.generic_string());
  }

  auto transport = std::make_unique<Transport::StdioProcessTransport>(
      exe_path, std::move(args), m_workspace_root);

  auto client = std::make_unique<Client::LanguageClient>(
      profile->language_id, std::move(transport), m_workspace_root);

  if (profile->language_id == "cpp") {
    nlohmann::json init_opts = nlohmann::json::object();
    init_opts["fallbackFlags"] = fallback_flags;
    client->set_initialization_options(std::move(init_opts));
  } else if (profile->language_id == "html" ||
             profile->executable_name == "emmet-ls") {
    nlohmann::json init_opts = nlohmann::json::object();
    init_opts["showExpandedAbbreviation"] = "always";
    init_opts["showAbbreviationSuggestions"] = true;
    init_opts["showSuggestionsAsSnippets"] = true;
    client->set_initialization_options(std::move(init_opts));
  }

  client->set_diagnostics_handler(
      [this](const std::string &uri,
             const std::vector<Protocol::Diagnostic> &diags) {
        std::function<void(const std::string&, const std::vector<Protocol::Diagnostic>&)> cb;
        {
          std::lock_guard<std::mutex> lock(m_clients_mutex);
          m_document_diagnostics[uri] = diags;
          cb = m_diagnostics_callback;
        }
        if (cb) {
          try {
            cb(uri, diags);
          } catch (const std::exception &ex) {
            std::cerr << "[LanguageServerManager] Diagnostics callback exception: " << ex.what() << '\n';
          } catch (...) {
            std::cerr << "[LanguageServerManager] Diagnostics callback unknown exception caught.\n";
          }
        }
      });

  if (client->start()) {
    lsp_debug_log("[zde-lsp] client started for " + profile->language_id);
    auto *ptr = client.get();
    m_clients[profile->language_id] = std::move(client);
    return ptr;
  }

  lsp_debug_log("[zde-lsp] client START FAILED for " + profile->language_id);
  m_unavailable_languages.insert(profile->language_id);
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
  if (ext == ".go" || ext == ".mod" || ext == ".work")
    return "go";
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
    if (!client->is_document_open(uri)) {
      on_document_opened(uri, filename, version, content);
      return;
    }
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
    std::function<void(std::vector<Protocol::CompletionItem>)> callback,
    std::optional<char> trigger_character) {
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
          },
          trigger_character);
      return;
    }

    if (callback) {
      callback(std::move(cmake_items));
    }
    return;
  }

  // Check if cursor is immediately following a scoped or member access operator
  // (::, ->, .)
  const std::string_view line_before_cursor = line_text.substr(
      0, std::min(static_cast<std::size_t>(pos.character), line_text.size()));
  std::size_t p_op = line_before_cursor.size();
  while (p_op > 0 && (std::isalnum(static_cast<unsigned char>(
                          line_before_cursor[p_op - 1])) ||
                      line_before_cursor[p_op - 1] == '_' ||
                      line_before_cursor[p_op - 1] == '~')) {
    --p_op;
  }
  const std::string_view prefix_op = line_before_cursor.substr(0, p_op);
  const bool is_scoped_context = prefix_op.ends_with("::") ||
                                 prefix_op.ends_with("->") ||
                                 prefix_op.ends_with('.');

  // Retrieve base templates for the language only if NOT in scoped context
  auto templates = is_scoped_context ? std::vector<Protocol::CompletionItem>{}
                                     : get_templates_for_filename(filename);

  // If in an include context (#include <... or #include "...), fetch header
  // completions
  std::vector<Protocol::CompletionItem> header_items;
  if (line_text.find("#include") != std::string_view::npos ||
      line_text.find("#import") != std::string_view::npos ||
      line_text.find('#') != std::string_view::npos) {
    header_items = get_header_completions(line_text, m_workspace_root);
  }

  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr &&
      (client->is_active() ||
       client->get_state() == Client::ClientState::Initializing)) {
    client->request_completion(
        uri, pos,
        [callback = std::move(callback), templates = std::move(templates),
         header_items = std::move(header_items)](
            std::vector<Protocol::CompletionItem> lsp_items) mutable {
          std::unordered_set<std::string> seen_labels;
          std::vector<Protocol::CompletionItem> combined;
          combined.reserve(lsp_items.size() + templates.size() +
                           header_items.size());

          // 1. LSP items first
          for (auto &it : lsp_items) {
            if (seen_labels.insert(it.label).second) {
              combined.push_back(std::move(it));
            }
          }

          // 2. Header completions (standard library + project headers)
          for (auto &hdr : header_items) {
            if (seen_labels.insert(hdr.label).second) {
              combined.push_back(std::move(hdr));
            }
          }

          // 3. Language templates & snippets
          for (auto &tpl : templates) {
            if (seen_labels.insert(tpl.label).second) {
              combined.push_back(std::move(tpl));
            }
          }

          if (callback) {
            callback(std::move(combined));
          }
        },
        trigger_character);
  } else if (callback) {
    std::unordered_set<std::string> seen_labels;
    std::vector<Protocol::CompletionItem> fallback_items;
    fallback_items.reserve(header_items.size() + templates.size());
    for (auto &hdr : header_items) {
      if (seen_labels.insert(hdr.label).second) {
        fallback_items.push_back(std::move(hdr));
      }
    }
    for (auto &tpl : templates) {
      if (seen_labels.insert(tpl.label).second) {
        fallback_items.push_back(std::move(tpl));
      }
    }
    callback(std::move(fallback_items));
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
    std::function<void(std::vector<Protocol::Location>)> callback,
    std::string_view line_text) {
  auto *client = get_or_start_client_for_file(filename);
  if (client != nullptr) {
    client->request_definition(
        uri, pos,
        [this, uri, filename = std::string(filename), pos,
         line_text = std::string(line_text),
         cb = std::move(callback)](std::vector<Protocol::Location> locations) {
          if (!locations.empty()) {
            if (cb) {
              cb(std::move(locations));
            }
            return;
          }

          // Seamless fallback to standard library / workspace symbol resolver
          auto fallback_locs =
              Definition::SymbolDefinitionResolver::instance().resolve_definition(
                  uri, filename, pos, line_text, m_workspace_root);
          if (cb) {
            cb(std::move(fallback_locs));
          }
        });
  } else {
    auto fallback_locs =
        Definition::SymbolDefinitionResolver::instance().resolve_definition(
            uri, filename, pos, line_text, m_workspace_root);
    if (callback) {
      callback(std::move(fallback_locs));
    }
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
      uri, [this, client, uri, callback = std::move(callback)](
               std::optional<Protocol::SemanticTokens> tokens) {
        if (!tokens.has_value() || tokens->data.empty()) {
          if (callback)
            callback({});
          return;
        }

        const auto legend = client->get_semantic_token_legend();
        auto spans = Syntax::SemanticTokensManager::decode_lsp_tokens(
            tokens->data, legend);
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
  std::lock_guard<std::mutex> lock(m_clients_mutex);
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
  m_diagnostics_callback = nullptr;
  for (auto &[id, client] : m_clients) {
    if (client) {
      client->shutdown();
      client->exit();
    }
  }
  m_clients.clear();
  m_unavailable_languages.clear();
  m_document_diagnostics.clear();
  Registry::ServerRegistry::instance().clear_cache();
}

} // namespace Zenvra::Language
