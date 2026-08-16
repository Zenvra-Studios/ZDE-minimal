#include "Language/Syntax/GrammarRegistry.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

namespace Zenvra::Language::Syntax
{

GrammarRegistry& GrammarRegistry::instance() noexcept
{
    static GrammarRegistry registry;
    return registry;
}

GrammarRegistry::GrammarRegistry()
{
    initialize_default_grammars();
    load_grammars_from_directory("Assets/Grammars");
    load_grammars_from_directory("../Assets/Grammars");
    load_grammars_from_directory("../../Assets/Grammars");
}

void GrammarRegistry::register_grammar(GrammarRule rule)
{
    auto ptr = std::make_shared<GrammarRule>(std::move(rule));
    m_grammars_by_name[ptr->name] = ptr;
    for (const auto& ext : ptr->extensions)
    {
        m_grammars_by_extension[ext] = ptr;
    }
}

bool GrammarRegistry::load_grammar_from_json(std::string_view json_content)
{
    try
    {
        const auto parsed = nlohmann::json::parse(json_content);
        GrammarRule rule;

        if (parsed.contains("name") && parsed["name"].is_string())
        {
            rule.name = parsed["name"].get<std::string>();
        }
        if (parsed.contains("extensions") && parsed["extensions"].is_array())
        {
            for (const auto& item : parsed["extensions"])
            {
                if (item.is_string()) rule.extensions.push_back(item.get<std::string>());
            }
        }
        if (parsed.contains("line_comment") && parsed["line_comment"].is_string())
        {
            rule.line_comment = parsed["line_comment"].get<std::string>();
        }
        if (parsed.contains("block_comment_start") && parsed["block_comment_start"].is_string())
        {
            rule.block_comment_start = parsed["block_comment_start"].get<std::string>();
        }
        if (parsed.contains("block_comment_end") && parsed["block_comment_end"].is_string())
        {
            rule.block_comment_end = parsed["block_comment_end"].get<std::string>();
        }
        if (parsed.contains("supports_preprocessor") && parsed["supports_preprocessor"].is_boolean())
        {
            rule.supports_preprocessor = parsed["supports_preprocessor"].get<bool>();
        }
        if (parsed.contains("keywords") && parsed["keywords"].is_array())
        {
            for (const auto& item : parsed["keywords"])
            {
                if (item.is_string()) rule.keywords.insert(item.get<std::string>());
            }
        }
        if (parsed.contains("types") && parsed["types"].is_array())
        {
            for (const auto& item : parsed["types"])
            {
                if (item.is_string()) rule.types.insert(item.get<std::string>());
            }
        }
        if (parsed.contains("operators") && parsed["operators"].is_array())
        {
            for (const auto& item : parsed["operators"])
            {
                if (item.is_string()) rule.operators.push_back(item.get<std::string>());
            }
        }
        if (parsed.contains("string_delimiters") && parsed["string_delimiters"].is_array())
        {
            rule.string_delimiters.clear();
            for (const auto& item : parsed["string_delimiters"])
            {
                if (item.is_string()) rule.string_delimiters.push_back(item.get<std::string>());
            }
        }

        register_grammar(std::move(rule));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool GrammarRegistry::load_grammar_from_file(const std::filesystem::path& file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return load_grammar_from_json(content);
}

std::size_t GrammarRegistry::load_grammars_from_directory(const std::filesystem::path& dir_path)
{
    if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path))
    {
        return 0;
    }

    std::size_t loaded_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            if (load_grammar_from_file(entry.path()))
            {
                ++loaded_count;
            }
        }
    }
    return loaded_count;
}

const GrammarRule* GrammarRegistry::get_grammar_for_extension(std::string_view extension) const noexcept
{
    const std::string ext_str(extension);
    const auto it = m_grammars_by_extension.find(ext_str);
    if (it != m_grammars_by_extension.end())
    {
        return it->second.get();
    }
    return nullptr;
}

const GrammarRule* GrammarRegistry::get_grammar_for_filename(std::string_view filename) const noexcept
{
    const std::filesystem::path path(filename);
    const std::string fname_str = path.filename().string();
    std::string lower_fname = fname_str;
    std::transform(lower_fname.begin(), lower_fname.end(), lower_fname.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    // Check exact whole filename first (e.g. "meson.build", "meson_options.txt", "cmakelists.txt")
    if (const auto it = m_grammars_by_extension.find(lower_fname); it != m_grammars_by_extension.end())
    {
        return it->second.get();
    }
    if (const auto it = m_grammars_by_extension.find(fname_str); it != m_grammars_by_extension.end())
    {
        return it->second.get();
    }

    const std::string ext = path.extension().string();
    std::string lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (const auto* rule = get_grammar_for_extension(lower_ext))
    {
        return rule;
    }
    return get_grammar_for_extension(ext);
}

void GrammarRegistry::initialize_default_grammars()
{
    // Built-in for Java & Frameworks (Spring Boot, Hibernate, Lombok, JUnit, Jakarta)
    {
        GrammarRule java_rule;
        java_rule.name = "Java";
        java_rule.extensions = {".java", ".jav", ".class", ".jar"};
        java_rule.line_comment = "//";
        java_rule.block_comment_start = "/*";
        java_rule.block_comment_end = "*/";
        java_rule.keywords = {
            "abstract", "assert", "break", "case", "catch",
            "class", "const", "continue", "default", "do", "else", "enum",
            "extends", "final", "finally", "for", "goto", "if", "implements",
            "import", "instanceof", "interface", "native", "new",
            "package", "private", "protected", "public", "return", "static",
            "strictfp", "super", "switch", "synchronized", "this", "throw", "throws",
            "transient", "try", "volatile", "while", "yield", "record",
            "sealed", "non-sealed", "permits", "var", "true", "false", "null",
            // Spring Boot & Enterprise Annotations
            "@SpringBootApplication", "@RestController", "@Controller", "@Service",
            "@Repository", "@Component", "@Configuration", "@Bean", "@Autowired",
            "@Inject", "@Qualifier", "@Value", "@Profile", "@ConditionalOnProperty",
            "@GetMapping", "@PostMapping", "@PutMapping", "@DeleteMapping", "@PatchMapping",
            "@RequestMapping", "@PathVariable", "@RequestParam", "@RequestBody",
            "@ResponseBody", "@ResponseStatus", "@ExceptionHandler", "@ControllerAdvice",
            "@CrossOrigin", "@Valid", "@Validated", "@Transactional", "@Scheduled", "@Async",
            // JPA / Hibernate Annotations
            "@Entity", "@Table", "@Id", "@GeneratedValue", "@Column", "@OneToOne",
            "@OneToMany", "@ManyToOne", "@ManyToMany", "@JoinColumn", "@JoinTable",
            "@Transient", "@Enumerated", "@Temporal", "@Lob", "@Embedded", "@Embeddable",
            // Lombok Annotations
            "@Getter", "@Setter", "@Data", "@Builder", "@NoArgsConstructor",
            "@AllArgsConstructor", "@RequiredArgsConstructor", "@ToString",
            "@EqualsAndHashCode", "@Slf4j", "@Log4j2", "@Value", "@SneakyThrows",
            // JUnit & Testing Annotations
            "@Test", "@BeforeEach", "@AfterEach", "@BeforeAll", "@AfterAll",
            "@DisplayName", "@Disabled", "@ParameterizedTest", "@ValueSource",
            "@Mock", "@InjectMocks", "@Spy", "@ExtendWith",
            // Standard Annotations
            "@Override", "@Deprecated", "@SuppressWarnings", "@FunctionalInterface",
            "@Nullable", "@NonNull", "@NotNull"
        };
        java_rule.types = {
            "boolean", "byte", "char", "short", "int", "long", "float", "double", "void",
            "String", "Object", "Integer", "Long", "Double", "Float", "Boolean",
            "Character", "Byte", "Short", "Void", "CharSequence", "StringBuilder", "StringBuffer",
            "List", "ArrayList", "LinkedList", "Map", "HashMap", "TreeMap", "LinkedHashMap",
            "Set", "HashSet", "TreeSet", "LinkedHashSet", "Queue", "Deque", "ArrayDeque",
            "Optional", "Stream", "CompletableFuture", "Future", "Callable", "Runnable",
            "Thread", "Exception", "RuntimeException", "Throwable", "Error", "BigDecimal",
            "BigInteger", "LocalDate", "LocalDateTime", "LocalTime", "ZonedDateTime", "Instant",
            "Duration", "Period", "UUID", "File", "Path", "Paths", "InputStream", "OutputStream",
            // Framework Types
            "ResponseEntity", "HttpHeaders", "HttpStatus", "ModelAndView", "Model",
            "HttpServletRequest", "HttpServletResponse", "HttpSession", "ApplicationContext",
            "EntityManager", "Session", "Query", "TypedQuery", "Page", "Pageable", "Sort",
            "Mono", "Flux", "Logger", "LoggerFactory"
        };
        register_grammar(std::move(java_rule));
    }

    // Built-in for Meson Build System & Modules
    {
        GrammarRule meson_rule;
        meson_rule.name = "Meson";
        meson_rule.extensions = {".build", ".meson", "meson.build", "meson_options.txt"};
        meson_rule.line_comment = "#";
        meson_rule.keywords = {
            "project", "executable", "library", "shared_library", "static_library",
            "both_libraries", "shared_module", "dependency", "find_program", "subproject",
            "include_directories", "files", "declare_dependency", "custom_target",
            "run_target", "generator", "test", "benchmark", "install_headers",
            "install_man", "install_data", "install_subdir", "subdir", "configure_file",
            "set_variable", "get_variable", "is_variable", "import", "message",
            "warning", "error", "summary", "assert", "if", "elif", "else", "endif",
            "foreach", "endforeach", "continue", "break", "and", "or", "not", "in"
        };
        meson_rule.types = {
            "pkgconfig", "gnome", "qt5", "qt6", "python", "cmake", "cuda", "dlang",
            "hotdoc", "i18n", "java", "keyval", "simd", "sourceset", "windows",
            "compiler", "build_machine", "host_machine", "target_machine", "meson",
            "default_options", "version", "license", "meson_version", "language",
            "languages", "sources", "dependencies", "link_with", "link_whole",
            "include_directories", "c_args", "cpp_args", "link_args", "install",
            "install_dir", "override_options", "true", "false"
        };
        register_grammar(std::move(meson_rule));
    }

    // Built-in for JavaScript / TypeScript & Modern Frameworks (React, Vue, Angular, Nest, Node, Bun)
    {
        GrammarRule js_rule;
        js_rule.name = "JavaScript/TypeScript";
        js_rule.extensions = {".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx", ".mts", ".cts", ".vue", ".svelte"};
        js_rule.line_comment = "//";
        js_rule.block_comment_start = "/*";
        js_rule.block_comment_end = "*/";
        js_rule.string_delimiters = {"\"", "'", "`"};
        js_rule.keywords = {
            "abstract", "accessor", "arguments", "as", "asserts", "async", "await", "break", "case", "catch",
            "class", "const", "continue", "debugger", "declare", "default", "delete", "do", "else",
            "enum", "export", "extends", "false", "finally", "for", "from", "function",
            "get", "if", "implements", "import", "in", "infer", "instanceof", "interface", "is",
            "keyof", "let", "module", "namespace", "never", "new", "null", "of", "override", "package",
            "private", "protected", "public", "readonly", "require", "return", "satisfies", "set",
            "static", "super", "switch", "symbol", "this", "throw", "true", "try", "type",
            "typeof", "undefined", "unique", "unknown", "using", "var", "void", "while", "with", "yield",
            // React & Hooks
            "useState", "useEffect", "useContext", "useReducer", "useCallback", "useMemo",
            "useRef", "useImperativeHandle", "useLayoutEffect", "useDebugValue", "useId",
            "useTransition", "useDeferredValue", "createContext", "forwardRef", "memo", "lazy", "Suspense",
            // Vue 3 Composition API
            "ref", "reactive", "computed", "watch", "watchEffect", "onMounted", "onUnmounted",
            "onUpdated", "defineComponent", "defineProps", "defineEmits", "defineExpose",
            "definePageMeta", "useRoute", "useRouter", "useHead", "useAsyncData", "useFetch",
            // Angular & NestJS Decorators
            "@Component", "@Directive", "@Pipe", "@Injectable", "@NgModule", "@Input",
            "@Output", "@ViewChild", "@ContentChild", "@HostListener", "@HostBinding",
            "@Controller", "@Get", "@Post", "@Put", "@Delete", "@Patch", "@Param",
            "@Body", "@Query", "@Req", "@Res", "@Next", "@Module", "@Inject",
            // Runtime Globals & Methods
            "console", "process", "global", "globalThis", "window", "document", "fetch",
            "setTimeout", "clearTimeout", "setInterval", "clearInterval"
        };
        js_rule.types = {
            "string", "number", "boolean", "symbol", "bigint", "any", "unknown", "never",
            "void", "null", "undefined", "object", "Array", "Record", "Partial", "Required",
            "Readonly", "Pick", "Omit", "Exclude", "Extract", "NonNullable", "Parameters",
            "ReturnType", "InstanceType", "Awaited", "Capitalize", "Uncapitalize", "Uppercase",
            "Lowercase", "TemplateStringsArray", "ConstructorParameters", "Promise", "Map", "Set", "WeakMap", "WeakSet",
            // Framework Types
            "FC", "ReactNode", "ReactElement", "Component", "Fragment", "NextPage",
            "GetServerSideProps", "GetStaticProps", "Metadata", "AppProps", "Observable",
            "BehaviorSubject", "Subject", "Subscription", "Request", "Response",
            "NextFunction", "Router", "Express", "Buffer", "URL", "URLSearchParams"
        };
        register_grammar(std::move(js_rule));
    }

    // Built-in fallback for C/C++
    {
        GrammarRule cpp_rule;
        cpp_rule.name = "C/C++";
        cpp_rule.extensions = {".cpp", ".c", ".h", ".hpp", ".cc", ".cxx", ".hh", ".hxx", ".inl"};
        cpp_rule.line_comment = "//";
        cpp_rule.block_comment_start = "/*";
        cpp_rule.block_comment_end = "*/";
        cpp_rule.supports_preprocessor = true;
        cpp_rule.keywords = {
            "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
            "break", "case", "catch", "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
            "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
            "default", "delete", "do", "dynamic_cast", "else", "enum",
            "explicit", "export", "extern", "false", "for", "friend", "goto",
            "if", "inline", "mutable", "namespace", "new", "noexcept",
            "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
            "public", "register", "reinterpret_cast", "requires", "return",
            "sizeof", "static", "static_assert", "static_cast", "struct",
            "switch", "template", "this", "thread_local", "throw", "true", "try",
            "typedef", "typeid", "typename", "union", "using", "virtual",
            "volatile", "while", "xor", "xor_eq"
        };
        cpp_rule.types = {
            "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long", "short",
            "signed", "unsigned", "void", "wchar_t",
            "size_t", "int8_t", "int16_t", "int32_t", "int64_t",
            "uint8_t", "uint16_t", "uint32_t", "uint64_t",
            "intptr_t", "uintptr_t", "ptrdiff_t", "string", "string_view",
            "vector", "array", "map", "unordered_map", "set", "unordered_set",
            "unique_ptr", "shared_ptr", "optional", "span", "variant"
        };
        register_grammar(std::move(cpp_rule));
    }

    // Built-in fallback for Rust
    {
        GrammarRule rust_rule;
        rust_rule.name = "Rust";
        rust_rule.extensions = {".rs"};
        rust_rule.line_comment = "//";
        rust_rule.block_comment_start = "/*";
        rust_rule.block_comment_end = "*/";
        rust_rule.keywords = {
            "as", "async", "await", "break", "const", "continue", "crate", "dyn",
            "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
            "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
            "self", "Self", "static", "struct", "super", "trait", "true", "type",
            "unsafe", "use", "where", "while"
        };
        rust_rule.types = {
            "i8", "i16", "i32", "i64", "i128", "isize",
            "u8", "u16", "u32", "u64", "u128", "usize",
            "f32", "f64", "bool", "char", "str", "String", "Option", "Result", "Vec", "Box", "Rc", "Arc"
        };
        register_grammar(std::move(rust_rule));
    }

    // Built-in fallback for Python
    {
        GrammarRule py_rule;
        py_rule.name = "Python";
        py_rule.extensions = {".py", ".pyw", ".pyi"};
        py_rule.line_comment = "#";
        py_rule.keywords = {
            "False", "None", "True", "and", "as", "assert", "async", "await", "break",
            "class", "continue", "def", "del", "elif", "else", "except", "finally",
            "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
            "not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "self"
        };
        py_rule.types = {
            "int", "float", "complex", "list", "tuple", "range", "str", "bytes",
            "bytearray", "memoryview", "set", "frozenset", "dict", "bool", "type", "object"
        };
        register_grammar(std::move(py_rule));
    }

    // Built-in for CMake
    {
        GrammarRule cmake_rule;
        cmake_rule.name = "CMake";
        cmake_rule.extensions = {".cmake", "cmakelists.txt"};
        cmake_rule.line_comment = "#";
        cmake_rule.keywords = {
            "cmake_minimum_required", "project", "add_executable", "add_library",
            "add_subdirectory", "add_custom_command", "add_custom_target", "add_definitions",
            "add_dependencies", "find_package", "find_path", "find_library", "find_program",
            "find_file", "target_link_libraries", "target_include_directories",
            "target_compile_definitions", "target_compile_features", "target_compile_options",
            "target_sources", "set", "unset", "set_target_properties", "get_target_property",
            "set_property", "get_property", "if", "else", "elseif", "endif",
            "foreach", "endforeach", "while", "endwhile", "function", "endfunction",
            "macro", "endmacro", "return", "break", "continue", "option", "message",
            "include", "include_directories", "link_directories", "enable_testing",
            "add_test", "configure_file", "file", "list", "string", "math"
        };
        cmake_rule.types = {
            "PUBLIC", "PRIVATE", "INTERFACE", "REQUIRED", "COMPONENTS", "CONFIG",
            "STATIC", "SHARED", "MODULE", "OBJECT", "ALIAS", "IMPORTED",
            "STATUS", "WARNING", "AUTHOR_WARNING", "FATAL_ERROR", "SEND_ERROR",
            "TRUE", "FALSE", "ON", "OFF", "STREQUAL", "STRLESS", "STRGREATER",
            "VERSION_LESS", "VERSION_GREATER", "VERSION_EQUAL", "EXISTS", "IS_DIRECTORY",
            "DEFINED", "NOT", "AND", "OR", "MATCHES"
        };
        register_grammar(std::move(cmake_rule));
    }

    // Built-in for HTML / HTML5 / XHTML
    {
        GrammarRule html_rule;
        html_rule.name = "HTML";
        html_rule.extensions = {".html", ".htm", ".xhtml"};
        html_rule.line_comment = "";
        html_rule.block_comment_start = "<!--";
        html_rule.block_comment_end = "-->";
        html_rule.string_delimiters = {"\"", "'"};
        html_rule.keywords = {
            "!DOCTYPE", "html", "head", "title", "base", "link", "meta", "style", "script",
            "noscript", "body", "section", "nav", "article", "aside", "h1", "h2", "h3", "h4",
            "h5", "h6", "header", "footer", "address", "main", "p", "hr", "pre", "blockquote",
            "ol", "ul", "menu", "li", "dl", "dt", "dd", "figure", "figcaption", "div", "a",
            "em", "strong", "small", "s", "cite", "q", "dfn", "abbr", "ruby", "rt", "rp",
            "data", "time", "code", "var", "samp", "kbd", "sub", "sup", "i", "b", "u",
            "mark", "bdi", "bdo", "span", "br", "wbr", "ins", "del", "picture", "source",
            "img", "iframe", "embed", "object", "video", "audio", "track", "map", "area",
            "table", "caption", "colgroup", "col", "tbody", "thead", "tfoot", "tr", "td",
            "th", "form", "label", "input", "button", "select", "datalist", "optgroup",
            "option", "textarea", "output", "progress", "meter", "fieldset", "legend",
            "details", "summary", "dialog", "template", "slot", "canvas", "svg"
        };
        html_rule.types = {
            "class", "id", "style", "title", "lang", "dir", "accesskey", "tabindex", "hidden",
            "draggable", "spellcheck", "contenteditable", "role", "aria-label", "aria-hidden",
            "aria-expanded", "aria-checked", "aria-controls", "data-", "href", "src", "alt",
            "width", "height", "target", "rel", "type", "value", "name", "placeholder",
            "disabled", "readonly", "required", "checked", "selected", "multiple", "action",
            "method", "enctype", "autocomplete", "autofocus", "pattern", "min", "max",
            "step", "rows", "cols", "wrap", "for", "charset", "http-equiv", "content",
            "onclick", "onload", "onchange", "onsubmit", "onkeydown", "onkeyup", "onfocus",
            "onblur", "onmouseover", "onmouseout", "onmouseenter", "onmouseleave"
        };
        register_grammar(std::move(html_rule));
    }
}

} // namespace Zenvra::Language::Syntax
