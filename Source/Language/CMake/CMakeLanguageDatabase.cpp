#include "Language/CMake/CMakeLanguageDatabase.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace Zenvra::Language::CMake {

CMakeLanguageDatabase &CMakeLanguageDatabase::instance() noexcept {
  static auto* s_instance = new CMakeLanguageDatabase();
  return *s_instance;
}

CMakeLanguageDatabase::CMakeLanguageDatabase() { initialize_database(); }

void CMakeLanguageDatabase::initialize_database() {
  m_all_completions.clear();

  auto add = [this](std::string label, Protocol::CompletionItemKind kind,
                    std::string detail, std::string documentation,
                    std::string insert_text, std::string sort_text = "") {
    if (sort_text.empty()) {
      char prefix = '3';
      if (kind == Protocol::CompletionItemKind::Function)
        prefix = '1';
      else if (kind == Protocol::CompletionItemKind::Snippet)
        prefix = '2';
      else if (kind == Protocol::CompletionItemKind::Keyword)
        prefix = '3';
      else if (kind == Protocol::CompletionItemKind::Variable)
        prefix = '4';
      else if (kind == Protocol::CompletionItemKind::Property)
        prefix = '5';
      sort_text = std::string(1, prefix) + "_" + label;
    }

    m_all_completions.push_back(
        Protocol::CompletionItem{.label = std::move(label),
                                 .kind = kind,
                                 .detail = std::move(detail),
                                 .documentation = std::move(documentation),
                                 .insert_text = std::move(insert_text),
                                 .sort_text = std::move(sort_text),
                                 .filter_text = ""});
  };

  // =========================================================================
  // 1. CMAKE COMMANDS & BUILT-IN FUNCTIONS (DETAILED OFFICIAL DOCS)
  // =========================================================================
  add("cmake_minimum_required", Protocol::CompletionItemKind::Function,
      "cmake_minimum_required(VERSION <min>[...<policy_max>] [FATAL_ERROR])",
      "Sets the minimum required version of cmake for a project.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "cmake_minimum_required(VERSION <min>[...<policy_max>] [FATAL_ERROR])\n"
      "```\n\n"
      "### Parameters\n"
      "- `VERSION <min>`: Minimum required CMake version (e.g. 3.20, 3.25).\n"
      "- `...<policy_max>`: Optional maximum version up to which policies are "
      "enabled as NEW.\n"
      "- `FATAL_ERROR`: Ignored in CMake 2.6 and newer (fatal errors are "
      "default).\n\n"
      "### Description\n"
      "Call this command at the very top of any top-level `CMakeLists.txt` "
      "file to specify the required CMake version and initialize default "
      "policy settings.",
      "cmake_minimum_required($0)");

  add("project", Protocol::CompletionItemKind::Function,
      "project(<PROJECT-NAME> [VERSION <major>[.<minor>[.<patch>[.<tweak>]]]] "
      "[DESCRIPTION <desc>] [HOMEPAGE_URL <url>] [LANGUAGES <lang>...])",
      "Sets the name of the project, stores it in `PROJECT_NAME` and "
      "`CMAKE_PROJECT_NAME`, and enables specified languages.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "project(MyProject VERSION 1.0.0 DESCRIPTION \"My App\" LANGUAGES CXX "
      "C)\n"
      "```\n\n"
      "### Sets Variables\n"
      "- `PROJECT_NAME`, `CMAKE_PROJECT_NAME`: Top-level project identifier.\n"
      "- `PROJECT_VERSION`: Full version string.\n"
      "- `PROJECT_VERSION_MAJOR`, `PROJECT_VERSION_MINOR`, "
      "`PROJECT_VERSION_PATCH`, `PROJECT_VERSION_TWEAK`.\n"
      "- `PROJECT_SOURCE_DIR`, `PROJECT_BINARY_DIR`.\n\n"
      "### Languages\n"
      "`C`, `CXX`, `ASM`, `CUDA`, `OBJC`, `OBJCXX`, `Fortran`, `CSharp`, "
      "`Swift`, `ISPC`.",
      "project($0)");

  add("add_executable", Protocol::CompletionItemKind::Function,
      "add_executable(<name> [WIN32] [MACOSX_BUNDLE] [EXCLUDE_FROM_ALL] "
      "[source1] [source2 ...])",
      "Adds an executable target called `<name>` to be built from the source "
      "files listed in the command invocation.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_executable(my_app main.cpp helper.cpp)\n"
      "add_executable(my_app ALIAS existing_target)\n"
      "add_executable(my_app IMPORTED [GLOBAL])\n"
      "```\n\n"
      "### Options\n"
      "- `WIN32`: Creates a Win32 GUI executable (WinMain) instead of a "
      "console application on Windows.\n"
      "- `MACOSX_BUNDLE`: Builds the target as a GUI application bundle on "
      "macOS/iOS.\n"
      "- `EXCLUDE_FROM_ALL`: Target will not be built by default when building "
      "ALL.",
      "add_executable($0)");

  add("add_library", Protocol::CompletionItemKind::Function,
      "add_library(<name> [STATIC | SHARED | MODULE | INTERFACE | OBJECT] "
      "[EXCLUDE_FROM_ALL] [source1] [source2 ...])",
      "Adds a library target called `<name>` to be built from the source files "
      "listed in the command invocation.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_library(mylib STATIC src1.cpp src2.cpp)\n"
      "add_library(mylib SHARED src1.cpp src2.cpp)\n"
      "add_library(mylib INTERFACE)\n"
      "add_library(mylib::alias ALIAS mylib)\n"
      "```\n\n"
      "### Library Types\n"
      "- `STATIC`: Archive of object files (.lib on Windows, .a on Unix).\n"
      "- `SHARED`: Dynamic linked library (.dll on Windows, .so on Linux, "
      ".dylib on macOS).\n"
      "- `MODULE`: Plugin library intended to be loaded at runtime using "
      "dlopen/LoadLibrary.\n"
      "- `INTERFACE`: Header-only library with no compiled binary output.\n"
      "- `OBJECT`: Compiles source files to object files without creating an "
      "archive or shared library.",
      "add_library($0)");

  add("add_subdirectory", Protocol::CompletionItemKind::Function,
      "add_subdirectory(source_dir [binary_dir] [EXCLUDE_FROM_ALL] [SYSTEM])",
      "Adds a subdirectory to the build.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_subdirectory(Source/Core)\n"
      "add_subdirectory(Source/Vendor build/vendor EXCLUDE_FROM_ALL)\n"
      "```\n\n"
      "### Parameters\n"
      "- `source_dir`: Directory containing a `CMakeLists.txt` file.\n"
      "- `binary_dir`: Optional build output directory inside the build tree.\n"
      "- `EXCLUDE_FROM_ALL`: Targets in the subdirectory are excluded from the "
      "default ALL build target.\n"
      "- `SYSTEM`: All directory-level include directories are marked as "
      "SYSTEM includes.",
      "add_subdirectory($0)");

  add("add_compile_options", Protocol::CompletionItemKind::Function,
      "add_compile_options(<option> ...)",
      "Adds options to the compiler command line for targets in the current "
      "directory and below.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_compile_options(-Wall -Wextra -Wpedantic)\n"
      "add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->)\n"
      "```\n\n"
      "### Description\n"
      "Applies compiler flags globally to all targets created after this "
      "command in the current directory scope and child subdirectories. "
      "Supports generator expressions.",
      "add_compile_options($0)");

  add("add_compile_definitions", Protocol::CompletionItemKind::Function,
      "add_compile_definitions(<definition> ...)",
      "Adds preprocessor definitions to the compilation of source files in the "
      "current directory and below.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_compile_definitions(UNICODE _UNICODE NOMINMAX)\n"
      "add_compile_definitions($<$<CONFIG:Debug>:DEBUG_LOGGING=1>)\n"
      "```\n\n"
      "### Description\n"
      "Adds definitions without needing the `-D` flag prefix. Supports "
      "generator expressions.",
      "add_compile_definitions($0)");

  add("add_link_options", Protocol::CompletionItemKind::Function,
      "add_link_options(<option> ...)",
      "Adds options to the link step for targets in the current directory and "
      "below.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_link_options(-Wl,--no-undefined)\n"
      "```",
      "add_link_options($0)");

  add("add_custom_command", Protocol::CompletionItemKind::Function,
      "add_custom_command(OUTPUT output1 [output2 ...] COMMAND command1 [ARGS] "
      "[args1...] [DEPENDS [depends...]] [WORKING_DIRECTORY dir] [COMMENT "
      "comment])",
      "Adds a custom build rule to generate outputs or run pre/post-build "
      "steps.\n\n"
      "### Generating Files (OUTPUT signature)\n"
      "```cmake\n"
      "add_custom_command(\n"
      "    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp\n"
      "    COMMAND python generate.py "
      "${CMAKE_CURRENT_BINARY_DIR}/generated.cpp\n"
      "    DEPENDS generate.py input.txt\n"
      "    COMMENT \"Generating source files\"\n"
      ")\n"
      "```\n\n"
      "### Target Hook (TARGET signature)\n"
      "```cmake\n"
      "add_custom_command(TARGET my_target POST_BUILD COMMAND copy ...)\n"
      "```",
      "add_custom_command($0)");

  add("add_custom_target", Protocol::CompletionItemKind::Function,
      "add_custom_target(Name [ALL] [command1 [args1...]] [DEPENDS depend "
      "depend ... ] [WORKING_DIRECTORY dir] [COMMENT comment])",
      "Adds a target with no output so it will always be built.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_custom_target(run_tests ALL COMMAND ctest --output-on-failure)\n"
      "```\n\n"
      "### Description\n"
      "Creates a named target for running utility scripts, formatting code, "
      "packaging, or custom steps.",
      "add_custom_target($0)");

  add("add_definitions", Protocol::CompletionItemKind::Function,
      "add_definitions(-DFOO -DBAR ...)",
      "Adds -D define flags to compilation of sources in the current directory "
      "and below.\n\n"
      "*(Note: Modern CMake recommends `add_compile_definitions` or "
      "`target_compile_definitions` instead)*",
      "add_definitions($0)");

  add("add_dependencies", Protocol::CompletionItemKind::Function,
      "add_dependencies(<target> [<target-dependency>]...)",
      "Makes a top-level target depend on other top-level targets to ensure "
      "they build before this target.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_dependencies(my_app generate_assets)\n"
      "```",
      "add_dependencies($0)");

  add("target_link_libraries", Protocol::CompletionItemKind::Function,
      "target_link_libraries(<target> <PRIVATE|PUBLIC|INTERFACE> <item>... "
      "[<PRIVATE|PUBLIC|INTERFACE> <item>...]...)",
      "Specifies libraries or flags to use when linking a given target and/or "
      "its dependents.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "target_link_libraries(my_app\n"
      "    PUBLIC\n"
      "        ZDECore\n"
      "    PRIVATE\n"
      "        fmt::fmt\n"
      "        Threads::Threads\n"
      ")\n"
      "```\n\n"
      "### Scope Keywords\n"
      "- `PUBLIC`: Used to compile & link `<target>` AND propagated to "
      "consumers of `<target>`.\n"
      "- `PRIVATE`: Used only to compile & link `<target>` (not propagated).\n"
      "- `INTERFACE`: Not used to build `<target>`, but propagated to "
      "consumers of `<target>`.",
      "target_link_libraries($0)");

  add("target_include_directories", Protocol::CompletionItemKind::Function,
      "target_include_directories(<target> [SYSTEM] [BEFORE] "
      "<INTERFACE|PUBLIC|PRIVATE> [items1...] [<INTERFACE|PUBLIC|PRIVATE> "
      "[items2...] ...])",
      "Specifies include directories to use when compiling a target.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "target_include_directories(my_lib\n"
      "    PUBLIC\n"
      "        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>\n"
      "        $<INSTALL_INTERFACE:include>\n"
      "    PRIVATE\n"
      "        ${CMAKE_CURRENT_SOURCE_DIR}/src\n"
      ")\n"
      "```\n\n"
      "### Parameters\n"
      "- `SYSTEM`: Suppresses compiler warnings from headers in these "
      "directories.\n"
      "- `BEFORE`: Prepends directories to the include path rather than "
      "appending.",
      "target_include_directories($0)");

  add("target_compile_definitions", Protocol::CompletionItemKind::Function,
      "target_compile_definitions(<target> <INTERFACE|PUBLIC|PRIVATE> "
      "[items1...] [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])",
      "Specifies compile preprocessor definitions for a target.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "target_compile_definitions(my_app\n"
      "    PUBLIC\n"
      "        MYAPP_VERSION=\"1.0.0\"\n"
      "    PRIVATE\n"
      "        $<$<CONFIG:Debug>:DEBUG_BUILD=1>\n"
      ")\n"
      "```",
      "target_compile_definitions($0)");

  add("target_compile_features", Protocol::CompletionItemKind::Function,
      "target_compile_features(<target> <PRIVATE|PUBLIC|INTERFACE> "
      "<feature>...)",
      "Specifies compiler features required when compiling a given target.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "target_compile_features(my_target PUBLIC cxx_std_20)\n"
      "```\n\n"
      "### Common Features\n"
      "- `cxx_std_11`, `cxx_std_14`, `cxx_std_17`, `cxx_std_20`, `cxx_std_23`, "
      "`cxx_std_26`\n"
      "- `c_std_99`, `c_std_11`, `c_std_17`, `c_std_23`",
      "target_compile_features($0)");

  add("target_compile_options", Protocol::CompletionItemKind::Function,
      "target_compile_options(<target> [BEFORE] <INTERFACE|PUBLIC|PRIVATE> "
      "[items1...] [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])",
      "Specifies compiler flags and options for a specific target.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "target_compile_options(my_app PRIVATE\n"
      "    $<$<CXX_COMPILER_ID:Clang>:-Wshadow -Wconversion>\n"
      "    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>\n"
      ")\n"
      "```",
      "target_compile_options($0)");

  add("target_link_directories", Protocol::CompletionItemKind::Function,
      "target_link_directories(<target> [BEFORE] <INTERFACE|PUBLIC|PRIVATE> "
      "[items1...] [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])",
      "Specifies link directories for a target.",
      "target_link_directories($0)");

  add("target_link_options", Protocol::CompletionItemKind::Function,
      "target_link_options(<target> [BEFORE] <INTERFACE|PUBLIC|PRIVATE> "
      "[items1...] [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])",
      "Specifies options to use when linking a target.",
      "target_link_options($0)");

  add("target_precompile_headers", Protocol::CompletionItemKind::Function,
      "target_precompile_headers(<target> <INTERFACE|PUBLIC|PRIVATE> "
      "[header1...] [<INTERFACE|PUBLIC|PRIVATE> [header2...] ...])",
      "Specifies a list of header files to precompile (PCH) for speeding up "
      "builds.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "target_precompile_headers(my_app PRIVATE <vector> <string> <memory> "
      "\"pch.h\")\n"
      "```",
      "target_precompile_headers($0)");

  add("target_sources", Protocol::CompletionItemKind::Function,
      "target_sources(<target> <INTERFACE|PUBLIC|PRIVATE> [items1...] "
      "[<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])",
      "Adds source files to an existing target.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "target_sources(my_target PRIVATE src/extra.cpp include/extra.h)\n"
      "```",
      "target_sources($0)");

  add("set_target_properties", Protocol::CompletionItemKind::Function,
      "set_target_properties(target1 target2 ... PROPERTIES prop1 value1 prop2 "
      "value2 ...)",
      "Sets properties on targets (e.g. CXX_STANDARD 20, OUTPUT_NAME, "
      "POSITION_INDEPENDENT_CODE ON).\n\n"
      "### Syntax\n"
      "```cmake\n"
      "set_target_properties(my_target PROPERTIES\n"
      "    CXX_STANDARD 20\n"
      "    CXX_STANDARD_REQUIRED ON\n"
      "    CXX_EXTENSIONS OFF\n"
      "    OUTPUT_NAME \"myapp\"\n"
      ")\n"
      "```",
      "set_target_properties($0)");

  add("get_target_property", Protocol::CompletionItemKind::Function,
      "get_target_property(<VAR> [target] <property-name>)",
      "Gets a property of a target and stores it in `<VAR>`.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "get_target_property(LOC my_target LOCATION)\n"
      "```",
      "get_target_property($0)");

  add("set_property", Protocol::CompletionItemKind::Function,
      "set_property(<GLOBAL | DIRECTORY [dir] | TARGET [target ...] | SOURCE "
      "[src ...] | INSTALL [file ...] | TEST [test ...] | CACHE [entry ...]> "
      "[APPEND] [APPEND_STRING] PROPERTY <name> [<value1> ...])",
      "Sets a named property in a given scope (GLOBAL, DIRECTORY, TARGET, "
      "SOURCE, TEST, CACHE).",
      "set_property($0)");

  add("get_property", Protocol::CompletionItemKind::Function,
      "get_property(<variable> <GLOBAL | DIRECTORY [dir] | TARGET <target> | "
      "SOURCE <source> [TARGET_DIRECTORY <dir>] | INSTALL <file> | TEST <test> "
      "| CACHE <entry> | VARIABLE> PROPERTY <name> [SET | DEFINED | BRIEF_DOCS "
      "| FULL_DOCS])",
      "Gets a named property in a given scope.", "get_property($0)");

  add("find_package", Protocol::CompletionItemKind::Function,
      "find_package(<PackageName> [version] [EXACT] [QUIET] [MODULE] "
      "[REQUIRED] [[COMPONENTS] [components...]] [OPTIONAL_COMPONENTS "
      "components...])",
      "Finds and loads settings from an external package.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "find_package(Threads REQUIRED)\n"
      "find_package(OpenSSL 1.1.1 REQUIRED)\n"
      "find_package(Boost 1.70 REQUIRED COMPONENTS filesystem system)\n"
      "```\n\n"
      "### Modes\n"
      "- `Module Mode`: Uses a `Find<PackageName>.cmake` script.\n"
      "- `Config Mode`: Searches for `<PackageName>Config.cmake` or "
      "`<lowercase-package>-config.cmake` provided by the installed library.",
      "find_package($0)");

  add("find_path", Protocol::CompletionItemKind::Function,
      "find_path(<VAR> name1 [path1 path2 ...])",
      "Finds the directory containing a file (e.g. a header file).",
      "find_path($0)");

  add("find_library", Protocol::CompletionItemKind::Function,
      "find_library(<VAR> name1 [path1 path2 ...])",
      "Finds a library file on disk.", "find_library($0)");

  add("find_file", Protocol::CompletionItemKind::Function,
      "find_file(<VAR> name1 [path1 path2 ...])",
      "Finds the full path to a named file.", "find_file($0)");

  add("find_program", Protocol::CompletionItemKind::Function,
      "find_program(<VAR> name1 [path1 path2 ...])",
      "Finds an executable program in PATH or specified search directories.",
      "find_program($0)");

  add("set", Protocol::CompletionItemKind::Function,
      "set(<variable> <value>... [PARENT_SCOPE])\nset(<variable> <value>... "
      "CACHE <type> <docstring> [FORCE])",
      "Sets a normal, parent-scope, or cached environment/CMake variable.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "set(MY_VAR \"hello world\")\n"
      "set(SOURCES main.cpp util.cpp)\n"
      "set(BUILD_TESTS ON CACHE BOOL \"Build test suite\" FORCE)\n"
      "```",
      "set($0)");

  add("unset", Protocol::CompletionItemKind::Function,
      "unset(<variable> [CACHE | PARENT_SCOPE])",
      "Unsets a variable, cache variable, or environment variable.",
      "unset($0)");

  add("option", Protocol::CompletionItemKind::Function,
      "option(<variable> \"<help_text>\" [value])",
      "Provides an option that the user can select (ON/OFF).\n\n"
      "### Syntax\n"
      "```cmake\n"
      "option(BUILD_SHARED_LIBS \"Build shared libraries\" OFF)\n"
      "option(ENABLE_TESTS \"Build and run test suite\" ON)\n"
      "```",
      "option($0)");

  add("message", Protocol::CompletionItemKind::Function,
      "message([<mode>] \"message text\" ...)",
      "Logs a message to the CMake console output.\n\n"
      "### Modes\n"
      "- `STATUS`: Informative status message (starts with `--`).\n"
      "- `WARNING`: CMake warning message.\n"
      "- `AUTHOR_WARNING`: Project author warning message.\n"
      "- `FATAL_ERROR`: Error message that immediately halts configuration.\n"
      "- `SEND_ERROR`: Error message that allows configuration to continue but "
      "fails build file generation.\n"
      "- `NOTICE`, `VERBOSE`, `DEBUG`, `TRACE`.",
      "message($0)");

  add("include", Protocol::CompletionItemKind::Function,
      "include(<file|module> [OPTIONAL] [RESULT_VARIABLE <var>] "
      "[NO_POLICY_SCOPE])",
      "Loads and runs CMake code from a file or standard module.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "include(CTest)\n"
      "include(FetchContent)\n"
      "include(GNUInstallDirs)\n"
      "```",
      "include($0)");

  add("include_guard", Protocol::CompletionItemKind::Function,
      "include_guard([DIRECTORY | GLOBAL])",
      "Provides an include guard for the file currently being processed by "
      "CMake, preventing double inclusion.",
      "include_guard($0)");

  add("include_directories", Protocol::CompletionItemKind::Function,
      "include_directories([AFTER|BEFORE] [SYSTEM] dir1 [dir2 ...])",
      "Adds the given directories to those the compiler uses to search for "
      "include files globally for the current directory.",
      "include_directories($0)");

  add("link_directories", Protocol::CompletionItemKind::Function,
      "link_directories([AFTER|BEFORE] dir1 [dir2 ...])",
      "Adds directories in which the linker will look for libraries.",
      "link_directories($0)");

  add("configure_file", Protocol::CompletionItemKind::Function,
      "configure_file(<input> <output> [@ONLY] [ESCAPE_QUOTES] "
      "[NO_SOURCE_PERMISSIONS])",
      "Copies an `<input>` file to an `<output>` file and substitutes variable "
      "values referenced as `@VAR@` or `${VAR}`.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)\n"
      "```",
      "configure_file($0)");

  add("file", Protocol::CompletionItemKind::Function,
      "file(READ|WRITE|APPEND|GLOB|GLOB_RECURSE|MAKE_DIRECTORY|REMOVE|COPY|"
      "DOWNLOAD|...) ...",
      "Comprehensive file manipulation command.\n\n"
      "### Subcommands\n"
      "- `file(GLOB_RECURSE SOURCES \"src/*.cpp\")`\n"
      "- `file(READ filename var)`\n"
      "- `file(WRITE filename \"content\")`\n"
      "- `file(MAKE_DIRECTORY dir)`\n"
      "- `file(COPY source DESTINATION dest)`\n"
      "- `file(DOWNLOAD url destination [STATUS status])`",
      "file($0)");

  add("list", Protocol::CompletionItemKind::Function,
      "list(LENGTH|GET|APPEND|PREPEND|FIND|INSERT|REMOVE_ITEM|REMOVE_AT|REMOVE_"
      "DUPLICATES|SORT|REVERSE|JOIN|TRANSFORM) ...",
      "List operations and manipulations.\n\n"
      "### Subcommands\n"
      "- `list(APPEND list items...)`\n"
      "- `list(LENGTH list var)`\n"
      "- `list(REMOVE_ITEM list items...)`\n"
      "- `list(REMOVE_DUPLICATES list)`\n"
      "- `list(JOIN list glue var)`\n"
      "- `list(SORT list)`",
      "list($0)");

  add("string", Protocol::CompletionItemKind::Function,
      "string(REGEX|REPLACE|TOLOWER|TOUPPER|LENGTH|SUBSTRING|STRIP|GENEX_STRIP|"
      "...) ...",
      "String operations (search, regex, replace, uppercase, substring, "
      "compare, hashing, formatting).\n\n"
      "### Subcommands\n"
      "- `string(REPLACE match replace out_var input)`\n"
      "- `string(TOLOWER input out_var)`\n"
      "- `string(TOUPPER input out_var)`\n"
      "- `string(REGEX MATCH regex out_var input)`\n"
      "- `string(REGEX REPLACE regex replace out_var input)`",
      "string($0)");

  add("math", Protocol::CompletionItemKind::Function,
      "math(EXPR <variable> \"<expression>\" [OUTPUT_FORMAT "
      "<DECIMAL|HEXADECIMAL>])",
      "Evaluates a mathematical expression.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "math(EXPR RESULT \"1 + 2 * 3\")\n"
      "```",
      "math($0)");

  add("install", Protocol::CompletionItemKind::Function,
      "install(TARGETS targets... [EXPORT <export-name>] "
      "[[ARCHIVE|LIBRARY|RUNTIME|INCLUDES] [DESTINATION <dir>]] ...)",
      "Specifies rules which run at install time.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "install(TARGETS myapp mylib\n"
      "    RUNTIME DESTINATION bin\n"
      "    LIBRARY DESTINATION lib\n"
      "    ARCHIVE DESTINATION lib\n"
      ")\n"
      "```",
      "install($0)");

  add("enable_testing", Protocol::CompletionItemKind::Function,
      "enable_testing()",
      "Enables testing for this directory-tree in CMake / CTest. Must be "
      "called in the root CMakeLists.txt.",
      "enable_testing()");

  add("add_test", Protocol::CompletionItemKind::Function,
      "add_test(NAME <name> COMMAND <command> [<arg>...] [CONFIGURATIONS "
      "<config>...] [WORKING_DIRECTORY <dir>])",
      "Adds a test to the project with given arguments to be run by CTest.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "add_test(NAME UnitTests COMMAND test_runner --gtest_output=xml)\n"
      "```",
      "add_test($0)");

  add("cmake_policy", Protocol::CompletionItemKind::Function,
      "cmake_policy(SET CMP<NNNN> NEW|OLD)\ncmake_policy(GET CMP<NNNN> <VAR>)",
      "Manages CMake policy settings.", "cmake_policy($0)");

  add("cmake_path", Protocol::CompletionItemKind::Function,
      "cmake_path(GET|SET|APPEND|REMOVE_FILENAME|...) ...",
      "Performs filesystem path operations and conversions.", "cmake_path($0)");

  add("cmake_host_system_information", Protocol::CompletionItemKind::Function,
      "cmake_host_system_information(RESULT <variable> QUERY <key> ...)",
      "Queries system information of the host processor, memory, and OS.",
      "cmake_host_system_information($0)");

  add("execute_process", Protocol::CompletionItemKind::Function,
      "execute_process(COMMAND <cmd1> [args1...]] [WORKING_DIRECTORY <dir>] "
      "[TIMEOUT <seconds>] [RESULT_VARIABLE <var>] [OUTPUT_VARIABLE <var>] "
      "[ERROR_VARIABLE <var>])",
      "Executes one or more child processes during CMake configuration time.",
      "execute_process($0)");

  add("source_group", Protocol::CompletionItemKind::Function,
      "source_group([TREE <root>] [PREFIX <prefix>] [FILES <src>...])",
      "Defines a grouping for source files in IDE project hierarchies (Visual "
      "Studio, Xcode).",
      "source_group($0)");

  add("FetchContent_Declare", Protocol::CompletionItemKind::Function,
      "FetchContent_Declare(<name> <contentOptions>...)",
      "Declares details for how to fetch an external dependency.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "FetchContent_Declare(\n"
      "    googletest\n"
      "    GIT_REPOSITORY https://github.com/google/googletest.git\n"
      "    GIT_TAG        v1.14.0\n"
      ")\n"
      "```",
      "FetchContent_Declare($0)");

  add("FetchContent_MakeAvailable", Protocol::CompletionItemKind::Function,
      "FetchContent_MakeAvailable(<name1> [<name2>...])",
      "Ensures that named dependencies have been populated and added to the "
      "build.\n\n"
      "### Syntax\n"
      "```cmake\n"
      "FetchContent_MakeAvailable(googletest fmt nlohmann_json)\n"
      "```",
      "FetchContent_MakeAvailable($0)");

  // Flow Control
  add("if", Protocol::CompletionItemKind::Keyword,
      "if(<condition>)\n  <commands>\n[elseif(<condition>)\n  "
      "<commands>]\n[else()\n  <commands>]\nendif()",
      "Conditionally executes a group of commands.",
      "if(${1:condition})\n    $0\nendif()\n");

  add("elseif", Protocol::CompletionItemKind::Keyword, "elseif(<condition>)",
      "Starts an else-if conditional block.",
      "elseif(${1:condition})\n    $0\n");

  add("else", Protocol::CompletionItemKind::Keyword, "else()",
      "Starts an else conditional block.", "else()\n    $0\n");

  add("endif", Protocol::CompletionItemKind::Keyword, "endif()",
      "Ends an if-elseif-else block.", "endif()\n");

  add("foreach", Protocol::CompletionItemKind::Keyword,
      "foreach(<loop_var> <items>...)\n  <commands>\nendforeach()",
      "Evaluates a group of commands for each value in a list or range.",
      "foreach(${1:item} IN LISTS ${2:items})\n    $0\nendforeach()\n");

  add("endforeach", Protocol::CompletionItemKind::Keyword, "endforeach()",
      "Ends a foreach loop block.", "endforeach()\n");

  add("while", Protocol::CompletionItemKind::Keyword,
      "while(<condition>)\n  <commands>\nendwhile()",
      "Evaluates a group of commands while condition is true.",
      "while(${1:condition})\n    $0\nendwhile()\n");

  add("endwhile", Protocol::CompletionItemKind::Keyword, "endwhile()",
      "Ends a while loop block.", "endwhile()\n");

  add("function", Protocol::CompletionItemKind::Keyword,
      "function(<name> [<arg1> ...])\n  <commands>\nendfunction()",
      "Starts recording a function for later invocation as a command.",
      "function(${1:function_name} ${2:arg1})\n    $0\nendfunction()\n");

  add("endfunction", Protocol::CompletionItemKind::Keyword, "endfunction()",
      "Ends a function definition block.", "endfunction()\n");

  add("macro", Protocol::CompletionItemKind::Keyword,
      "macro(<name> [<arg1> ...])\n  <commands>\nendmacro()",
      "Starts recording a macro for later invocation as a command.",
      "macro(${1:macro_name} ${2:arg1})\n    $0\nendmacro()\n");

  add("endmacro", Protocol::CompletionItemKind::Keyword, "endmacro()",
      "Ends a macro definition block.", "endmacro()\n");

  add("return", Protocol::CompletionItemKind::Keyword,
      "return([PROPAGATE <var1> ...])",
      "Returns from a file, directory, or function.", "return()\n");

  add("break", Protocol::CompletionItemKind::Keyword, "break()",
      "Breaks from an enclosing foreach or while loop.", "break()\n");

  add("continue", Protocol::CompletionItemKind::Keyword, "continue()",
      "Continues the enclosing foreach or while loop.", "continue()\n");

  // =========================================================================
  // 2. CMAKE STANDARD & BUILT-IN VARIABLES
  // =========================================================================
  add("VERSION", Protocol::CompletionItemKind::Variable, "VERSION <version>",
      "Specifies a version requirement (e.g. 3.20 or 1.0.0).",
      "VERSION ${1:3.20}");

  add("MSVC_VERSION", Protocol::CompletionItemKind::Variable, "MSVC_VERSION",
      "The version of Microsoft Visual C/C++ compiler being used (e.g. 1930 "
      "for VS 2022, 1920 for VS 2019).",
      "MSVC_VERSION");

  add("CMAKE_VERSION", Protocol::CompletionItemKind::Variable, "CMAKE_VERSION",
      "The full version of CMake currently running in "
      "major.minor.patch[-tweak][-id] format.",
      "CMAKE_VERSION");

  add("XCODE_VERSION", Protocol::CompletionItemKind::Variable, "XCODE_VERSION",
      "Version of Xcode (macOS only).", "XCODE_VERSION");

  add("PROJECT_VERSION", Protocol::CompletionItemKind::Variable,
      "PROJECT_VERSION",
      "Value of the VERSION parameter to the most recent project() command "
      "call.",
      "PROJECT_VERSION");

  add("CMAKE_TLS_VERSION", Protocol::CompletionItemKind::Variable,
      "CMAKE_TLS_VERSION",
      "Specify the default TLS version for https:// and secure operations.",
      "CMAKE_TLS_VERSION");

  add("CTEST_TLS_VERSION", Protocol::CompletionItemKind::Variable,
      "CTEST_TLS_VERSION", "Specify default TLS version for ctest operations.",
      "CTEST_TLS_VERSION");

  add("FRAMEWORK_VERSION", Protocol::CompletionItemKind::Variable,
      "FRAMEWORK_VERSION", "Version of a framework on Apple platforms.",
      "FRAMEWORK_VERSION");

  add("PROJECT_VERSION_MAJOR", Protocol::CompletionItemKind::Variable,
      "PROJECT_VERSION_MAJOR",
      "First version number component of the PROJECT_VERSION.",
      "PROJECT_VERSION_MAJOR");

  add("PROJECT_VERSION_MINOR", Protocol::CompletionItemKind::Variable,
      "PROJECT_VERSION_MINOR",
      "Second version number component of the PROJECT_VERSION.",
      "PROJECT_VERSION_MINOR");

  add("PROJECT_VERSION_PATCH", Protocol::CompletionItemKind::Variable,
      "PROJECT_VERSION_PATCH",
      "Third version number component of the PROJECT_VERSION.",
      "PROJECT_VERSION_PATCH");

  add("PROJECT_VERSION_TWEAK", Protocol::CompletionItemKind::Variable,
      "PROJECT_VERSION_TWEAK",
      "Fourth version number component of the PROJECT_VERSION.",
      "PROJECT_VERSION_TWEAK");

  add("CMAKE_CURRENT_SOURCE_DIR", Protocol::CompletionItemKind::Variable,
      "CMAKE_CURRENT_SOURCE_DIR",
      "The path to the source directory currently being processed by CMake.",
      "CMAKE_CURRENT_SOURCE_DIR");

  add("CMAKE_CURRENT_BINARY_DIR", Protocol::CompletionItemKind::Variable,
      "CMAKE_CURRENT_BINARY_DIR",
      "The path to the binary build directory currently being processed by "
      "CMake.",
      "CMAKE_CURRENT_BINARY_DIR");

  add("CMAKE_SOURCE_DIR", Protocol::CompletionItemKind::Variable,
      "CMAKE_SOURCE_DIR", "The path to the top level of the source tree.",
      "CMAKE_SOURCE_DIR");

  add("CMAKE_BINARY_DIR", Protocol::CompletionItemKind::Variable,
      "CMAKE_BINARY_DIR", "The path to the top level of the build tree.",
      "CMAKE_BINARY_DIR");

  add("PROJECT_NAME", Protocol::CompletionItemKind::Variable, "PROJECT_NAME",
      "Name of the project given to the project() command.", "PROJECT_NAME");

  add("PROJECT_SOURCE_DIR", Protocol::CompletionItemKind::Variable,
      "PROJECT_SOURCE_DIR",
      "Top level source directory for the current project.",
      "PROJECT_SOURCE_DIR");

  add("PROJECT_BINARY_DIR", Protocol::CompletionItemKind::Variable,
      "PROJECT_BINARY_DIR",
      "Top level binary directory for the current project.",
      "PROJECT_BINARY_DIR");

  add("CMAKE_CXX_STANDARD", Protocol::CompletionItemKind::Variable,
      "CMAKE_CXX_STANDARD",
      "The default C++ standard requested to build C++ targets (e.g. 11, 14, "
      "17, 20, 23).",
      "CMAKE_CXX_STANDARD");

  add("CMAKE_CXX_STANDARD_REQUIRED", Protocol::CompletionItemKind::Variable,
      "CMAKE_CXX_STANDARD_REQUIRED",
      "Boolean describing whether CMAKE_CXX_STANDARD is a hard requirement "
      "(ON/OFF).",
      "CMAKE_CXX_STANDARD_REQUIRED");

  add("CMAKE_CXX_EXTENSIONS", Protocol::CompletionItemKind::Variable,
      "CMAKE_CXX_EXTENSIONS",
      "Boolean describing whether compiler specific extensions (e.g. "
      "-std=gnu++20) are enabled (ON/OFF).",
      "CMAKE_CXX_EXTENSIONS");

  add("CMAKE_C_STANDARD", Protocol::CompletionItemKind::Variable,
      "CMAKE_C_STANDARD",
      "The default C standard requested to build C targets (e.g. 99, 11, 17, "
      "23).",
      "CMAKE_C_STANDARD");

  add("CMAKE_BUILD_TYPE", Protocol::CompletionItemKind::Variable,
      "CMAKE_BUILD_TYPE",
      "Specifies the build type on single-configuration generators (Debug, "
      "Release, RelWithDebInfo, MinSizeRel).",
      "CMAKE_BUILD_TYPE");

  add("CMAKE_INSTALL_PREFIX", Protocol::CompletionItemKind::Variable,
      "CMAKE_INSTALL_PREFIX",
      "Install directory used by install(). Defaults to /usr/local on UNIX and "
      "C:/Program Files/<ProjectName> on Windows.",
      "CMAKE_INSTALL_PREFIX");

  add("CMAKE_EXPORT_COMPILE_COMMANDS", Protocol::CompletionItemKind::Variable,
      "CMAKE_EXPORT_COMPILE_COMMANDS",
      "Enable/Disable output of compile_commands.json for clangd and "
      "clang-tidy (ON/OFF).",
      "CMAKE_EXPORT_COMPILE_COMMANDS");

  add("CMAKE_TOOLCHAIN_FILE", Protocol::CompletionItemKind::Variable,
      "CMAKE_TOOLCHAIN_FILE",
      "Path to toolchain file (e.g. vcpkg.cmake or android.toolchain.cmake).",
      "CMAKE_TOOLCHAIN_FILE");

  add("CMAKE_MODULE_PATH", Protocol::CompletionItemKind::Variable,
      "CMAKE_MODULE_PATH",
      "Semicolon-separated list of directories specifying a search path for "
      "CMake modules to be loaded by include() or find_package().",
      "CMAKE_MODULE_PATH");

  add("CMAKE_PREFIX_PATH", Protocol::CompletionItemKind::Variable,
      "CMAKE_PREFIX_PATH",
      "Semicolon-separated list of search path prefixes for find_package, "
      "find_program, find_library, find_file, and find_path.",
      "CMAKE_PREFIX_PATH");

  add("BUILD_SHARED_LIBS", Protocol::CompletionItemKind::Variable,
      "BUILD_SHARED_LIBS",
      "Global flag to cause add_library() to create shared libraries if no "
      "library type is explicitly specified (ON/OFF).",
      "BUILD_SHARED_LIBS");

  add("WIN32", Protocol::CompletionItemKind::Variable, "WIN32",
      "Set to True on Windows operating systems, including Win64.", "WIN32");

  add("MSVC", Protocol::CompletionItemKind::Variable, "MSVC",
      "Set to True when the compiler is some version of Microsoft C/C++.",
      "MSVC");

  add("APPLE", Protocol::CompletionItemKind::Variable, "APPLE",
      "Set to True on Apple platforms (macOS, iOS, tvOS, watchOS, visionOS).",
      "APPLE");

  add("UNIX", Protocol::CompletionItemKind::Variable, "UNIX",
      "Set to True on UNIX-like platforms (Linux, macOS, BSD, Solaris, "
      "Cygwin).",
      "UNIX");

  add("MINGW", Protocol::CompletionItemKind::Variable, "MINGW",
      "Set to True when using MinGW compiler toolchain.", "MINGW");

  add("CYGWIN", Protocol::CompletionItemKind::Variable, "CYGWIN",
      "Set to True when using Cygwin compiler toolchain.", "CYGWIN");

  add("CMAKE_SYSTEM_NAME", Protocol::CompletionItemKind::Variable,
      "CMAKE_SYSTEM_NAME",
      "The name of the operating system for which CMake is building (e.g. "
      "Windows, Linux, Darwin, Android).",
      "CMAKE_SYSTEM_NAME");

  add("CMAKE_SYSTEM_VERSION", Protocol::CompletionItemKind::Variable,
      "CMAKE_SYSTEM_VERSION",
      "The version of the operating system for which CMake is building.",
      "CMAKE_SYSTEM_VERSION");

  add("CMAKE_SYSTEM_PROCESSOR", Protocol::CompletionItemKind::Variable,
      "CMAKE_SYSTEM_PROCESSOR",
      "The name of the CPU architecture for which CMake is building (e.g. "
      "x86_64, AMD64, arm64, aarch64, x86).",
      "CMAKE_SYSTEM_PROCESSOR");

  add("CMAKE_CXX_FLAGS", Protocol::CompletionItemKind::Variable,
      "CMAKE_CXX_FLAGS", "Flags for C++ compiler for all build types.",
      "CMAKE_CXX_FLAGS");

  add("CMAKE_C_FLAGS", Protocol::CompletionItemKind::Variable, "CMAKE_C_FLAGS",
      "Flags for C compiler for all build types.", "CMAKE_C_FLAGS");

  add("CMAKE_EXE_LINKER_FLAGS", Protocol::CompletionItemKind::Variable,
      "CMAKE_EXE_LINKER_FLAGS",
      "Linker flags to be used to create executables.",
      "CMAKE_EXE_LINKER_FLAGS");

  add("CMAKE_SHARED_LINKER_FLAGS", Protocol::CompletionItemKind::Variable,
      "CMAKE_SHARED_LINKER_FLAGS",
      "Linker flags to be used to create shared libraries.",
      "CMAKE_SHARED_LINKER_FLAGS");

  add("CMAKE_STATIC_LINKER_FLAGS", Protocol::CompletionItemKind::Variable,
      "CMAKE_STATIC_LINKER_FLAGS",
      "Archiver/linker flags to be used to create static libraries.",
      "CMAKE_STATIC_LINKER_FLAGS");

  // =========================================================================
  // 3. TARGET PROPERTIES & KEYWORDS
  // =========================================================================
  add("PUBLIC", Protocol::CompletionItemKind::Keyword, "PUBLIC",
      "Specifies public target requirements (used by both target and its "
      "consumers).",
      "PUBLIC ");

  add("PRIVATE", Protocol::CompletionItemKind::Keyword, "PRIVATE",
      "Specifies private target requirements (used only by the target itself).",
      "PRIVATE ");

  add("INTERFACE", Protocol::CompletionItemKind::Keyword, "INTERFACE",
      "Specifies interface target requirements (used only by consumers of "
      "target).",
      "INTERFACE ");

  add("REQUIRED", Protocol::CompletionItemKind::Keyword, "REQUIRED",
      "Halts configuration with an error if package / dependency is not found.",
      "REQUIRED");

  add("COMPONENTS", Protocol::CompletionItemKind::Keyword, "COMPONENTS",
      "Specifies a list of package components to find in find_package().",
      "COMPONENTS ");

  add("CONFIG", Protocol::CompletionItemKind::Keyword, "CONFIG",
      "Forces find_package() to use Config mode (PackageConfig.cmake) rather "
      "than Find module.",
      "CONFIG");

  add("EXACT", Protocol::CompletionItemKind::Keyword, "EXACT",
      "Requires exact version match in find_package().", "EXACT");

  add("QUIET", Protocol::CompletionItemKind::Keyword, "QUIET",
      "Suppresses informative messages if package is not found.", "QUIET");

  add("STATIC", Protocol::CompletionItemKind::Keyword, "STATIC",
      "Declares a static library archive (.lib / .a).", "STATIC ");

  add("SHARED", Protocol::CompletionItemKind::Keyword, "SHARED",
      "Declares a dynamic link shared library (.dll / .so / .dylib).",
      "SHARED ");

  add("MODULE", Protocol::CompletionItemKind::Keyword, "MODULE",
      "Declares a plugin module library loaded dynamically at runtime.",
      "MODULE ");

  add("OBJECT", Protocol::CompletionItemKind::Keyword, "OBJECT",
      "Declares an object library containing compiled object files without "
      "linking.",
      "OBJECT ");

  add("ALIAS", Protocol::CompletionItemKind::Keyword, "ALIAS",
      "Creates an alias name for an existing target.", "ALIAS ");

  add("IMPORTED", Protocol::CompletionItemKind::Keyword, "IMPORTED",
      "Declares an imported target that already exists outside the project.",
      "IMPORTED");

  add("GLOBAL", Protocol::CompletionItemKind::Keyword, "GLOBAL",
      "Makes target or property globally accessible across all CMake "
      "subdirectories.",
      "GLOBAL");

  add("SYSTEM", Protocol::CompletionItemKind::Keyword, "SYSTEM",
      "Marks directory as a system include directory (suppressing compiler "
      "warnings).",
      "SYSTEM");

  add("STATUS", Protocol::CompletionItemKind::Keyword, "STATUS",
      "Prints informative log status message.", "STATUS \"${1:Message}\"");

  add("WARNING", Protocol::CompletionItemKind::Keyword, "WARNING",
      "Prints a CMake warning message and continues processing.",
      "WARNING \"${1:Message}\"");

  add("AUTHOR_WARNING", Protocol::CompletionItemKind::Keyword, "AUTHOR_WARNING",
      "Prints a CMake project author warning and continues processing.",
      "AUTHOR_WARNING \"${1:Message}\"");

  add("FATAL_ERROR", Protocol::CompletionItemKind::Keyword, "FATAL_ERROR",
      "Prints a fatal error message and immediately stops CMake configuration.",
      "FATAL_ERROR \"${1:Message}\"");

  add("SEND_ERROR", Protocol::CompletionItemKind::Keyword, "SEND_ERROR",
      "Prints an error message, continues configuration, but skips build file "
      "generation.",
      "SEND_ERROR \"${1:Message}\"");

  add("LANGUAGES", Protocol::CompletionItemKind::Keyword, "LANGUAGES <lang>...",
      "Specifies programming languages enabled in project (CXX, C, ASM, CUDA, "
      "OBJC, OBJCXX).",
      "LANGUAGES CXX");

  add("DESCRIPTION", Protocol::CompletionItemKind::Keyword,
      "DESCRIPTION \"<string>\"",
      "Specifies a human-readable description for project().",
      "DESCRIPTION \"${1:Description}\"");

  add("HOMEPAGE_URL", Protocol::CompletionItemKind::Keyword,
      "HOMEPAGE_URL \"<url>\"", "Specifies project homepage URL for project().",
      "HOMEPAGE_URL \"${1:https://example.com}\"");

  add("PROPERTIES", Protocol::CompletionItemKind::Keyword,
      "PROPERTIES <prop1> <val1> ...",
      "Starts list of target property assignments.", "PROPERTIES\n    ");

  add("CXX_STANDARD", Protocol::CompletionItemKind::Property,
      "CXX_STANDARD <11|14|17|20|23>",
      "The C++ standard whose features are required to build this target.",
      "CXX_STANDARD 20");

  add("CXX_STANDARD_REQUIRED", Protocol::CompletionItemKind::Property,
      "CXX_STANDARD_REQUIRED <ON|OFF>",
      "Boolean describing whether the value of CXX_STANDARD is a requirement.",
      "CXX_STANDARD_REQUIRED ON");

  add("CXX_EXTENSIONS", Protocol::CompletionItemKind::Property,
      "CXX_EXTENSIONS <ON|OFF>",
      "Boolean describing whether compiler specific extensions are enabled.",
      "CXX_EXTENSIONS OFF");

  add("OUTPUT_NAME", Protocol::CompletionItemKind::Property,
      "OUTPUT_NAME \"<name>\"",
      "Sets the base name for the output file produced by a target.",
      "OUTPUT_NAME \"${1:name}\"");

  add("RUNTIME_OUTPUT_DIRECTORY", Protocol::CompletionItemKind::Property,
      "RUNTIME_OUTPUT_DIRECTORY \"<dir>\"",
      "Output directory in which RUNTIME target files (executables, DLLs) "
      "should be built.",
      "RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin\"");

  add("LIBRARY_OUTPUT_DIRECTORY", Protocol::CompletionItemKind::Property,
      "LIBRARY_OUTPUT_DIRECTORY \"<dir>\"",
      "Output directory in which LIBRARY target files (.so, .dylib) should be "
      "built.",
      "LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib\"");

  add("ARCHIVE_OUTPUT_DIRECTORY", Protocol::CompletionItemKind::Property,
      "ARCHIVE_OUTPUT_DIRECTORY \"<dir>\"",
      "Output directory in which ARCHIVE target files (.lib, .a) should be "
      "built.",
      "ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib\"");

  add("POSITION_INDEPENDENT_CODE", Protocol::CompletionItemKind::Property,
      "POSITION_INDEPENDENT_CODE <ON|OFF>",
      "Whether to create a position-independent target (-fPIC).",
      "POSITION_INDEPENDENT_CODE ON");

  add("NOT", Protocol::CompletionItemKind::Keyword, "NOT <condition>",
      "Logical NOT condition.", "NOT ");
  add("AND", Protocol::CompletionItemKind::Keyword, "<cond1> AND <cond2>",
      "Logical AND condition.", "AND ");
  add("OR", Protocol::CompletionItemKind::Keyword, "<cond1> OR <cond2>",
      "Logical OR condition.", "OR ");
  add("DEFINED", Protocol::CompletionItemKind::Keyword, "DEFINED <variable>",
      "True if the given variable is defined.", "DEFINED ");
  add("EXISTS", Protocol::CompletionItemKind::Keyword, "EXISTS <path>",
      "True if named file or directory exists.", "EXISTS ");
  add("COMMAND", Protocol::CompletionItemKind::Keyword, "COMMAND <name>",
      "True if the given name is a command, macro or function.", "COMMAND ");
  add("TARGET", Protocol::CompletionItemKind::Keyword, "TARGET <name>",
      "True if the given name is an existing target.", "TARGET ");
  add("STREQUAL", Protocol::CompletionItemKind::Keyword, "STREQUAL <string>",
      "True if strings are equal.", "STREQUAL ");
  add("STRLESS", Protocol::CompletionItemKind::Keyword, "STRLESS <string>",
      "True if string is lexicographically less.", "STRLESS ");
  add("STRGREATER", Protocol::CompletionItemKind::Keyword,
      "STRGREATER <string>", "True if string is lexicographically greater.",
      "STRGREATER ");
  add("VERSION_LESS", Protocol::CompletionItemKind::Keyword,
      "VERSION_LESS <ver>", "True if version is less.", "VERSION_LESS ");
  add("VERSION_GREATER", Protocol::CompletionItemKind::Keyword,
      "VERSION_GREATER <ver>", "True if version is greater.",
      "VERSION_GREATER ");
  add("VERSION_EQUAL", Protocol::CompletionItemKind::Keyword,
      "VERSION_EQUAL <ver>", "True if versions are equal.", "VERSION_EQUAL ");
  add("MATCHES", Protocol::CompletionItemKind::Keyword, "MATCHES <regex>",
      "True if string matches regular expression.", "MATCHES ");
}

std::vector<Protocol::CompletionItem>
CMakeLanguageDatabase::get_all_completions() const {
  return m_all_completions;
}

std::vector<Protocol::CompletionItem>
CMakeLanguageDatabase::get_completions_for_context(
    std::string_view current_line, std::size_t caret_column) const {
  const std::string_view prefix =
      current_line.substr(0, std::min(caret_column, current_line.size()));

  // Check if inside ${...}
  const std::size_t last_var_open = prefix.rfind("${");
  const std::size_t last_var_close = prefix.rfind('}');
  if (last_var_open != std::string_view::npos &&
      (last_var_close == std::string_view::npos ||
       last_var_close < last_var_open)) {
    // Inside a variable expansion: prioritize variables
    std::vector<Protocol::CompletionItem> var_items;
    for (const auto &item : m_all_completions) {
      if (item.kind == Protocol::CompletionItemKind::Variable ||
          item.kind == Protocol::CompletionItemKind::Property) {
        var_items.push_back(item);
      }
    }
    return var_items;
  }

  return m_all_completions;
}

std::optional<Protocol::Hover>
CMakeLanguageDatabase::find_hover(std::string_view symbol_name) const {
  if (symbol_name.empty()) {
    return std::nullopt;
  }

  std::string clean_symbol(symbol_name);
  if (clean_symbol.starts_with("${") && clean_symbol.ends_with("}")) {
    clean_symbol = clean_symbol.substr(2, clean_symbol.size() - 3);
  }

  std::string clean_lower = clean_symbol;
  for (char &c : clean_lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  for (const auto &item : m_all_completions) {
    std::string item_lower = item.label;
    for (char &c : item_lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (item.label == clean_symbol || item_lower == clean_lower) {
      std::string md;
      if (!item.detail.empty()) {
        md += "```cmake\n" + item.detail + "\n```\n\n";
      }
      if (!item.documentation.empty()) {
        md += item.documentation;
      } else {
        md += "CMake identifier: `" + item.label + "`";
      }

      return Protocol::Hover{.contents = std::move(md), .range = std::nullopt};
    }
  }

  return std::nullopt;
}

std::optional<Protocol::SignatureHelp>
CMakeLanguageDatabase::find_signature_help(
    std::string_view command_name) const {
  if (command_name.empty()) {
    return std::nullopt;
  }

  std::string clean_cmd(command_name);
  while (!clean_cmd.empty() &&
         std::isspace(static_cast<unsigned char>(clean_cmd.front())))
    clean_cmd.erase(clean_cmd.begin());
  while (!clean_cmd.empty() &&
         std::isspace(static_cast<unsigned char>(clean_cmd.back())))
    clean_cmd.pop_back();

  std::string clean_lower = clean_cmd;
  for (char &c : clean_lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  for (const auto &item : m_all_completions) {
    if (item.kind != Protocol::CompletionItemKind::Function)
      continue;

    std::string item_lower = item.label;
    for (char &c : item_lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (item.label == clean_cmd || item_lower == clean_lower) {
      Protocol::SignatureInformation sig_info{
          .label = item.detail.empty() ? (item.label + "(...)") : item.detail,
          .documentation = item.documentation,
          .parameters = {},
          .active_parameter = 0};

      return Protocol::SignatureHelp{.signatures = {std::move(sig_info)},
                                     .active_signature = 0,
                                     .active_parameter = 0};
    }
  }

  return std::nullopt;
}

} // namespace Zenvra::Language::CMake
