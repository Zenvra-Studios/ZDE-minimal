#include <gtest/gtest.h>

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

TEST(LanguageServerTests, ThirdPartyBinaryClangdDiscovery) {
  auto &registry = Language::Registry::ServerRegistry::instance();
  const auto clangd_path = registry.find_executable_in_system("clangd");
  ASSERT_FALSE(clangd_path.empty());
  EXPECT_TRUE(clangd_path.string().find("clangd") != std::string::npos);
}

TEST(LanguageServerTests, ThirdPartyBinaryTlsDiscovery) {
  auto &registry = Language::Registry::ServerRegistry::instance();
  const auto tls_path = registry.find_executable_in_system("typescript-language-server");
  ASSERT_FALSE(tls_path.empty());
  EXPECT_TRUE(tls_path.string().find("typescript-language-server") != std::string::npos);
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
            palette.label);

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
          tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
        found_define_kw = true;
      if (tokens[i].text == "MAX_BUFFER_SIZE" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::Label)
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
          tokens[i].kind == UI::Editor::EditorTokenKind::Keyword)
        found_include_kw = true;
      if (tokens[i].text == "<filesystem>" &&
          tokens[i].kind == UI::Editor::EditorTokenKind::String)
        found_header_str = true;
    }

    EXPECT_TRUE(found_include_kw);
    EXPECT_TRUE(found_header_str);
  }
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


