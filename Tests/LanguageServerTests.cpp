#include <gtest/gtest.h>

#include "Language/Protocol/LspProtocolSerializer.h"
#include "Language/Protocol/LspTypes.h"
#include "Language/Registry/ServerRegistry.h"
#include "Language/Toolchain/ToolchainDetector.h"
#include "Language/Syntax/GenericGrammarEngine.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/Syntax/SemanticTokensManager.h"
#include "Language/CMake/CMakeLanguageDatabase.h"
#include "UI/Components/CompletionPopup.h"
#include "UI/Editor/StudioEditorModel.h"

using namespace Zenvra;

TEST(LanguageServerTests, LspProtocolFramingAndSerialization)
{
    Language::Protocol::JsonRpcRequest req{
        .id = 42,
        .method = "textDocument/completion",
        .params = {{"textDocument", {{"uri", "file:///test.cpp"}}}}
    };

    const std::string framed = Language::Protocol::LspProtocolSerializer::serialize_request(req);
    EXPECT_TRUE(framed.starts_with("Content-Length: "));
    EXPECT_TRUE(framed.find("\r\n\r\n") != std::string::npos);
    EXPECT_TRUE(framed.find("\"id\":42") != std::string::npos || framed.find("\"id\": 42") != std::string::npos);
    EXPECT_TRUE(framed.find("textDocument/completion") != std::string::npos);
}

TEST(LanguageServerTests, SemanticTokensDeltaDecoding)
{
    // Test LSP 5-tuple delta format:
    // Token 1: line 0, start col 5, length 4, type 0 (Type), modifier 0
    // Token 2: line 0, delta start col 6 (abs col 11), length 3, type 7 (Variable), modifier 0
    // Token 3: delta line 2 (abs line 2), start col 4, length 8, type 11 (Function), modifier 0
    std::vector<uint32_t> raw_tokens = {
        0, 5, 4, 0, 0,
        0, 6, 3, 7, 0,
        2, 4, 8, 11, 0
    };

    auto decoded = Language::Syntax::SemanticTokensManager::decode_lsp_tokens(raw_tokens);
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

TEST(LanguageServerTests, GenericGrammarEngineDataDrivenTokenization)
{
    Language::Syntax::GrammarRule rust_rule;
    rust_rule.name = "Rust";
    rust_rule.extensions = {".rs"};
    rust_rule.line_comment = "//";
    rust_rule.keywords = {"fn", "let", "mut", "pub", "struct", "impl", "return"};
    rust_rule.types = {"i32", "u64", "String", "bool", "Vec"};
    rust_rule.operators = {"+", "-", "*", "/", "=", "->", "::"};

    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "pub fn calculate(val: i32) -> String {", rust_rule, tokens);

    ASSERT_GT(count, 0u);

    // Verify fn is recognized as Keyword
    bool found_fn = false;
    bool found_i32 = false;
    bool found_string = false;

    for (std::size_t i = 0; i < count; ++i)
    {
        if (tokens[i].text == "fn" && tokens[i].kind == UI::Editor::EditorTokenKind::Keyword) found_fn = true;
        if (tokens[i].text == "i32" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_i32 = true;
        if (tokens[i].text == "String" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_string = true;
    }

    EXPECT_TRUE(found_fn);
    EXPECT_TRUE(found_i32);
    EXPECT_TRUE(found_string);
}

TEST(LanguageServerTests, CMakeGrammarTokenization)
{
    Language::Syntax::GrammarRule cmake_rule;
    cmake_rule.name = "CMake";
    cmake_rule.extensions = {".cmake", "cmakelists.txt"};
    cmake_rule.line_comment = "#";
    cmake_rule.keywords = {"add_executable", "target_link_libraries", "project"};
    cmake_rule.types = {"PUBLIC", "PRIVATE", "INTERFACE"};

    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
    std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
        "target_link_libraries(MyTarget PRIVATE ZDECore)", cmake_rule, tokens);

    ASSERT_GT(count, 0u);

    bool found_cmd = false;
    bool found_private = false;

    for (std::size_t i = 0; i < count; ++i)
    {
        if (tokens[i].text == "target_link_libraries" && tokens[i].kind == UI::Editor::EditorTokenKind::Keyword) found_cmd = true;
        if (tokens[i].text == "PRIVATE" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_private = true;
    }

    EXPECT_TRUE(found_cmd);
    EXPECT_TRUE(found_private);
}

TEST(LanguageServerTests, ServerRegistryProfileLookup)
{
    auto& registry = Language::Registry::ServerRegistry::instance();

    const auto* cpp_prof = registry.find_profile_for_extension(".cpp");
    ASSERT_NE(cpp_prof, nullptr);
    EXPECT_EQ(cpp_prof->language_id, "cpp");
    EXPECT_EQ(cpp_prof->executable_name, "clangd");

    const auto* rs_prof = registry.find_profile_for_extension(".rs");
    ASSERT_NE(rs_prof, nullptr);
    EXPECT_EQ(rs_prof->language_id, "rust");
    EXPECT_EQ(rs_prof->executable_name, "rust-analyzer");

    const auto* py_prof = registry.find_profile_for_extension(".py");
    ASSERT_NE(py_prof, nullptr);
    EXPECT_EQ(py_prof->language_id, "python");

    const auto* go_prof = registry.find_profile_for_extension(".go");
    ASSERT_NE(go_prof, nullptr);
    EXPECT_EQ(go_prof->language_id, "go");
}

TEST(LanguageServerTests, ThirdPartyBinaryClangdDiscovery)
{
    auto& registry = Language::Registry::ServerRegistry::instance();
    const auto clangd_path = registry.find_executable_in_system("clangd");
    ASSERT_FALSE(clangd_path.empty());
    EXPECT_TRUE(clangd_path.string().find("clangd") != std::string::npos);
}

TEST(LanguageServerTests, CompletionPopupFuzzyFiltering)
{
    UI::Components::CompletionPopup popup;

    std::vector<Language::Protocol::CompletionItem> items;
    items.push_back({.label = "vector", .kind = Language::Protocol::CompletionItemKind::Class, .detail = "class std::vector"});
    items.push_back({.label = "string", .kind = Language::Protocol::CompletionItemKind::Class, .detail = "class std::string"});
    items.push_back({.label = "version", .kind = Language::Protocol::CompletionItemKind::Variable, .detail = "int version"});

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

TEST(LanguageServerTests, CompletionPopupDynamicScrolling)
{
    UI::Components::CompletionPopup popup;

    std::vector<Language::Protocol::CompletionItem> items;
    for (int i = 0; i < 20; ++i)
    {
        items.push_back({.label = "item_" + std::to_string(i), .kind = Language::Protocol::CompletionItemKind::Variable, .detail = "var"});
    }

    popup.show(items, 100.0F, 100.0F);
    EXPECT_EQ(popup.get_item_count(), 20u);
    EXPECT_EQ(popup.get_scroll_offset(), 0u);
    EXPECT_EQ(popup.get_selected_index(), 0u);

    // Navigate down past the visible window (default max visible is 8)
    for (int i = 0; i < 10; ++i)
    {
        popup.select_next();
    }
    EXPECT_EQ(popup.get_selected_index(), 10u);
    EXPECT_GE(popup.get_scroll_offset(), 3u); // Window scrolled down

    // Scroll using mouse wheel
    EXPECT_TRUE(popup.scroll(-2)); // Scroll down by 2 lines
    EXPECT_GE(popup.get_scroll_offset(), 5u);

    EXPECT_TRUE(popup.scroll(2));  // Scroll up by 2 lines
    EXPECT_LE(popup.get_scroll_offset(), 5u);

    // Hit test
    EXPECT_TRUE(popup.is_point_inside(120.0F, 120.0F));
    EXPECT_FALSE(popup.is_point_inside(800.0F, 800.0F));
}

TEST(LanguageServerTests, ToolchainDetectionAndStatus)
{
    auto& detector = Language::Toolchain::ToolchainDetector::instance();
    const auto& tc = detector.get_active_toolchain();

    EXPECT_FALSE(tc.name.empty());
    const std::string label = detector.get_status_bar_label();
    EXPECT_FALSE(label.empty());

    const std::string guidance = detector.get_tooltip_guidance();
    EXPECT_FALSE(guidance.empty());
}

TEST(LanguageServerTests, CMakeLanguageDatabaseBuiltinCompletions)
{
    const auto items = Language::CMake::CMakeLanguageDatabase::instance().get_all_completions();
    EXPECT_GT(items.size(), 50u);

    bool found_min_req = false;
    bool found_version = false;
    bool found_link_libs = false;
    bool found_public = false;

    for (const auto& item : items)
    {
        if (item.label == "cmake_minimum_required")
        {
            found_min_req = true;
            EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Function);
            EXPECT_FALSE(item.detail.empty());
            EXPECT_FALSE(item.documentation.empty());
        }
        else if (item.label == "VERSION")
        {
            found_version = true;
            EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Variable);
        }
        else if (item.label == "target_link_libraries")
        {
            found_link_libs = true;
            EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Function);
        }
        else if (item.label == "PUBLIC")
        {
            found_public = true;
            EXPECT_EQ(item.kind, Language::Protocol::CompletionItemKind::Keyword);
        }
    }

    EXPECT_TRUE(found_min_req);
    EXPECT_TRUE(found_version);
    EXPECT_TRUE(found_link_libs);
    EXPECT_TRUE(found_public);
}

TEST(LanguageServerTests, CMakeLanguageDatabaseHover)
{
    auto hover_cmd = Language::CMake::CMakeLanguageDatabase::instance().find_hover("cmake_minimum_required");
    ASSERT_TRUE(hover_cmd.has_value());
    EXPECT_TRUE(hover_cmd->contents.find("cmake_minimum_required") != std::string::npos);

    auto hover_var = Language::CMake::CMakeLanguageDatabase::instance().find_hover("${CMAKE_VERSION}");
    ASSERT_TRUE(hover_var.has_value());
    EXPECT_TRUE(hover_var->contents.find("CMAKE_VERSION") != std::string::npos);

    auto hover_kw = Language::CMake::CMakeLanguageDatabase::instance().find_hover("PUBLIC");
    ASSERT_TRUE(hover_kw.has_value());
    EXPECT_TRUE(hover_kw->contents.find("PUBLIC") != std::string::npos);
}

