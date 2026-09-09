#include <gtest/gtest.h>
#include <fstream>

#include "Language/CMake/CMakeLanguageDatabase.h"
#include "Language/Definition/SymbolDefinitionResolver.h"
#include "Language/LanguageServerManager.h"
#include "Language/Protocol/LspProtocolSerializer.h"
#include "Language/Protocol/LspTypes.h"
#include "Language/Registry/ServerRegistry.h"
#include "Language/Syntax/GenericGrammarEngine.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/Syntax/SemanticTokensManager.h"
#include "Language/Toolchain/ToolchainDetector.h"
#include "Platform/HostSystem.h"
#include "Tools/Builder/CMakeBuilder.h"
#include "Tools/Runner/ProcessRunner.h"
#include "UI/Components/CompletionPopup.h"
#include "UI/Components/EditorFolding.h"
#include "UI/Components/HoverTooltip.h"
#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/FileIconModel.h"
#include "UI/Editor/StudioEditorModel.h"
#include "UI/Editor/TextDocumentModel.h"
#include "UI/Toolbar/StudioMainToolbar.h"

using namespace Zenvra;

class LanguageServerTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    Language::Registry::ServerRegistry::instance().initialize_default_profiles();
  }
};

static ::testing::Environment* const g_lang_env =
    ::testing::AddGlobalTestEnvironment(new LanguageServerTestEnvironment);

TEST(LanguageServerTests, LspProtocolFramingAndSerialization) {
  Language::Protocol::JsonRpcRequest req{
      .id = 42,
      .method = "textDocument/completion",
      .params = {{"textDocument", {{"uri", "file:///test.cpp"}}}}};

  const std::string framed =
      Language::Protocol::LspProtocolSerializer::serialize_request(req);
  EXPECT_TRUE(framed.starts_with("Content-Length: "));
  EXPECT_TRUE(framed.find("\r\n\r\n") != std::string::npos);
  EXPECT_TRUE(framed.find("\"id\":42") != std::string::npos ||
              framed.find("\"id\": 42") != std::string::npos);
  EXPECT_TRUE(framed.find("textDocument/completion") != std::string::npos);
}

TEST(LanguageServerTests, SemanticTokensDeltaDecoding) {
  // Test LSP 5-tuple delta format:
  // Token 1: line 0, start col 5, length 4, type (Type), modifier 0
  // Token 2: line 0, delta start col 6 (abs col 11), length 3, type (Variable),
  // modifier 0 Token 3: delta line 2 (abs line 2), start col 4, length 8, type
  // (Function), modifier 0
  std::vector<uint32_t> raw_tokens = {
      0,
      5,
      4,
      static_cast<uint32_t>(Language::Syntax::SemanticTokenType::Type),
      0,
      0,
      6,
      3,
      static_cast<uint32_t>(Language::Syntax::SemanticTokenType::Variable),
      0,
      2,
      4,
      8,
      static_cast<uint32_t>(Language::Syntax::SemanticTokenType::Function),
      0};

  auto decoded =
      Language::Syntax::SemanticTokensManager::decode_lsp_tokens(raw_tokens);
  ASSERT_EQ(decoded.size(), 3u);

  EXPECT_EQ(decoded[0].line, 0u);
  EXPECT_EQ(decoded[0].start_column, 5u);
  EXPECT_EQ(decoded[0].length, 4u);
  EXPECT_EQ(decoded[0].type, Language::Syntax::SemanticTokenType::Type);

  EXPECT_EQ(decoded[1].line, 0u);
  EXPECT_EQ(decoded[1].start_column, 11u);
  EXPECT_EQ(decoded[1].length, 3u);
  EXPECT_EQ(decoded[1].type, Language::Syntax::SemanticTokenType::Variable);

  EXPECT_EQ(decoded[2].line, 2u);
  EXPECT_EQ(decoded[2].start_column, 4u);
  EXPECT_EQ(decoded[2].length, 8u);
  EXPECT_EQ(decoded[2].type, Language::Syntax::SemanticTokenType::Function);
}

TEST(LanguageServerTests, GenericGrammarEngineDataDrivenTokenization) {
  Language::Syntax::GrammarRule rust_rule;
  rust_rule.name = "Rust";
  rust_rule.extensions = {".rs"};
  rust_rule.line_comment = "//";
  rust_rule.keywords = {"fn", "let", "mut", "pub", "struct", "impl", "return"};
  rust_rule.types = {"i32", "u64", "String", "bool", "Vec"};
  rust_rule.operators = {"+", "-", "*", "/", "=", "->", "::"};

  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
      tokens{};
  std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
      "pub fn calculate(val: i32) -> String {", rust_rule, tokens);

  ASSERT_GT(count, 0u);

  // Verify fn is recognized as Keyword
  bool found_fn = false;
  bool found_i32 = false;
  bool found_string = false;

  for (std::size_t i = 0; i < count; ++i) {
    if (tokens[i].text == "fn" &&
        tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
      found_fn = true;
    if (tokens[i].text == "i32" &&
        tokens[i].kind == UI::Editor::EditorTokenKind::Type)
      found_i32 = true;
    if (tokens[i].text == "String" &&
        tokens[i].kind == UI::Editor::EditorTokenKind::Type)
      found_string = true;
  }

  EXPECT_TRUE(found_fn);
  EXPECT_TRUE(found_i32);
  EXPECT_TRUE(found_string);
}

TEST(LanguageServerTests, CMakeGrammarTokenization) {
  Language::Syntax::GrammarRule cmake_rule;
  cmake_rule.name = "CMake";
  cmake_rule.extensions = {".cmake", "cmakelists.txt"};
  cmake_rule.line_comment = "#";
  cmake_rule.keywords = {"add_executable", "target_link_libraries", "project"};
  cmake_rule.types = {"PUBLIC", "PRIVATE", "INTERFACE"};

  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
      tokens{};
  std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
      "target_link_libraries(MyTarget PRIVATE ZDECore)", cmake_rule, tokens);

  ASSERT_GT(count, 0u);

  bool found_cmd = false;
  bool found_private = false;

  for (std::size_t i = 0; i < count; ++i) {
    if (tokens[i].text == "target_link_libraries" &&
        tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
      found_cmd = true;
    if (tokens[i].text == "PRIVATE" &&
        tokens[i].kind == UI::Editor::EditorTokenKind::Type)
      found_private = true;
  }

  EXPECT_TRUE(found_cmd);
  EXPECT_TRUE(found_private);
}

TEST(LanguageServerTests, ServerRegistryProfileLookup) {
  auto &registry = Language::Registry::ServerRegistry::instance();

  const auto *cpp_prof = registry.find_profile_for_extension(".cpp");
  ASSERT_NE(cpp_prof, nullptr);
  EXPECT_EQ(cpp_prof->language_id, "cpp");
  EXPECT_EQ(cpp_prof->executable_name, "clangd");

  const auto *rs_prof = registry.find_profile_for_extension(".rs");
  ASSERT_NE(rs_prof, nullptr);
  EXPECT_EQ(rs_prof->language_id, "rust");
  EXPECT_EQ(rs_prof->executable_name, "rust-analyzer");

  const auto *py_prof = registry.find_profile_for_extension(".py");
  ASSERT_NE(py_prof, nullptr);
  EXPECT_EQ(py_prof->language_id, "python");

  const auto *go_prof = registry.find_profile_for_extension(".go");
  ASSERT_NE(go_prof, nullptr);
  EXPECT_EQ(go_prof->language_id, "go");

  const auto *tsx_prof = registry.find_profile_for_extension(".tsx");
  ASSERT_NE(tsx_prof, nullptr);
  EXPECT_EQ(tsx_prof->language_id, "typescript");
  EXPECT_EQ(tsx_prof->executable_name, "typescript-language-server");

  const auto *jsx_prof = registry.find_profile_for_extension(".jsx");
  ASSERT_NE(jsx_prof, nullptr);
  EXPECT_EQ(jsx_prof->language_id, "typescript");
  EXPECT_EQ(jsx_prof->executable_name, "typescript-language-server");
}

TEST(LanguageServerTests, SystemOrPluginBinaryClangdDiscovery) {
  auto &registry = Language::Registry::ServerRegistry::instance();
  const auto clangd_path = registry.find_executable_in_system("clangd");
  ASSERT_FALSE(clangd_path.empty());
  EXPECT_TRUE(clangd_path.string().find("clangd") != std::string::npos);
}

TEST(LanguageServerTests, PluginBinaryDiscoveryInPluginsDirectory) {
  auto &registry = Language::Registry::ServerRegistry::instance();
  std::error_code ec;
  const auto plugin_bin_dir = std::filesystem::current_path() / "plugins" / "lsp" / "typescript" / "bin";
  std::filesystem::create_directories(plugin_bin_dir, ec);
  const auto dummy_exe = plugin_bin_dir / "typescript-language-server.exe";
  {
    std::ofstream ofs(dummy_exe);
    ofs << "dummy";
  }
  registry.clear_cache();
  const auto tls_path = registry.find_executable_in_system("typescript-language-server");
  EXPECT_FALSE(tls_path.empty());
  EXPECT_TRUE(tls_path.string().find("typescript-language-server") != std::string::npos);
  std::filesystem::remove(dummy_exe, ec);
}

TEST(LanguageServerTests, ProjectSwitchingAndLspLifecycle) {
  auto &manager = Language::LanguageServerManager::instance();

  // 1. Set initial workspace (e.g. C++ project)
  const std::filesystem::path cpp_root = "C:/Projects/MyCppProject";
  manager.set_workspace_root(cpp_root);
  EXPECT_EQ(manager.get_workspace_root(), cpp_root);

  // 2. Switching to React JS project must update workspace root and trigger clean shutdown_all
  const std::filesystem::path react_root = "C:/Projects/MyReactApp";
  manager.set_workspace_root(react_root);
  EXPECT_EQ(manager.get_workspace_root(), react_root);

  // 3. Closing project (set_workspace_root({})) must reset root and shutdown all LSPs
  manager.set_workspace_root({});
  EXPECT_TRUE(manager.get_workspace_root().empty());
  manager.shutdown_all();
}

TEST(LanguageServerTests, CompletionPopupFuzzyFiltering) {
  UI::Components::CompletionPopup popup;

  std::vector<Language::Protocol::CompletionItem> items;
  items.push_back({.label = "vector",
                   .kind = Language::Protocol::CompletionItemKind::Class,
                   .detail = "class std::vector"});
  items.push_back({.label = "string",
                   .kind = Language::Protocol::CompletionItemKind::Class,
                   .detail = "class std::string"});
  items.push_back({.label = "version",
                   .kind = Language::Protocol::CompletionItemKind::Variable,
                   .detail = "int version"});

  popup.show(items, 100.0F, 100.0F);
  EXPECT_TRUE(popup.is_visible());
  EXPECT_EQ(popup.get_item_count(), 3u);

  popup.set_filter("vec");
  EXPECT_EQ(popup.get_item_count(), 1u);
  ASSERT_NE(popup.get_selected_item(), nullptr);
  EXPECT_EQ(popup.get_selected_item()->label, "vector");

  popup.hide();
  EXPECT_FALSE(popup.is_visible());
}

TEST(LanguageServerTests, CompletionPopupDynamicScrolling) {
  UI::Components::CompletionPopup popup;

  std::vector<Language::Protocol::CompletionItem> items;
  for (int i = 0; i < 20; ++i) {
    items.push_back({.label = "item_" + std::to_string(i),
                     .kind = Language::Protocol::CompletionItemKind::Variable,
                     .detail = "var"});
  }

  popup.show(items, 100.0F, 100.0F);
  EXPECT_EQ(popup.get_item_count(), 20u);
  EXPECT_EQ(popup.get_scroll_offset(), 0u);
  EXPECT_EQ(popup.get_selected_index(), 0u);

  // Navigate down past the visible window (default max visible is 8)
  for (int i = 0; i < 10; ++i) {
    popup.select_next();
  }
  EXPECT_EQ(popup.get_selected_index(), 10u);
  EXPECT_GE(popup.get_scroll_offset(), 3u); // Window scrolled down

  // Scroll using mouse wheel
  EXPECT_TRUE(popup.scroll(-2)); // Scroll down by 2 lines
  EXPECT_GE(popup.get_scroll_offset(), 5u);

  EXPECT_TRUE(popup.scroll(2)); // Scroll up by 2 lines
  EXPECT_LE(popup.get_scroll_offset(), 5u);

  // Hit test
  EXPECT_TRUE(popup.is_point_inside(120.0F, 120.0F));
  EXPECT_FALSE(popup.is_point_inside(800.0F, 800.0F));
}

TEST(LanguageServerTests, ToolchainDetectionAndStatus) {
  auto &detector = Language::Toolchain::ToolchainDetector::instance();
  const auto &tc = detector.get_active_toolchain();

  EXPECT_FALSE(tc.name.empty());
  const std::string label = detector.get_status_bar_label();
  EXPECT_FALSE(label.empty());

  const std::string guidance = detector.get_tooltip_guidance();
  EXPECT_FALSE(guidance.empty());
}

TEST(LanguageServerTests, CMakeLanguageDatabaseBuiltinCompletions) {
  const auto items =
      Language::CMake::CMakeLanguageDatabase::instance().get_all_completions();
  EXPECT_GT(items.size(), 50u);

  bool found_min_req = false;
  bool found_version = false;
  bool found_link_libs = false;
  bool found_public = false;

  for (const auto &item : items) {
    if (item.label == "cmake_minimum_required") {
      found_min_req = true;
      EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Function);
      EXPECT_FALSE(item.detail.empty());
      EXPECT_FALSE(item.documentation.empty());
    } else if (item.label == "VERSION") {
      found_version = true;
      EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Variable);
    } else if (item.label == "target_link_libraries") {
      found_link_libs = true;
      EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Function);
    } else if (item.label == "PUBLIC") {
      found_public = true;
      EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Keyword);
    }
  }

  EXPECT_TRUE(found_min_req);
  EXPECT_TRUE(found_version);
  EXPECT_TRUE(found_link_libs);
  EXPECT_TRUE(found_public);
}

TEST(LanguageServerTests, CMakeLanguageDatabaseHover) {
  auto hover_cmd =
      Language::CMake::CMakeLanguageDatabase::instance().find_hover(
          "cmake_minimum_required");
  ASSERT_TRUE(hover_cmd.has_value());
  EXPECT_TRUE(hover_cmd->contents.find("cmake_minimum_required") !=
              std::string::npos);

  auto hover_var =
      Language::CMake::CMakeLanguageDatabase::instance().find_hover(
          "${CMAKE_VERSION}");
  ASSERT_TRUE(hover_var.has_value());
  EXPECT_TRUE(hover_var->contents.find("CMAKE_VERSION") != std::string::npos);

  auto hover_kw =
      Language::CMake::CMakeLanguageDatabase::instance().find_hover("PUBLIC");
  ASSERT_TRUE(hover_kw.has_value());
  EXPECT_TRUE(hover_kw->contents.find("PUBLIC") != std::string::npos);
}

TEST(LanguageServerTests, CppSyntaxHighlightingNamespacesFunctionsClasses) {
  const auto *grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(
          ".cpp");
  ASSERT_NE(grammar, nullptr);

  // 1. Namespace declaration
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    const std::size_t count =
        Language::Syntax::GenericGrammarEngine::tokenize_line(
            "namespace Zenvra::Platform::X11::Components", *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_namespace_kw = false;
    bool found_zenvra_label = false;
    bool found_platform_label = false;
    bool found_x11_label = false;
    bool found_components_label = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "namespace" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
        found_namespace_kw = true;
      if (tokens[i].text == "Zenvra" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_zenvra_label = true;
      if (tokens[i].text == "Platform" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_platform_label = true;
      if (tokens[i].text == "X11" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_x11_label = true;
      if (tokens[i].text == "Components" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_components_label = true;
    }

    EXPECT_TRUE(found_namespace_kw);
    EXPECT_TRUE(found_zenvra_label);
    EXPECT_TRUE(found_platform_label);
    EXPECT_TRUE(found_x11_label);
    EXPECT_TRUE(found_components_label);
  }

  // 2. Member function definition with class qualifier
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    const std::size_t count =
        Language::Syntax::GenericGrammarEngine::tokenize_line(
            "void EditorScrollbar::reset() noexcept", *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_void_type = false;
    bool found_scrollbar_class = false;
    bool found_reset_func = false;
    bool found_noexcept_kw = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "void" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_void_type = true;
      if (tokens[i].text == "EditorScrollbar" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_scrollbar_class = true;
      if (tokens[i].text == "reset" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_reset_func = true;
      if (tokens[i].text == "noexcept" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
        found_noexcept_kw = true;
    }

    EXPECT_TRUE(found_void_type);
    EXPECT_TRUE(found_scrollbar_class);
    EXPECT_TRUE(found_reset_func);
    EXPECT_TRUE(found_noexcept_kw);
  }

  // 3. Class declaration
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    const std::size_t count =
        Language::Syntax::GenericGrammarEngine::tokenize_line(
            "class MyComponent : public BaseComponent", *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_class_kw = false;
    bool found_my_component = false;
    bool found_public_kw = false;
    bool found_base_component = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "class" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
        found_class_kw = true;
      if (tokens[i].text == "MyComponent" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_my_component = true;
      if (tokens[i].text == "public" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
        found_public_kw = true;
      if (tokens[i].text == "BaseComponent" &&
          (tokens[i].kind == UI::Editor::EditorTokenKind::Label ||
           tokens[i].kind == UI::Editor::EditorTokenKind::Type))
        found_base_component = true;
    }

    EXPECT_TRUE(found_class_kw);
    EXPECT_TRUE(found_my_component);
    EXPECT_TRUE(found_public_kw);
    EXPECT_TRUE(found_base_component);
  }

  // 4. Data types and std namespace
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    const std::size_t count =
        Language::Syntax::GenericGrammarEngine::tokenize_line(
            "const std::string text = \"Hello\";", *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_const_kw = false;
    bool found_std_type = false;
    bool found_string_type = false;
    bool found_text_plain = false;
    bool found_str_literal = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "const" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
        found_const_kw = true;
      if (tokens[i].text == "std" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_std_type = true;
      if (tokens[i].text == "string" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_string_type = true;
      if (tokens[i].text == "text" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_text_plain = true;
      if (tokens[i].text == "\"Hello\"" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::String)
        found_str_literal = true;
    }

    EXPECT_TRUE(found_const_kw);
    EXPECT_TRUE(found_std_type);
    EXPECT_TRUE(found_string_type);
    EXPECT_TRUE(found_text_plain);
    EXPECT_TRUE(found_str_literal);
  }

  // 5. STL template containers, fixed-width ints, and smart pointers
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    const std::size_t count =
        Language::Syntax::GenericGrammarEngine::tokenize_line(
            "std::vector<uint32_t> items = std::make_unique<uint32_t>(10);",
            *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_std_type = false;
    bool found_vector_type = false;
    bool found_uint32_type = false;
    bool found_make_unique_type = false;
    bool found_num = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "std" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_std_type = true;
      if (tokens[i].text == "vector" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_vector_type = true;
      if (tokens[i].text == "uint32_t" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_uint32_type = true;
      if (tokens[i].text == "make_unique" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_make_unique_type = true;
      if (tokens[i].text == "10" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Number)
        found_num = true;
    }

    EXPECT_TRUE(found_std_type);
    EXPECT_TRUE(found_vector_type);
    EXPECT_TRUE(found_uint32_type);
    EXPECT_TRUE(found_make_unique_type);
    EXPECT_TRUE(found_num);
  }
}

TEST(LanguageServerTests, GenericGrammarEngineRawStringLiteralsAndMultiline) {
  const auto *grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(
          ".cpp");
  ASSERT_NE(grammar, nullptr);

  // 1. Empty string literal ""
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    const std::size_t count =
        Language::Syntax::GenericGrammarEngine::tokenize_line(
            "const char* str = \"\";", *grammar, tokens);
    ASSERT_GT(count, 0u);
    bool found_empty_str = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "\"\"" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::String) {
        found_empty_str = true;
      }
    }
    EXPECT_TRUE(found_empty_str);
  }

  // 2. Single-line and Multi-line raw string literals with Document State
  {
    UI::Editor::TextDocumentModel doc;
    doc.replace_contents({
        "const char* text = R\"tag(",
        "    line one",
        "    line two",
        ")tag\";",
        "int value = 42;"
    }, "sample.cpp", {}, "LF");

    EXPECT_EQ(doc.get_line_state(0).kind,
              Language::Syntax::TokenizerState::StateKind::Normal);
    EXPECT_EQ(doc.get_line_state(1).kind,
              Language::Syntax::TokenizerState::StateKind::RawString);
    EXPECT_EQ(doc.get_line_state(2).kind,
              Language::Syntax::TokenizerState::StateKind::RawString);
    EXPECT_EQ(doc.get_line_state(3).kind,
              Language::Syntax::TokenizerState::StateKind::RawString);
    EXPECT_EQ(doc.get_line_state(4).kind,
              Language::Syntax::TokenizerState::StateKind::Normal);

    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
        tokens{};
    auto state = doc.get_line_state(1);
    const std::size_t count = UI::Editor::tokenize_editor_line(
        doc.get_line(1), tokens, doc.get_file_name(), &state);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::String);
    EXPECT_EQ(tokens[0].text, "    line one");

    auto state4 = doc.get_line_state(4);
    const std::size_t count4 = UI::Editor::tokenize_editor_line(
        doc.get_line(4), tokens, doc.get_file_name(), &state4);
    EXPECT_GT(count4, 0u);
    EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::Type);
    EXPECT_EQ(tokens[0].text, "int");
  }
}

TEST(LanguageServerTests, PlainTextNotesDoNotTriggerLspOrCppGrammar) {
  // 1. Plain text notes (.txt, .md, .log) must not use syntax highlighting
  EXPECT_FALSE(UI::Editor::supports_editor_syntax_highlighting("notes.txt"));
  EXPECT_FALSE(UI::Editor::supports_editor_syntax_highlighting("todo.md"));
  EXPECT_FALSE(UI::Editor::supports_editor_syntax_highlighting("build.log"));
  EXPECT_FALSE(UI::Editor::supports_editor_syntax_highlighting(""));
  EXPECT_TRUE(UI::Editor::supports_editor_syntax_highlighting("main.cpp"));
  EXPECT_TRUE(UI::Editor::supports_editor_syntax_highlighting("Shader.h"));

  // 2. Plain text notes must not return a language server profile (no clangd)
  EXPECT_EQ(Language::Registry::ServerRegistry::instance().find_profile_for_filename("notes.txt"), nullptr);
  EXPECT_EQ(Language::Registry::ServerRegistry::instance().find_profile_for_filename("todo.md"), nullptr);
  EXPECT_EQ(Language::Registry::ServerRegistry::instance().find_profile_for_filename("my_note.note"), nullptr);

  // 3. Document model on plain text files returns Plain tokens only
  UI::Editor::TextDocumentModel note_doc;
  note_doc.replace_contents({"This is a normal note with class and void keywords"}, "notes.txt", {}, "LF");
  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
  const std::size_t count = UI::Editor::tokenize_editor_line(
      note_doc.get_line(0), tokens, note_doc.get_file_name());
  ASSERT_EQ(count, 1u);
  EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::Plain);
}

TEST(LanguageServerTests, SemanticTokensColorMapping) {
  const auto palette = UI::Editor::StudioEditorPalette::dark();

  // Namespace, Class, Struct, Interface map to palette.type (Cyan)
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Namespace, palette),
            palette.type);
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Class, palette),
            palette.type);
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Struct, palette),
            palette.type);
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Interface, palette),
            palette.type);

  // Function, Method, Macro map to palette.label (Pink)
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Function, palette),
            palette.label);
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Method, palette),
            palette.label);
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Macro, palette),
            palette.macro_symbol);

  // Types map to palette.type (Cyan)
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Type, palette),
            palette.type);

  // Keywords map to palette.keyword (Peach)
  EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
                Language::Syntax::SemanticTokenType::Keyword, palette),
            palette.keyword);
}

TEST(LanguageServerTests, ToolbarRunConfigurationWidgetState) {
  UI::Toolbar::Widgets::RunConfigurationWidget widget;
  widget.set_active_target("ZDE");
  widget.set_active_mode(UI::Toolbar::BuildConfigurationMode::Debug);
  widget.set_active_architecture(UI::Toolbar::TargetArchitecture::Arm64);

  EXPECT_EQ(widget.get_state().active_target_name, "ZDE");
  EXPECT_EQ(widget.get_state().active_mode,
            UI::Toolbar::BuildConfigurationMode::Debug);
  EXPECT_EQ(widget.get_state().active_architecture,
            UI::Toolbar::TargetArchitecture::Arm64);
  EXPECT_EQ(widget.get_summary_label(), "ZDE | Debug | ARM64");

  widget.set_active_mode(UI::Toolbar::BuildConfigurationMode::Release);
  widget.set_active_architecture(UI::Toolbar::TargetArchitecture::X86_64);
  widget.set_active_target("ZDEUnitTests");

  EXPECT_EQ(widget.get_summary_label(), "ZDEUnitTests | Release | x86_64");
}

TEST(LanguageServerTests, ToolbarLayoutResponsiveComputation) {
  UI::Toolbar::StudioMainToolbar toolbar;
  toolbar.update_dpi_scale(1.0F);

  const auto layout = toolbar.layout(1280.0F, 0.0F);
  EXPECT_EQ(layout.toolbar_bounds.width, 1280.0F);
  EXPECT_GT(layout.left_section_bounds.width, 0.0F);
  EXPECT_GT(layout.center_section_bounds.width, 0.0F);
  EXPECT_GT(layout.right_section_bounds.width, 0.0F);

  EXPECT_GT(layout.run_button_bounds.width, 0.0F);
  EXPECT_GT(layout.debug_button_bounds.width, 0.0F);
  EXPECT_GT(layout.build_button_bounds.width, 0.0F);
  EXPECT_GT(layout.stop_button_bounds.width, 0.0F);
  EXPECT_GT(layout.target_combo_bounds.width, 0.0F);
}

TEST(LanguageServerTests, ToolsCMakeBuilderAndRunner) {
  Tools::Builder::CMakeBuilder builder;
  const auto targets = builder.discover_cmake_targets(".");
  EXPECT_GE(targets.size(), 2u);
  EXPECT_EQ(targets[0], "ZDE");

  Tools::Runner::ProcessRunner runner;
  Tools::Runner::ProcessExecutionOptions opts{.executable_path =
                                                  "non_existent_binary",
                                              .arguments = {},
                                              .working_directory = ".",
                                              .run_in_background = false};
  EXPECT_FALSE(runner.launch_process(opts));
}

TEST(LanguageServerTests, HostSystemArchitectureAndOSDetection) {
  const auto os = Platform::HostSystem::get_operating_system();
  EXPECT_NE(os, Platform::HostSystem::OperatingSystem::Unknown);

  const auto arch = Platform::HostSystem::get_native_architecture();
  EXPECT_NE(arch, Platform::HostSystem::Architecture::Unknown);

  const auto &info = Platform::HostSystem::get_system_info();
  EXPECT_FALSE(info.default_preset_debug.empty());
  EXPECT_FALSE(info.default_preset_release.empty());
}

TEST(LanguageServerTests, ActivityPanelModelFileFolderManipulationAndMove) {
  UI::Editor::ActivityPanelModel model;
  EXPECT_TRUE(model.initialize("."));

  const std::filesystem::path temp_dir =
      std::filesystem::current_path() / "build" / "temp_test_tree";
  std::filesystem::create_directories(temp_dir);

  UI::Editor::ActivityPanelModel test_model;
  EXPECT_TRUE(test_model.initialize(temp_dir));

  std::filesystem::path created_folder;
  EXPECT_TRUE(test_model.create_directory("subfolder", created_folder));
  EXPECT_TRUE(std::filesystem::is_directory(created_folder));

  test_model.set_selected_path(std::nullopt);
  std::filesystem::path created_file;
  EXPECT_TRUE(test_model.create_file("test.txt", created_file));
  EXPECT_TRUE(std::filesystem::exists(created_file));

  std::filesystem::path moved_path;
  EXPECT_TRUE(test_model.move_item(created_file, created_folder, moved_path));
  EXPECT_TRUE(std::filesystem::exists(moved_path));
  EXPECT_EQ(moved_path, created_folder / "test.txt");
  EXPECT_FALSE(std::filesystem::exists(created_file));

  EXPECT_TRUE(test_model.delete_item(created_folder));
  EXPECT_FALSE(std::filesystem::exists(created_folder));

  std::filesystem::remove_all(temp_dir);
}

TEST(LanguageServerTests, ActivityPanelModelScrollingAndClamping) {
  UI::Editor::ActivityPanelModel model;
  EXPECT_TRUE(model.initialize(std::filesystem::current_path()));
  EXPECT_TRUE(model.is_visible());
  EXPECT_TRUE(model.is_active(UI::Editor::SidebarIcon::Project));

  const auto items = model.get_project_items();
  if (items.size() > 5) {
    const std::size_t viewport_rows = 5;
    const std::size_t max_offset = items.size() - viewport_rows;

    EXPECT_EQ(model.get_scroll_offset(), 0u);
    EXPECT_FALSE(model.scroll(0, viewport_rows));

    // Scroll down
    EXPECT_TRUE(model.scroll(3, viewport_rows));
    EXPECT_EQ(model.get_scroll_offset(), std::min(max_offset, std::size_t{3}));

    // Scroll up
    EXPECT_TRUE(model.scroll(-2, viewport_rows));
    EXPECT_EQ(model.get_scroll_offset(), std::size_t{1});

    // Scroll way past maximum offset
    EXPECT_TRUE(model.scroll(10000, viewport_rows));
    EXPECT_EQ(model.get_scroll_offset(), max_offset);

    // Scroll way past top (0)
    EXPECT_TRUE(model.scroll(-10000, viewport_rows));
    EXPECT_EQ(model.get_scroll_offset(), 0u);
  }
}

TEST(LanguageServerTests, HoverTooltipStateAndBoundsCalculation) {
  UI::Components::HoverTooltip tooltip;
  EXPECT_FALSE(tooltip.is_visible());

  tooltip.show(
      "```cpp\nint calculate(int a, int b);\n```\nCalculates sum of a and b",
      150.0F, 200.0F);
  EXPECT_TRUE(tooltip.is_visible());
  EXPECT_EQ(tooltip.get_x(), 150.0F);
  EXPECT_EQ(tooltip.get_y(), 200.0F);
  EXPECT_FALSE(tooltip.get_content().empty());

  const auto bounds = tooltip.calculate_bounds(320.0F, 90.0F);
  EXPECT_EQ(bounds.x, 150.0F);
  EXPECT_EQ(bounds.y, 104.0F);
  EXPECT_EQ(bounds.width, 320.0F);
  EXPECT_EQ(bounds.height, 90.0F);

  tooltip.hide();
  EXPECT_FALSE(tooltip.is_visible());
}

TEST(LanguageServerTests, UriConversionAndPercentDecoding) {
  using Language::Protocol::LspProtocolSerializer;

#if !defined(_WIN32)
  // 1. Linux/Unix absolute path conversion
  const std::filesystem::path unix_path("/home/user/project/main.cpp");
  const std::string unix_uri = LspProtocolSerializer::path_to_uri(unix_path);
  EXPECT_EQ(unix_uri, "file:///home/user/project/main.cpp");
  const std::filesystem::path parsed_unix =
      LspProtocolSerializer::uri_to_path(unix_uri);
  EXPECT_EQ(parsed_unix, unix_path);
#endif

  // 2. Windows drive path conversion
  const std::filesystem::path win_path("C:/Users/dev/project/main.cpp");
  const std::string win_uri = LspProtocolSerializer::path_to_uri(win_path);
  EXPECT_EQ(win_uri, "file:///C:/Users/dev/project/main.cpp");
  const std::filesystem::path parsed_win =
      LspProtocolSerializer::uri_to_path(win_uri);
  EXPECT_EQ(parsed_win, win_path);

  // 3. Percent-encoded spaces and symbols
  const std::filesystem::path space_path = LspProtocolSerializer::uri_to_path(
      "file:///home/user/my%20cool%20app/test.cpp");
  EXPECT_EQ(space_path,
            std::filesystem::path("/home/user/my cool app/test.cpp"));

  // 4. Empty / Untitled fallback
  EXPECT_EQ(LspProtocolSerializer::path_to_uri({}), "file:///untitled.cpp");
  EXPECT_TRUE(LspProtocolSerializer::uri_to_path("").empty());
}

TEST(LanguageServerTests, ToolchainDetectionIncludesAndHeaders) {
  auto &detector = Language::Toolchain::ToolchainDetector::instance();
  detector.refresh();
  const auto &toolchain = detector.get_active_toolchain();

  // Verify toolchain detection on any development system
  EXPECT_TRUE(detector.has_valid_sdk());
  if (toolchain.kind != Language::Toolchain::ToolchainKind::None) {
    EXPECT_FALSE(toolchain.compiler_path.empty());
    EXPECT_TRUE(toolchain.status ==
                Language::Toolchain::ToolchainStatus::Ready);
    EXPECT_FALSE(toolchain.name.empty());
#if !defined(_WIN32)
    // On Linux / macOS systems with build tools, system include paths must be
    // populated
    if (std::filesystem::exists("/usr/include") ||
        std::filesystem::exists("/usr/include/c++")) {
      EXPECT_TRUE(toolchain.has_standard_headers);
      EXPECT_FALSE(toolchain.system_include_paths.empty());
    }
#endif
  }
}

TEST(LanguageServerTests, AssemblyGrammarProfileAndTemplates) {
  // 1. Verify ServerRegistry resolves .asm, .s, .S, .nasm, .inc to asm profile
  const auto *asm_profile =
      Language::Registry::ServerRegistry::instance().find_profile_for_filename(
          "main.asm");
  ASSERT_NE(asm_profile, nullptr);
  EXPECT_EQ(asm_profile->language_id, "asm");
  EXPECT_EQ(asm_profile->executable_name, "asm-lsp");

  const auto *s_profile =
      Language::Registry::ServerRegistry::instance().find_profile_for_filename(
          "kernel.s");
  ASSERT_NE(s_profile, nullptr);
  EXPECT_EQ(s_profile->language_id, "asm");

  // 2. Verify GrammarRegistry retrieves Assembly grammar
  const auto *asm_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename(
          "code.asm");
  ASSERT_NE(asm_grammar, nullptr);
  EXPECT_EQ(asm_grammar->name, "Assembly");
  EXPECT_TRUE(asm_grammar->is_keyword("mov"));
  EXPECT_TRUE(asm_grammar->is_keyword("MOV")); // case-insensitive check
  EXPECT_TRUE(asm_grammar->is_keyword("syscall"));
  EXPECT_TRUE(asm_grammar->is_keyword("svc"));
  EXPECT_TRUE(asm_grammar->is_type("rax"));
  EXPECT_TRUE(asm_grammar->is_type("RAX"));
  EXPECT_TRUE(asm_grammar->is_type("x0"));
  EXPECT_TRUE(asm_grammar->is_type("X0"));

  // 3. Verify GenericGrammarEngine tokenization for Assembly
  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
  Language::Syntax::TokenizerState state{};
  const std::string_view asm_line = "    mov rax, 1 ; sys_write";
  const std::size_t token_count =
      Language::Syntax::GenericGrammarEngine::tokenize_line(asm_line, *asm_grammar,
                                                            tokens, state);
  EXPECT_GT(token_count, 0);

  // 4. Verify templates for Assembly exist and cover x86 (16/32/64-bit) & ARM (32/64-bit)
  const auto templates =
      Language::LanguageServerManager::get_templates_for_filename("main.asm");
  EXPECT_FALSE(templates.empty());
  EXPECT_GE(templates.size(), 8);

  bool has_x86_64 = false;
  bool has_x86_32 = false;
  bool has_x86_16 = false;
  bool has_arm64 = false;
  bool has_arm32 = false;
  bool has_win32_forbidden = false;

  for (const auto &item : templates) {
    std::string lower_label = item.label;
    std::transform(lower_label.begin(), lower_label.end(), lower_label.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string lower_doc = item.documentation;
    std::transform(lower_doc.begin(), lower_doc.end(), lower_doc.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower_label.find("x86_64") != std::string::npos) has_x86_64 = true;
    if (lower_label.find("x86_32") != std::string::npos) has_x86_32 = true;
    if (lower_label.find("x86_16") != std::string::npos) has_x86_16 = true;
    if (lower_label.find("arm64") != std::string::npos) has_arm64 = true;
    if (lower_label.find("arm32") != std::string::npos) has_arm32 = true;

    // Strict validation: must NOT contain win32
    if (lower_label.find("win32") != std::string::npos ||
        lower_doc.find("win32") != std::string::npos) {
      has_win32_forbidden = true;
    }
  }

  EXPECT_TRUE(has_x86_64);
  EXPECT_TRUE(has_x86_32);
  EXPECT_TRUE(has_x86_16);
  EXPECT_TRUE(has_arm64);
  EXPECT_TRUE(has_arm32);
  EXPECT_FALSE(has_win32_forbidden);

  // 5. Verify FileIconModel icon mapping for assembly files
  const std::string asm_icon =
      UI::Editor::file_icon_asset_for_path(std::filesystem::path("program.asm"));
  EXPECT_EQ(asm_icon, "material-icon-theme/assembly.svg");

  const std::string s_icon =
      UI::Editor::file_icon_asset_for_path(std::filesystem::path("driver.s"));
  EXPECT_EQ(s_icon, "material-icon-theme/assembly.svg");
}

TEST(LanguageServerTests, LocationAndLocationLinkParsing) {
  // 1. Standard Location
  nlohmann::json loc_json = {
      {"uri", "file:///c:/project/main.cpp"},
      {"range", {{"start", {{"line", 10}, {"character", 5}}}, {"end", {{"line", 10}, {"character", 15}}}}}
  };
  auto loc = Language::Protocol::LspProtocolSerializer::parse_location(loc_json);
  EXPECT_EQ(loc.uri, "file:///c:/project/main.cpp");
  EXPECT_EQ(loc.range.start.line, 10);
  EXPECT_EQ(loc.range.start.character, 5);

  // 2. LSP 3.14+ LocationLink
  nlohmann::json link_json = {
      {"targetUri", "file:///usr/include/c++/11/string"},
      {"targetSelectionRange", {{"start", {{"line", 102}, {"character", 8}}}, {"end", {{"line", 102}, {"character", 20}}}}},
      {"targetRange", {{"start", {{"line", 100}, {"character", 0}}}, {"end", {{"line", 200}, {"character", 0}}}}}
  };
  auto link_loc = Language::Protocol::LspProtocolSerializer::parse_location(link_json);
  EXPECT_EQ(link_loc.uri, "file:///usr/include/c++/11/string");
  EXPECT_EQ(link_loc.range.start.line, 102);
  EXPECT_EQ(link_loc.range.start.character, 8);

  // 3. Array of LocationLinks
  nlohmann::json list_json = nlohmann::json::array({link_json, loc_json});
  auto locations = Language::Protocol::LspProtocolSerializer::parse_locations(list_json);
  ASSERT_EQ(locations.size(), 2);
  EXPECT_EQ(locations[0].uri, "file:///usr/include/c++/11/string");
  EXPECT_EQ(locations[1].uri, "file:///c:/project/main.cpp");
}

TEST(LanguageServerTests, SymbolExtractionAndRange) {
  using Language::Definition::SymbolDefinitionResolver;

  // C++ Standard Library Symbol
  std::string_view line1 = "    std::string text = \"hello\";";
  EXPECT_EQ(SymbolDefinitionResolver::extract_symbol_at(line1, 10), "std::string");
  auto [s1, e1] = SymbolDefinitionResolver::extract_symbol_range(line1, 10);
  EXPECT_EQ(line1.substr(s1, e1 - s1), "string");

  // Include directive
  std::string_view line2 = "#include <vector>";
  EXPECT_EQ(SymbolDefinitionResolver::extract_symbol_at(line2, 12), "vector");
  auto [s2, e2] = SymbolDefinitionResolver::extract_symbol_range(line2, 12);
  EXPECT_EQ(line2.substr(s2, e2 - s2), "vector");

  // Include directive with full path
  std::string_view line_inc = "#include \"Platform/Win32/Components/StudioWorkspaceRenderer.h\"";
  auto [s_inc, e_inc] = SymbolDefinitionResolver::extract_symbol_range(line_inc, 25);
  EXPECT_EQ(line_inc.substr(s_inc, e_inc - s_inc), "Platform/Win32/Components/StudioWorkspaceRenderer.h");

  // Variable assignment
  std::string_view line_var = "    m_model = UI::Editor::EditorScrollModel{};";
  auto [s_var, e_var] = SymbolDefinitionResolver::extract_symbol_range(line_var, 6);
  EXPECT_EQ(line_var.substr(s_var, e_var - s_var), "m_model");

  // Multi-language custom symbol
  std::string_view line3 = "def calculate_matrix_norm(matrix):";
  EXPECT_EQ(SymbolDefinitionResolver::extract_symbol_at(line3, 8), "calculate_matrix_norm");
  auto [s3, e3] = SymbolDefinitionResolver::extract_symbol_range(line3, 8);
  EXPECT_EQ(line3.substr(s3, e3 - s3), "calculate_matrix_norm");
}

TEST(LanguageServerTests, SymbolDefinitionResolverCppStdHeaders) {
  using Language::Definition::SymbolDefinitionResolver;

  std::string_view line = "std::string msg = \"test\";";
  Language::Protocol::Position pos{.line = 0, .character = 7};

  auto results = SymbolDefinitionResolver::instance().resolve_definition(
      "file:///test.cpp", "test.cpp", pos, line, std::filesystem::current_path());

  if (!results.empty()) {
    std::filesystem::path resolved_path =
        Language::Protocol::LspProtocolSerializer::uri_to_path(results[0].uri);
    std::string filename = resolved_path.filename().string();
    EXPECT_TRUE(filename == "string" || filename == "xstring" ||
                filename == "basic_string.h" || filename == "string.h");
  }
}

TEST(LanguageServerTests, SymbolDefinitionResolverWorkspaceAndOpenDocs) {
  using Language::Definition::SymbolDefinitionResolver;
  using Language::Definition::DocumentContext;

  DocumentContext doc;
  doc.uri = "file:///c:/project/MyClass.h";
  doc.filename = "c:/project/MyClass.h";
  doc.lines = {
      "#pragma once",
      "",
      "class MyCustomEngine {",
      "public:",
      "    void initialize();",
      "};"
  };

  std::string_view line = "    MyCustomEngine engine;";
  Language::Protocol::Position pos{.line = 0, .character = 7};

  auto locs = SymbolDefinitionResolver::instance().resolve_definition(
      "file:///c:/project/main.cpp", "c:/project/main.cpp", pos, line,
      "c:/project", {doc});

  ASSERT_FALSE(locs.empty());
  EXPECT_EQ(locs[0].uri, "file:///c:/project/MyClass.h");
  EXPECT_EQ(locs[0].range.start.line, 2); // line 2: class MyCustomEngine
}

TEST(LanguageServerTests, ExtensionlessStlHeaderGrammarAndLspRecognition) {
  // Verify that extensionless C++ STL headers get full C/C++ grammar syntax highlighting
  const auto* string_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename("string");
  ASSERT_NE(string_grammar, nullptr);
  EXPECT_EQ(string_grammar->name, "C/C++");

  const auto* vector_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename("vector");
  ASSERT_NE(vector_grammar, nullptr);
  EXPECT_EQ(vector_grammar->name, "C/C++");

  const auto* iostream_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename("iostream");
  ASSERT_NE(iostream_grammar, nullptr);
  EXPECT_EQ(iostream_grammar->name, "C/C++");

  const auto* xstring_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename("xstring");
  ASSERT_NE(xstring_grammar, nullptr);
  EXPECT_EQ(xstring_grammar->name, "C/C++");

  // Verify LSP profile lookup
  const auto* cpp_profile =
      Language::Registry::ServerRegistry::instance().find_profile_for_filename("string");
  ASSERT_NE(cpp_profile, nullptr);
  EXPECT_EQ(cpp_profile->language_id, "cpp");

  // Verify file icon
  const std::string icon =
      UI::Editor::file_icon_asset_for_path(std::filesystem::path("string"));
  EXPECT_EQ(icon, "vscode-symbols/files/cplus.svg");
}

TEST(LanguageServerTests, DimUnusedDiagnosticsAndColors) {
  // Test Diagnostic::is_unnecessary()
  Language::Protocol::Diagnostic diag_tag;
  diag_tag.tags = {Language::Protocol::DiagnosticTag::Unnecessary};
  EXPECT_TRUE(diag_tag.is_unnecessary());

  Language::Protocol::Diagnostic diag_code;
  diag_code.code = "unused-includes";
  EXPECT_TRUE(diag_code.is_unnecessary());

  Language::Protocol::Diagnostic diag_msg;
  diag_msg.message = "Included header <iostream> is not used directly";
  EXPECT_TRUE(diag_msg.is_unnecessary());

  Language::Protocol::Diagnostic diag_error;
  diag_error.message = "syntax error: unexpected token";
  EXPECT_FALSE(diag_error.is_unnecessary());

  // Test UI::Theme::dim_color blending
  UI::Theme::Color fg{255, 255, 255, 255};
  UI::Theme::Color bg{0, 0, 0, 255};
  UI::Theme::Color dimmed = UI::Theme::dim_color(fg, bg, 0.45F);
  EXPECT_GT(dimmed.red, 0);
  EXPECT_LT(dimmed.red, 255);
  EXPECT_EQ(dimmed.red, dimmed.green);
  EXPECT_EQ(dimmed.green, dimmed.blue);
}

TEST(LanguageServerTests, SyntaxHighlightingDistinguishesClassesVariablesAndDefines) {
  const auto* grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".cpp");
  ASSERT_NE(grammar, nullptr);

  // 1. Check classes vs variables vs function calls
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "const StudioWorkspaceRenderer& surface, Drawable drawable",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_renderer_type = false;
    bool found_surface_var = false;
    bool found_drawable_type = false;
    bool found_drawable_var = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "StudioWorkspaceRenderer" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_renderer_type = true;
      if (tokens[i].text == "surface" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_surface_var = true;
      if (tokens[i].text == "Drawable" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_drawable_type = true;
      if (tokens[i].text == "drawable" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_drawable_var = true;
    }

    EXPECT_TRUE(found_renderer_type);
    EXPECT_TRUE(found_surface_var);
    EXPECT_TRUE(found_drawable_type);
    EXPECT_TRUE(found_drawable_var);
  }

  // 2. Check variable declaration and function call
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "const int center_x = round_to_int(layout.activity_bar_bounds.x);",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_int_type = false;
    bool found_center_x_var = false;
    bool found_round_func = false;
    bool found_layout_var = false;
    bool found_bounds_var = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "int" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_int_type = true;
      if (tokens[i].text == "center_x" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_center_x_var = true;
      if (tokens[i].text == "round_to_int" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_round_func = true;
      if (tokens[i].text == "layout" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_layout_var = true;
      if (tokens[i].text == "activity_bar_bounds" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_bounds_var = true;
    }

    EXPECT_TRUE(found_int_type);
    EXPECT_TRUE(found_center_x_var);
    EXPECT_TRUE(found_round_func);
    EXPECT_TRUE(found_layout_var);
    EXPECT_TRUE(found_bounds_var);
  }

  // 3. Check preprocessor #define definition and #include
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "#define MAX_BUFFER_SIZE 4096",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_define_kw = false;
    bool found_macro_label = false;
    bool found_num = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "#define" &&
          (tokens[i].kind == UI::Editor::EditorTokenKind::Directive ||
           tokens[i].kind == UI::Editor::EditorTokenKind::Keyword))
        found_define_kw = true;
      if (tokens[i].text == "MAX_BUFFER_SIZE" &&
          (tokens[i].kind == UI::Editor::EditorTokenKind::Macro ||
           tokens[i].kind == UI::Editor::EditorTokenKind::Label))
        found_macro_label = true;
      if (tokens[i].text == "4096" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Number)
        found_num = true;
    }

    EXPECT_TRUE(found_define_kw);
    EXPECT_TRUE(found_macro_label);
    EXPECT_TRUE(found_num);
  }

  // 4. Check #include <filesystem>
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "#include <filesystem>",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_include_kw = false;
    bool found_header_str = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "#include" &&
          (tokens[i].kind == UI::Editor::EditorTokenKind::Directive ||
           tokens[i].kind == UI::Editor::EditorTokenKind::Keyword))
        found_include_kw = true;
      if (tokens[i].text == "<filesystem>" &&
          (tokens[i].kind == UI::Editor::EditorTokenKind::IncludeHeader ||
           tokens[i].kind == UI::Editor::EditorTokenKind::String))
        found_header_str = true;
    }

    EXPECT_TRUE(found_include_kw);
    EXPECT_TRUE(found_header_str);
  }

  // 5. Check #if defined(_WIN32) directive and macro tokenization
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "#if defined(_WIN32)",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_if = false;
    bool found_defined = false;
    bool found_macro = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "#if" && tokens[i].kind == UI::Editor::EditorTokenKind::Directive)
        found_if = true;
      if (tokens[i].text == "defined" && tokens[i].kind == UI::Editor::EditorTokenKind::Directive)
        found_defined = true;
      if (tokens[i].text == "_WIN32" && tokens[i].kind == UI::Editor::EditorTokenKind::Macro)
        found_macro = true;
    }

    EXPECT_TRUE(found_if);
    EXPECT_TRUE(found_defined);
    EXPECT_TRUE(found_macro);
  }

  // 6. Check C++ compound types and Logo.h
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "unsigned char Assets_icons_zenvra_logo_build_ico[] = {",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_unsigned = false;
    bool found_char = false;
    bool found_array_var = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "unsigned" && tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_unsigned = true;
      if (tokens[i].text == "char" && tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_char = true;
      if (tokens[i].text == "Assets_icons_zenvra_logo_build_ico" && tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_array_var = true;
    }

    EXPECT_TRUE(found_unsigned);
    EXPECT_TRUE(found_char);
    EXPECT_TRUE(found_array_var);
  }

  // 7. Check C++ standard attributes [[nodiscard]] and [[deprecated("reason")]]
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "[[nodiscard]] int calculate();",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_open_attr = false;
    bool found_nodiscard = false;
    bool found_close_attr = false;
    bool found_int_type = false;
    bool found_calc_label = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "[[" && tokens[i].kind == UI::Editor::EditorTokenKind::Directive)
        found_open_attr = true;
      if (tokens[i].text == "nodiscard" && tokens[i].kind == UI::Editor::EditorTokenKind::Directive)
        found_nodiscard = true;
      if (tokens[i].text == "]]" && tokens[i].kind == UI::Editor::EditorTokenKind::Directive)
        found_close_attr = true;
      if (tokens[i].text == "int" && tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_int_type = true;
      if (tokens[i].text == "calculate" && tokens[i].kind == UI::Editor::EditorTokenKind::Label)
        found_calc_label = true;
    }

    EXPECT_TRUE(found_open_attr);
    EXPECT_TRUE(found_nodiscard);
    EXPECT_TRUE(found_close_attr);
    EXPECT_TRUE(found_int_type);
    EXPECT_TRUE(found_calc_label);
  }

  // 8. Check C++ attribute with argument string: [[deprecated("use new_calc")]]
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "[[deprecated(\"use new_calc\")]] void old_func();",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_dep = false;
    bool found_msg = false;
    bool found_void = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "deprecated" && tokens[i].kind == UI::Editor::EditorTokenKind::Directive)
        found_dep = true;
      if (tokens[i].text == "\"use new_calc\"" && tokens[i].kind == UI::Editor::EditorTokenKind::String)
        found_msg = true;
      if (tokens[i].text == "void" && tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_void = true;
    }

    EXPECT_TRUE(found_dep);
    EXPECT_TRUE(found_msg);
    EXPECT_TRUE(found_void);
  }

  // 9. Check member variable access: player.Health and player->Location are Plain, not Type
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "player.Health = 100; mesh->Location = loc;",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_health_var = false;
    bool found_location_var = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "Health" && tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_health_var = true;
      if (tokens[i].text == "Location" && tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_location_var = true;
    }

    EXPECT_TRUE(found_health_var);
    EXPECT_TRUE(found_location_var);
  }

  // 10. Check C++14 digit separator: 1'000'000 is single Number token
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "int count = 1'000'000;",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_num = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "1'000'000" && tokens[i].kind == UI::Editor::EditorTokenKind::Number)
        found_num = true;
    }
    EXPECT_TRUE(found_num);
  }

  // 11. Check compiler attribute and Unreal Engine macro specifier
  {
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "UPROPERTY(EditAnywhere) FVector PlayerLocation;",
        *grammar, tokens);
    ASSERT_GT(count, 0u);

    bool found_uprop = false;
    bool found_fvector = false;
    bool found_location = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "UPROPERTY" && tokens[i].kind == UI::Editor::EditorTokenKind::Directive)
        found_uprop = true;
      if (tokens[i].text == "FVector" && tokens[i].kind == UI::Editor::EditorTokenKind::Type)
        found_fvector = true;
      if (tokens[i].text == "PlayerLocation" && tokens[i].kind == UI::Editor::EditorTokenKind::Plain)
        found_location = true;
    }

    EXPECT_TRUE(found_uprop);
    EXPECT_TRUE(found_fvector);
    EXPECT_TRUE(found_location);
  }
}

TEST(LanguageServerTests, PreprocessorInactiveBranchEvaluation) {
  UI::Editor::TextDocumentModel doc;
  doc.replace_contents({
      "#if defined(_WIN32)",
      "#define ACTIVE_MACRO 1",
      "#else",
      "#include <unistd.h>",
      "#endif"
  }, "main.cpp", {}, "LF");

#if defined(_WIN32)
  EXPECT_FALSE(doc.is_line_inactive(0)); // #if defined(_WIN32)
  EXPECT_FALSE(doc.is_line_inactive(1)); // #define ACTIVE_MACRO 1
  EXPECT_FALSE(doc.is_line_inactive(2)); // #else (directive itself is not dimmed)
  EXPECT_TRUE(doc.is_line_inactive(3));  // #include <unistd.h> (inactive block statement is dimmed)
  EXPECT_FALSE(doc.is_line_inactive(4)); // #endif (directive itself is not dimmed)
#endif
}

TEST(LanguageServerTests, BreakpointManagementInTextDocumentModel) {
  UI::Editor::TextDocumentModel doc;
  doc.replace_contents(
      {"int main() {", "    int x = 10;", "    return 0;", "}"},
      "main.cpp", {}, "LF");

  EXPECT_FALSE(doc.has_breakpoint(0));
  EXPECT_FALSE(doc.has_breakpoint(1));
  EXPECT_TRUE(doc.get_breakpoints().empty());

  // Toggle breakpoint on line 1
  bool added = doc.toggle_breakpoint(1);
  EXPECT_TRUE(added);
  EXPECT_TRUE(doc.has_breakpoint(1));
  EXPECT_EQ(doc.get_breakpoints().size(), 1u);

  // Toggle breakpoint on line 2
  added = doc.toggle_breakpoint(2);
  EXPECT_TRUE(added);
  EXPECT_TRUE(doc.has_breakpoint(2));
  EXPECT_EQ(doc.get_breakpoints().size(), 2u);

  // Toggle off breakpoint on line 1
  bool removed = doc.toggle_breakpoint(1);
  EXPECT_FALSE(removed);
  EXPECT_FALSE(doc.has_breakpoint(1));
  EXPECT_TRUE(doc.has_breakpoint(2));
  EXPECT_EQ(doc.get_breakpoints().size(), 1u);

  // Clear all breakpoints
  doc.clear_all_breakpoints();
  EXPECT_FALSE(doc.has_breakpoint(2));
  EXPECT_TRUE(doc.get_breakpoints().empty());
}

TEST(LanguageServerTests, DynamicGutterWidthProtectsBreakpointLane) {
  // 1 to 3 digits (e.g. 50 lines)
  const float w_small = Zenvra::UI::Editor::StudioEditorMetrics::calculate_gutter_width(50, 1.0F);
  EXPECT_GE(w_small, 66.0F);

  // 4 digits (e.g. 1500 lines)
  const float w_4digit = Zenvra::UI::Editor::StudioEditorMetrics::calculate_gutter_width(1500, 1.0F);
  EXPECT_GT(w_4digit, w_small);

  // 5 digits (e.g. 10656 lines like user's file)
  const float w_5digit = Zenvra::UI::Editor::StudioEditorMetrics::calculate_gutter_width(10656, 1.0F);
  EXPECT_GT(w_5digit, w_4digit);
  EXPECT_GE(w_5digit, 80.0F);

  // 6 digits (e.g. 100000 lines)
  const float w_6digit = Zenvra::UI::Editor::StudioEditorMetrics::calculate_gutter_width(100000, 1.0F);
  EXPECT_GT(w_6digit, w_5digit);
}

TEST(LanguageServerTests, LargeDocumentFoldingAndNavigationPerformance100k) {
  // Create a 100,000 line document
  std::vector<std::string> lines;
  lines.reserve(100000);
  for (std::size_t i = 0; i < 100000; ++i) {
    if (i % 100 == 0) {
      lines.push_back("void func_" + std::to_string(i) + "() {");
    } else if (i % 100 == 50) {
      lines.push_back("}");
    } else if (i % 10 == 0) {
      lines.push_back("    int val = " + std::to_string(i) + ";");
    } else {
      lines.push_back("");
    }
  }

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 4, 50000, 2500);

  EXPECT_FALSE(folding.get_ranges().empty());
  EXPECT_TRUE(folding.get_collapsed().empty());

  // Test active indent scope near line 50000
  const auto scope = folding.get_active_indent_scope(50010, 4);
  EXPECT_TRUE(scope.valid);
}

TEST(LanguageServerTests, LargeDocumentFoldingAndNavigationPerformance5M) {
  // Simulate a 5,200,000 line document structure with windowed rebuild
  std::vector<std::string> lines;
  lines.resize(5200000);
  lines[5196800] = "void massive_function() {";
  lines[5196810] = "    int val = 42;";
  lines[5196820] = "}";

  Zenvra::UI::Components::EditorFoldingModel folding;
  // Window centered at line 5196800 with radius 2500
  folding.rebuild(lines, 4, 5196800, 2500);

  EXPECT_FALSE(folding.get_ranges().empty());
  EXPECT_TRUE(folding.is_fold_start(5196800));
  EXPECT_FALSE(folding.is_fold_start(5196801));

  // Test indent scope at line 5196810
  const auto scope = folding.get_active_indent_scope(5196810, 4);
  EXPECT_TRUE(scope.valid);
  EXPECT_EQ(scope.start_line, 5196800u);
  EXPECT_EQ(scope.end_line, 5196820u);
}

TEST(LanguageServerTests, CurlyBraceFoldingBasicAndNested) {
  std::vector<std::string> lines = {
    "function main() {",             // 0
    "    if (true) {",               // 1
    "        console.log('hi');",    // 2
    "    }",                         // 3
    "    return 0;",                 // 4
    "}"                              // 5
  };

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 4);

  EXPECT_EQ(folding.get_ranges().size(), 2u);
  EXPECT_TRUE(folding.is_fold_start(0));
  EXPECT_TRUE(folding.is_fold_start(1));
  EXPECT_FALSE(folding.is_fold_start(2));

  // Verify markers
  EXPECT_EQ(folding.get_marker(0), Zenvra::UI::Components::FoldMarker::Expanded);
  EXPECT_EQ(folding.get_marker(1), Zenvra::UI::Components::FoldMarker::Expanded);
  EXPECT_EQ(folding.get_marker(2), Zenvra::UI::Components::FoldMarker::Continuation);
  EXPECT_EQ(folding.get_marker(3), Zenvra::UI::Components::FoldMarker::End); // inner end corner
  EXPECT_EQ(folding.get_marker(4), Zenvra::UI::Components::FoldMarker::Continuation);
  EXPECT_EQ(folding.get_marker(5), Zenvra::UI::Components::FoldMarker::End); // outer end corner
}

TEST(LanguageServerTests, CurlyBraceElseIfCatchFolding) {
  std::vector<std::string> lines = {
    "if (condition) {",             // 0
    "    stepOne();",               // 1
    "} else if (other) {",          // 2
    "    stepTwo();",               // 3
    "} else {",                     // 4
    "    stepThree();",             // 5
    "}"                             // 6
  };

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 4);

  EXPECT_GE(folding.get_ranges().size(), 3u);
  EXPECT_TRUE(folding.is_fold_start(0));
  EXPECT_TRUE(folding.is_fold_start(2));
  EXPECT_TRUE(folding.is_fold_start(4));
}

TEST(LanguageServerTests, TypeScriptReactTsxTwoSpaceFolding) {
  std::vector<std::string> lines = {
    "import { useState } from 'react'",    // 0
    "",                                    // 1
    "function App() {",                    // 2
    "  const [count, setCount] = useState(0);", // 3
    "  return (",                          // 4
    "    <div>",                           // 5
    "      <h1>Hello `{count}`</h1>",      // 6
    "    </div>",                          // 7
    "  );",                                // 8
    "}"                                    // 9
  };

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 2);

  EXPECT_TRUE(folding.is_fold_start(2));
  const auto* r = folding.get_range_at(2);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->start_line, 2u);
  EXPECT_EQ(r->end_line, 9u);

  // Active indent scope for line 3 with tab_size=2
  const auto scope = folding.get_active_indent_scope(3, 2);
  EXPECT_TRUE(scope.valid);
  EXPECT_EQ(scope.start_line, 2u);
  EXPECT_EQ(scope.end_line, 9u);
  EXPECT_EQ(scope.column, 0u);
}

TEST(LanguageServerTests, AllmanStyleBraceFolding) {
  std::vector<std::string> lines = {
    "void calculateResult()",       // 0
    "{",                            // 1
    "    int a = 10;",              // 2
    "}"                             // 3
  };

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 4);

  EXPECT_FALSE(folding.get_ranges().empty());
  // The fold start should attach to line 0 (the header)
  EXPECT_TRUE(folding.is_fold_start(0));
}

TEST(LanguageServerTests, PythonIndentationFoldingAndScopeBorders) {
  std::vector<std::string> lines = {
    "def calculate_total(items):",        // 0
    "    total = 0",                      // 1
    "    for item in items:",             // 2
    "        if item.is_valid:",          // 3
    "            total += item.price",    // 4
    "    return total",                   // 5
    "",                                   // 6
    "print('Finished')"                   // 7
  };

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 4);

  EXPECT_GE(folding.get_ranges().size(), 3u);
  EXPECT_TRUE(folding.is_fold_start(0));
  EXPECT_TRUE(folding.is_fold_start(2));
  EXPECT_TRUE(folding.is_fold_start(3));

  // Range checks
  const auto* root_range = folding.get_range_at(0);
  ASSERT_NE(root_range, nullptr);
  EXPECT_EQ(root_range->start_line, 0u);
  EXPECT_EQ(root_range->end_line, 5u);
  EXPECT_EQ(root_range->indent_level, 0u);

  // Markers
  EXPECT_EQ(folding.get_marker(0), Zenvra::UI::Components::FoldMarker::Expanded);
  EXPECT_EQ(folding.get_marker(1), Zenvra::UI::Components::FoldMarker::Continuation);
  EXPECT_EQ(folding.get_marker(2), Zenvra::UI::Components::FoldMarker::Expanded);
  EXPECT_EQ(folding.get_marker(4), Zenvra::UI::Components::FoldMarker::End);
  EXPECT_EQ(folding.get_marker(5), Zenvra::UI::Components::FoldMarker::End);

  // Active indent scope when caret is inside the deepest if-block (line 4)
  const auto scope = folding.get_active_indent_scope(4, 4);
  EXPECT_TRUE(scope.valid);
  EXPECT_EQ(scope.start_line, 3u);
  EXPECT_EQ(scope.end_line, 4u);
  EXPECT_EQ(scope.column, 8u);

  // Active indent scope when caret is in line 1
  const auto root_scope = folding.get_active_indent_scope(1, 4);
  EXPECT_TRUE(root_scope.valid);
  EXPECT_EQ(root_scope.start_line, 0u);
  EXPECT_EQ(root_scope.end_line, 5u);
  EXPECT_EQ(root_scope.column, 0u);
}

TEST(LanguageServerTests, YamlAndConfigIndentationFolding) {
  std::vector<std::string> lines = {
    "server:",                  // 0
    "  host: 0.0.0.0",          // 1
    "  port: 8080",             // 2
    "database:",                // 3
    "  url: postgres://db",     // 4
    "  pool:",                  // 5
    "    min: 2",               // 6
    "    max: 10"               // 7
  };

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 2);

  EXPECT_TRUE(folding.is_fold_start(0));
  EXPECT_TRUE(folding.is_fold_start(3));
  EXPECT_TRUE(folding.is_fold_start(5));

  const auto* pool_range = folding.get_range_at(5);
  ASSERT_NE(pool_range, nullptr);
  EXPECT_EQ(pool_range->start_line, 5u);
  EXPECT_EQ(pool_range->end_line, 7u);
  EXPECT_EQ(pool_range->indent_level, 2u);

  const auto pool_scope = folding.get_active_indent_scope(6, 2);
  EXPECT_TRUE(pool_scope.valid);
  EXPECT_EQ(pool_scope.start_line, 5u);
  EXPECT_EQ(pool_scope.end_line, 7u);
  EXPECT_EQ(pool_scope.column, 2u);
}

TEST(LanguageServerTests, ShellAndMakefileIndentationFolding) {
  std::vector<std::string> lines = {
    "all: build test",          // 0
    "    @echo 'Building...'",  // 1
    "    @gcc main.c -o app",   // 2
    "clean:",                   // 3
    "    @rm -f app"            // 4
  };

  Zenvra::UI::Components::EditorFoldingModel folding;
  folding.rebuild(lines, 4);

  EXPECT_TRUE(folding.is_fold_start(0));
  EXPECT_TRUE(folding.is_fold_start(3));

  const auto* all_range = folding.get_range_at(0);
  ASSERT_NE(all_range, nullptr);
  EXPECT_EQ(all_range->start_line, 0u);
  EXPECT_EQ(all_range->end_line, 2u);
  EXPECT_EQ(all_range->indent_level, 0u);
}

TEST(LanguageServerTests, PHPSyntaxAndIntelliSense) {
  // 1. Verify PHP Grammar Registration
  const auto* php_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".php");
  ASSERT_NE(php_grammar, nullptr);
  EXPECT_EQ(php_grammar->name, "PHP");
  EXPECT_TRUE(php_grammar->is_keyword("function"));
  EXPECT_TRUE(php_grammar->is_keyword("echo"));
  EXPECT_TRUE(php_grammar->is_keyword("class"));
  EXPECT_TRUE(php_grammar->is_keyword("match"));
  EXPECT_TRUE(php_grammar->is_keyword("readonly"));
  EXPECT_TRUE(php_grammar->is_type("int"));
  EXPECT_TRUE(php_grammar->is_type("string"));
  EXPECT_TRUE(php_grammar->is_type("Exception"));
  EXPECT_TRUE(php_grammar->is_type("PDO"));

  // Also verify other PHP extensions (.phtml, .php8)
  EXPECT_NE(Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".phtml"), nullptr);
  EXPECT_NE(Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".php8"), nullptr);

  // 2. Verify PHP Syntax Tokenization with GenericGrammarEngine
  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
  Language::Syntax::TokenizerState state{};

  // 2a. <?php open tag is Directive
  {
    const std::string_view tag_line = "<?php";
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        tag_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);
    EXPECT_EQ(tokens[0].text, "<?php");
    EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::Directive);
  }

  // 2b. $this is Keyword, $user is Macro, comments are Comment
  {
    const std::string_view code_line = "    $this->user = $name; // set user";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        code_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_this = false;
    bool found_name = false;
    bool found_comment = false;

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "$this") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_this = true;
      } else if (tokens[i].text == "$name") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Macro);
        found_name = true;
      } else if (tokens[i].kind == UI::Editor::EditorTokenKind::Comment) {
        found_comment = true;
      }
    }
    EXPECT_TRUE(found_this);
    EXPECT_TRUE(found_name);
    EXPECT_TRUE(found_comment);
  }

  // 2c. # single-line comment in PHP
  {
    const std::string_view hash_line = "# This is a shell/perl style PHP comment";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        hash_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);
    EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::Comment);
  }

  // 2d. ?> close tag is Directive
  {
    const std::string_view close_tag_line = "?>";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        close_tag_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);
    EXPECT_EQ(tokens[0].text, "?>");
    EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::Directive);
  }

  // 2e. HTML markup & DOCTYPE harmonization in PHP/Blade templates
  {
    const std::string_view doctype_line = "<!DOCTYPE html>";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        doctype_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_doctype = false;
    bool found_html = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "DOCTYPE") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_doctype = true;
      } else if (tokens[i].text == "html") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_html = true;
      }
    }
    EXPECT_TRUE(found_doctype);
    EXPECT_TRUE(found_html);
  }

  // 2f. HTML tags and attributes (<html lang="en" class="h-full">)
  {
    const std::string_view html_tag_line = "<html lang=\"en\" class=\"h-full\">";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        html_tag_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_html_tag = false;
    bool found_lang_attr = false;
    bool found_class_attr = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "html") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_html_tag = true;
      } else if (tokens[i].text == "lang") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Label);
        found_lang_attr = true;
      } else if (tokens[i].text == "class") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Label);
        found_class_attr = true;
      }
    }
    EXPECT_TRUE(found_html_tag);
    EXPECT_TRUE(found_lang_attr);
    EXPECT_TRUE(found_class_attr);
  }

  // 2g. HTML comment in PHP template (<!-- Font Awesome CDN -->)
  {
    const std::string_view comment_line = "<!-- Font Awesome CDN -->";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        comment_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);
    EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::Comment);
  }

  // 2h. Blade directive (@extends, @section, @yield, @csrf)
  {
    const std::string_view blade_dir_line = "@extends('layouts.app')";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        blade_dir_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);
    EXPECT_EQ(tokens[0].text, "@extends");
    EXPECT_EQ(tokens[0].kind, UI::Editor::EditorTokenKind::Keyword);
  }

  // 2i. Plain text rendering inside title and elements is Plain (white)
  {
    const std::string_view title_line = "<title>404 - Halaman Tidak Ditemukan</title>";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        title_line, *php_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "Halaman" || tokens[i].text == "Tidak" || tokens[i].text == "Ditemukan") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Plain);
      }
    }

    const std::string_view button_text = "Kembali ke Beranda";
    state = Language::Syntax::TokenizerState{};
    const std::size_t btn_count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        button_text, *php_grammar, tokens, state);
    ASSERT_GT(btn_count, 0u);

    for (std::size_t i = 0; i < btn_count; ++i) {
      if (tokens[i].text == "Kembali" || tokens[i].text == "ke" || tokens[i].text == "Beranda") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Plain);
      }
    }
  }

  // 3. Verify Server Registry Profile for PHP
  const auto* php_profile =
      Language::Registry::ServerRegistry::instance().find_profile_for_filename("app.php");
  ASSERT_NE(php_profile, nullptr);
  EXPECT_EQ(php_profile->language_id, "php");
  EXPECT_EQ(php_profile->executable_name, "phpantom_lsp");

  // Verify finding phpantom_lsp binary in plugins or system if installed
  const auto php_exe_path =
      Language::Registry::ServerRegistry::instance().find_executable_in_system("phpantom_lsp");
  if (!php_exe_path.empty()) {
    EXPECT_TRUE(std::filesystem::exists(php_exe_path));
  }

  // 4. Verify Built-in Templates & Completions for PHP
  const auto templates =
      Language::LanguageServerManager::get_templates_for_filename("index.php");
  EXPECT_FALSE(templates.empty());
  EXPECT_GE(templates.size(), 30u);

  bool has_class = false;
  bool has_construct = false;
  bool has_strlen = false;
  bool has_get_superglobal = false;
  bool has_json_encode = false;

  for (const auto& item : templates) {
    if (item.label == "class") has_class = true;
    if (item.label == "__construct") has_construct = true;
    if (item.label == "strlen") has_strlen = true;
    if (item.label == "$_GET") has_get_superglobal = true;
    if (item.label == "json_encode") has_json_encode = true;
  }

  EXPECT_TRUE(has_class);
  EXPECT_TRUE(has_construct);
  EXPECT_TRUE(has_strlen);
  EXPECT_TRUE(has_get_superglobal);
  EXPECT_TRUE(has_json_encode);

  // 5. Verify FileIconModel icon mapping for PHP and Laravel Blade templates
  EXPECT_EQ(UI::Editor::file_icon_asset_for_path(std::filesystem::path("index.php")),
            "vscode-symbols/files/php.svg");
  EXPECT_EQ(UI::Editor::file_icon_asset_for_path(std::filesystem::path("welcome.blade.php")),
            "vscode-symbols/files/laravel.svg");
  EXPECT_EQ(UI::Editor::file_icon_asset_for_path(std::filesystem::path("404.blade.php")),
            "vscode-symbols/files/laravel.svg");
  EXPECT_EQ(UI::Editor::file_icon_asset_for_path(std::filesystem::path("artisan")),
            "vscode-symbols/files/laravel.svg");
}

TEST(LanguageServerTests, CSSPropertiesValuesAndUnitsHighlighting) {
  // 1. Verify CSS Grammar Registration
  const auto* css_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".css");
  ASSERT_NE(css_grammar, nullptr);
  EXPECT_TRUE(css_grammar->name == "CSS" || css_grammar->name == "HTML/CSS");
  EXPECT_TRUE(css_grammar->is_type("border-collapse"));
  EXPECT_TRUE(css_grammar->is_type("text-align"));
  EXPECT_TRUE(css_grammar->is_type("padding"));
  EXPECT_TRUE(css_grammar->is_type("width"));
  EXPECT_TRUE(css_grammar->is_keyword("collapse"));
  EXPECT_TRUE(css_grammar->is_keyword("solid"));
  EXPECT_TRUE(css_grammar->is_keyword("black"));
  EXPECT_TRUE(css_grammar->is_keyword("left"));

  // Also verify other CSS extensions (.scss, .less)
  EXPECT_NE(Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".scss"), nullptr);
  EXPECT_NE(Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".less"), nullptr);

  // 2. Verify Tokenization of the exact user snippet across CSS & HTML grammar
  const auto* html_grammar =
      Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".html");
  ASSERT_NE(html_grammar, nullptr);

  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
  Language::Syntax::TokenizerState state{};

  // 2a. border-collapse: collapse; (property is Label, value is Keyword)
  {
    const std::string_view line = "    border-collapse: collapse;";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        line, *html_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_property = false;
    bool found_value = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "border-collapse") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Label);
        found_property = true;
      } else if (tokens[i].text == "collapse") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_value = true;
      }
    }
    EXPECT_TRUE(found_property);
    EXPECT_TRUE(found_value);
  }

  // 2b. width: 50%; (width is Label, 50% is Number)
  {
    const std::string_view line = "    width: 50%;";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        line, *html_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_width = false;
    bool found_percent = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "width") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Label);
        found_width = true;
      } else if (tokens[i].text == "50%") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Number);
        found_percent = true;
      }
    }
    EXPECT_TRUE(found_width);
    EXPECT_TRUE(found_percent);
  }

  // 2c. border: 1px solid black; (border is Label, 1px is Number, solid & black are Keyword)
  {
    const std::string_view line = "    border: 1px solid black;";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        line, *html_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_border = false;
    bool found_px = false;
    bool found_solid = false;
    bool found_black = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "border") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Label);
        found_border = true;
      } else if (tokens[i].text == "1px") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Number);
        found_px = true;
      } else if (tokens[i].text == "solid") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_solid = true;
      } else if (tokens[i].text == "black") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_black = true;
      }
    }
    EXPECT_TRUE(found_border);
    EXPECT_TRUE(found_px);
    EXPECT_TRUE(found_solid);
    EXPECT_TRUE(found_black);
  }

  // 2d. text-align: left; (text-align is Label, left is Keyword)
  {
    const std::string_view line = "    text-align: left;";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        line, *html_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_align = false;
    bool found_left = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "text-align") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Label);
        found_align = true;
      } else if (tokens[i].text == "left") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
        found_left = true;
      }
    }
    EXPECT_TRUE(found_align);
    EXPECT_TRUE(found_left);
  }

  // 2e. padding: 16px; (padding is Label, 16px is Number)
  {
    const std::string_view line = "    padding: 16px;";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        line, *html_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_padding = false;
    bool found_16px = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "padding") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Label);
        found_padding = true;
      } else if (tokens[i].text == "16px") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Number);
        found_16px = true;
      }
    }
    EXPECT_TRUE(found_padding);
    EXPECT_TRUE(found_16px);
  }

  // 2f. .logo{ (class selector logo is Type)
  {
    const std::string_view line = "  .logo{";
    state = Language::Syntax::TokenizerState{};
    const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        line, *html_grammar, tokens, state);
    ASSERT_GT(count, 0u);

    bool found_logo = false;
    for (std::size_t i = 0; i < count; ++i) {
      if (tokens[i].text == "logo") {
        EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Type);
        found_logo = true;
      }
    }
    EXPECT_TRUE(found_logo);
  }
}

TEST(LanguageServerTests, FolderIconModelDefaultsToVSCodeOutlineIcons) {
  // 1. Verify default closed folder icons use standard VS Code outline icon
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path(".git"), false), "folder.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path(".github"), false), "folder.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("Source"), false), "folder.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("Assets"), false), "folder.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("Tests"), false), "folder.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("build"), false), "folder.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("ThirdParty"), false), "folder.svg");

  // 2. Verify default opened folder icons use standard VS Code open outline icon
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path(".git"), true), "folder-open.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("Source"), true), "folder-open.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("ThirdParty"), true), "folder-open.svg");

  // 3. Optional material icon override when explicitly enabled
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path(".git"), false, true), "material-icon-theme/folder-git.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("Source"), false, true), "material-icon-theme/folder-src.svg");
  EXPECT_EQ(UI::Editor::folder_icon_asset_for_path(std::filesystem::path("ThirdParty"), false, true), "folder.svg");
}

TEST(LanguageServerTests, ShaderLanguageServerProfilesAndExecutable) {
  // 1. Verify GLSL profile resolution
  const auto* glsl_profile = Language::Registry::ServerRegistry::instance().find_profile_for_filename("sandbox.frag");
  ASSERT_NE(glsl_profile, nullptr);
  EXPECT_EQ(glsl_profile->language_id, "glsl");
  EXPECT_EQ(glsl_profile->executable_name, "shader-language-server");

  const auto* vert_profile = Language::Registry::ServerRegistry::instance().find_profile_for_filename("main.vert");
  ASSERT_NE(vert_profile, nullptr);
  EXPECT_EQ(vert_profile->language_id, "glsl");

  // 2. Verify HLSL profile resolution
  const auto* hlsl_profile = Language::Registry::ServerRegistry::instance().find_profile_for_filename("shader.hlsl");
  ASSERT_NE(hlsl_profile, nullptr);
  EXPECT_EQ(hlsl_profile->language_id, "hlsl");
  EXPECT_EQ(hlsl_profile->executable_name, "shader-language-server");

  // 3. Verify WGSL profile resolution
  const auto* wgsl_profile = Language::Registry::ServerRegistry::instance().find_profile_for_filename("render.wgsl");
  ASSERT_NE(wgsl_profile, nullptr);
  EXPECT_EQ(wgsl_profile->language_id, "wgsl");
  EXPECT_EQ(wgsl_profile->executable_name, "shader-language-server");

  // 4. Verify shader-language-server executable resolution in system / plugins if present
  const auto exe = Language::Registry::ServerRegistry::instance().find_executable_in_system("shader-language-server");
  if (!exe.empty()) {
    EXPECT_TRUE(std::filesystem::exists(exe));
  }
}

TEST(LanguageServerTests, ShaderSyntaxHighlightingGLSL) {
  const auto* grammar = Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename("test.frag");
  ASSERT_NE(grammar, nullptr);
  EXPECT_EQ(grammar->name, "GLSL");

  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
  Language::Syntax::TokenizerState state{};

  // Line with precision, uniform, and types
  const std::string line = "precision highp float; uniform vec2 iResolution; uniform float iTime;";
  const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(line, *grammar, tokens, state);
  ASSERT_GT(count, 0u);

  bool found_precision = false;
  bool found_highp = false;
  bool found_float = false;
  bool found_uniform = false;
  bool found_vec2 = false;
  bool found_iResolution = false;
  bool found_iTime = false;

  for (std::size_t i = 0; i < count; ++i) {
    if (tokens[i].text == "precision") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
      found_precision = true;
    } else if (tokens[i].text == "highp") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
      found_highp = true;
    } else if (tokens[i].text == "float") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Type);
      found_float = true;
    } else if (tokens[i].text == "uniform") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
      found_uniform = true;
    } else if (tokens[i].text == "vec2") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Type);
      found_vec2 = true;
    } else if (tokens[i].text == "iResolution") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Macro);
      found_iResolution = true;
    } else if (tokens[i].text == "iTime") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Macro);
      found_iTime = true;
    }
  }

  EXPECT_TRUE(found_precision);
  EXPECT_TRUE(found_highp);
  EXPECT_TRUE(found_float);
  EXPECT_TRUE(found_uniform);
  EXPECT_TRUE(found_vec2);
  EXPECT_TRUE(found_iResolution);
  EXPECT_TRUE(found_iTime);
}

TEST(LanguageServerTests, ShaderSyntaxHighlightingHLSL) {
  const auto* grammar = Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename("test.hlsl");
  ASSERT_NE(grammar, nullptr);
  EXPECT_EQ(grammar->name, "HLSL");

  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
  Language::Syntax::TokenizerState state{};

  const std::string line = "cbuffer Constants : register(b0) { float4x4 uViewProj; };";
  const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(line, *grammar, tokens, state);
  ASSERT_GT(count, 0u);

  bool found_cbuffer = false;
  bool found_register = false;
  bool found_float4x4 = false;

  for (std::size_t i = 0; i < count; ++i) {
    if (tokens[i].text == "cbuffer") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
      found_cbuffer = true;
    } else if (tokens[i].text == "register") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
      found_register = true;
    } else if (tokens[i].text == "float4x4") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Type);
      found_float4x4 = true;
    }
  }

  EXPECT_TRUE(found_cbuffer);
  EXPECT_TRUE(found_register);
  EXPECT_TRUE(found_float4x4);
}

TEST(LanguageServerTests, ShaderSyntaxHighlightingWGSL) {
  const auto* grammar = Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename("test.wgsl");
  ASSERT_NE(grammar, nullptr);
  EXPECT_EQ(grammar->name, "WGSL");

  std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
  Language::Syntax::TokenizerState state{};

  const std::string line = "@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f";
  const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(line, *grammar, tokens, state);
  ASSERT_GT(count, 0u);

  bool found_fragment = false;
  bool found_fn = false;
  bool found_vec2f = false;
  bool found_vec4f = false;

  for (std::size_t i = 0; i < count; ++i) {
    if (tokens[i].text == "@fragment") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
      found_fragment = true;
    } else if (tokens[i].text == "fn") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Keyword);
      found_fn = true;
    } else if (tokens[i].text == "vec2f") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Type);
      found_vec2f = true;
    } else if (tokens[i].text == "vec4f") {
      EXPECT_EQ(tokens[i].kind, UI::Editor::EditorTokenKind::Type);
      found_vec4f = true;
    }
  }

  EXPECT_TRUE(found_fragment);
  EXPECT_TRUE(found_fn);
  EXPECT_TRUE(found_vec2f);
  EXPECT_TRUE(found_vec4f);
}

