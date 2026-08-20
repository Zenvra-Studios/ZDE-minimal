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
#include "UI/Components/HoverTooltip.h"
#include "UI/Editor/StudioEditorModel.h"
#include "UI/Toolbar/StudioMainToolbar.h"
#include "Tools/Builder/CMakeBuilder.h"
#include "Tools/Runner/ProcessRunner.h"
#include "UI/Editor/ActivityPanelModel.h"
#include "Platform/HostSystem.h"

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
    // Token 1: line 0, start col 5, length 4, type (Type), modifier 0
    // Token 2: line 0, delta start col 6 (abs col 11), length 3, type (Variable), modifier 0
    // Token 3: delta line 2 (abs line 2), start col 4, length 8, type (Function), modifier 0
    std::vector<uint32_t> raw_tokens = {
        0, 5, 4, static_cast<uint32_t>(Language::Syntax::SemanticTokenType::Type), 0,
        0, 6, 3, static_cast<uint32_t>(Language::Syntax::SemanticTokenType::Variable), 0,
        2, 4, 8, static_cast<uint32_t>(Language::Syntax::SemanticTokenType::Function), 0
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

TEST(LanguageServerTests, CppSyntaxHighlightingNamespacesFunctionsClasses)
{
    const auto* grammar = Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".cpp");
    ASSERT_NE(grammar, nullptr);

    // 1. Namespace declaration
    {
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
        const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
            "namespace Zenvra::Platform::X11::Components", *grammar, tokens);
        ASSERT_GT(count, 0u);

        bool found_namespace_kw = false;
        bool found_zenvra_label = false;
        bool found_platform_label = false;
        bool found_x11_label = false;
        bool found_components_label = false;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (tokens[i].text == "namespace" && tokens[i].kind == UI::Editor::EditorTokenKind::Keyword) found_namespace_kw = true;
            if (tokens[i].text == "Zenvra" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_zenvra_label = true;
            if (tokens[i].text == "Platform" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_platform_label = true;
            if (tokens[i].text == "X11" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_x11_label = true;
            if (tokens[i].text == "Components" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_components_label = true;
        }

        EXPECT_TRUE(found_namespace_kw);
        EXPECT_TRUE(found_zenvra_label);
        EXPECT_TRUE(found_platform_label);
        EXPECT_TRUE(found_x11_label);
        EXPECT_TRUE(found_components_label);
    }

    // 2. Member function definition with class qualifier
    {
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
        const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
            "void EditorScrollbar::reset() noexcept", *grammar, tokens);
        ASSERT_GT(count, 0u);

        bool found_void_type = false;
        bool found_scrollbar_class = false;
        bool found_reset_func = false;
        bool found_noexcept_kw = false;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (tokens[i].text == "void" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_void_type = true;
            if (tokens[i].text == "EditorScrollbar" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_scrollbar_class = true;
            if (tokens[i].text == "reset" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_reset_func = true;
            if (tokens[i].text == "noexcept" && tokens[i].kind == UI::Editor::EditorTokenKind::Keyword) found_noexcept_kw = true;
        }

        EXPECT_TRUE(found_void_type);
        EXPECT_TRUE(found_scrollbar_class);
        EXPECT_TRUE(found_reset_func);
        EXPECT_TRUE(found_noexcept_kw);
    }

    // 3. Class declaration
    {
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
        const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
            "class MyComponent : public BaseComponent", *grammar, tokens);
        ASSERT_GT(count, 0u);

        bool found_class_kw = false;
        bool found_my_component = false;
        bool found_public_kw = false;
        bool found_base_component = false;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (tokens[i].text == "class" && tokens[i].kind == UI::Editor::EditorTokenKind::Keyword) found_class_kw = true;
            if (tokens[i].text == "MyComponent" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_my_component = true;
            if (tokens[i].text == "public" && tokens[i].kind == UI::Editor::EditorTokenKind::Keyword) found_public_kw = true;
            if (tokens[i].text == "BaseComponent" && tokens[i].kind == UI::Editor::EditorTokenKind::Label) found_base_component = true;
        }

        EXPECT_TRUE(found_class_kw);
        EXPECT_TRUE(found_my_component);
        EXPECT_TRUE(found_public_kw);
        EXPECT_TRUE(found_base_component);
    }

    // 4. Data types and std namespace
    {
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
        const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
            "const std::string text = \"Hello\";", *grammar, tokens);
        ASSERT_GT(count, 0u);

        bool found_const_kw = false;
        bool found_std_type = false;
        bool found_string_type = false;
        bool found_text_plain = false;
        bool found_str_literal = false;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (tokens[i].text == "const" && tokens[i].kind == UI::Editor::EditorTokenKind::Keyword) found_const_kw = true;
            if (tokens[i].text == "std" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_std_type = true;
            if (tokens[i].text == "string" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_string_type = true;
            if (tokens[i].text == "text" && tokens[i].kind == UI::Editor::EditorTokenKind::Plain) found_text_plain = true;
            if (tokens[i].text == "\"Hello\"" && tokens[i].kind == UI::Editor::EditorTokenKind::String) found_str_literal = true;
        }

        EXPECT_TRUE(found_const_kw);
        EXPECT_TRUE(found_std_type);
        EXPECT_TRUE(found_string_type);
        EXPECT_TRUE(found_text_plain);
        EXPECT_TRUE(found_str_literal);
    }

    // 5. STL template containers, fixed-width ints, and smart pointers
    {
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
        const std::size_t count = Language::Syntax::GenericGrammarEngine::tokenize_line(
            "std::vector<uint32_t> items = std::make_unique<uint32_t>(10);", *grammar, tokens);
        ASSERT_GT(count, 0u);

        bool found_std_type = false;
        bool found_vector_type = false;
        bool found_uint32_type = false;
        bool found_make_unique_type = false;
        bool found_num = false;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (tokens[i].text == "std" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_std_type = true;
            if (tokens[i].text == "vector" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_vector_type = true;
            if (tokens[i].text == "uint32_t" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_uint32_type = true;
            if (tokens[i].text == "make_unique" && tokens[i].kind == UI::Editor::EditorTokenKind::Type) found_make_unique_type = true;
            if (tokens[i].text == "10" && tokens[i].kind == UI::Editor::EditorTokenKind::Number) found_num = true;
        }

        EXPECT_TRUE(found_std_type);
        EXPECT_TRUE(found_vector_type);
        EXPECT_TRUE(found_uint32_type);
        EXPECT_TRUE(found_make_unique_type);
        EXPECT_TRUE(found_num);
    }
}

TEST(LanguageServerTests, SemanticTokensColorMapping)
{
    const auto palette = UI::Editor::StudioEditorPalette::jetbrains_dark();

    // Namespace, Class, Struct, Interface, Function, Method, Macro must all map to palette.label (Pink)
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Namespace, palette), palette.label);
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Class, palette), palette.label);
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Struct, palette), palette.label);
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Interface, palette), palette.label);
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Function, palette), palette.label);
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Method, palette), palette.label);
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Macro, palette), palette.label);

    // Types map to palette.type (Cyan)
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Type, palette), palette.type);

    // Keywords map to palette.keyword (Peach)
    EXPECT_EQ(Language::Syntax::SemanticTokensManager::get_token_color(
        Language::Syntax::SemanticTokenType::Keyword, palette), palette.keyword);
}

TEST(LanguageServerTests, ToolbarRunConfigurationWidgetState)
{
    UI::Toolbar::Widgets::RunConfigurationWidget widget;
    widget.set_active_target("ZDE");
    widget.set_active_mode(UI::Toolbar::BuildConfigurationMode::Debug);
    widget.set_active_architecture(UI::Toolbar::TargetArchitecture::Arm64);

    EXPECT_EQ(widget.get_state().active_target_name, "ZDE");
    EXPECT_EQ(widget.get_state().active_mode, UI::Toolbar::BuildConfigurationMode::Debug);
    EXPECT_EQ(widget.get_state().active_architecture, UI::Toolbar::TargetArchitecture::Arm64);
    EXPECT_EQ(widget.get_summary_label(), "ZDE | Debug | arm64");

    widget.set_active_mode(UI::Toolbar::BuildConfigurationMode::Release);
    widget.set_active_architecture(UI::Toolbar::TargetArchitecture::X86_64);
    widget.set_active_target("ZDEUnitTests");

    EXPECT_EQ(widget.get_summary_label(), "ZDEUnitTests | Release | x86_64");
}

TEST(LanguageServerTests, ToolbarLayoutResponsiveComputation)
{
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

TEST(LanguageServerTests, ToolsCMakeBuilderAndRunner)
{
    Tools::Builder::CMakeBuilder builder;
    const auto targets = builder.discover_cmake_targets(".");
    EXPECT_GE(targets.size(), 2u);
    EXPECT_EQ(targets[0], "ZDE");

    Tools::Runner::ProcessRunner runner;
    Tools::Runner::ProcessExecutionOptions opts{
        .executable_path = "non_existent_binary",
        .arguments = {},
        .working_directory = ".",
        .run_in_background = false
    };
    EXPECT_FALSE(runner.launch_process(opts));
}

TEST(LanguageServerTests, HostSystemArchitectureAndOSDetection)
{
    const auto os = Platform::HostSystem::get_operating_system();
    EXPECT_NE(os, Platform::HostSystem::OperatingSystem::Unknown);

    const auto arch = Platform::HostSystem::get_native_architecture();
    EXPECT_NE(arch, Platform::HostSystem::Architecture::Unknown);

    const auto& info = Platform::HostSystem::get_system_info();
    EXPECT_FALSE(info.default_preset_debug.empty());
    EXPECT_FALSE(info.default_preset_release.empty());
}

TEST(LanguageServerTests, ActivityPanelModelFileFolderManipulationAndMove)
{
    UI::Editor::ActivityPanelModel model;
    EXPECT_TRUE(model.initialize("."));

    const std::filesystem::path temp_dir = std::filesystem::current_path() / "build" / "temp_test_tree";
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

TEST(LanguageServerTests, ActivityPanelModelScrollingAndClamping)
{
    UI::Editor::ActivityPanelModel model;
    EXPECT_TRUE(model.initialize(std::filesystem::current_path()));
    EXPECT_TRUE(model.is_visible());
    EXPECT_TRUE(model.is_active(UI::Editor::SidebarIcon::Project));

    const auto items = model.get_project_items();
    if (items.size() > 5)
    {
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

TEST(LanguageServerTests, HoverTooltipStateAndBoundsCalculation)
{
    UI::Components::HoverTooltip tooltip;
    EXPECT_FALSE(tooltip.is_visible());

    tooltip.show("```cpp\nint calculate(int a, int b);\n```\nCalculates sum of a and b", 150.0F, 200.0F);
    EXPECT_TRUE(tooltip.is_visible());
    EXPECT_EQ(tooltip.get_x(), 150.0F);
    EXPECT_EQ(tooltip.get_y(), 200.0F);
    EXPECT_FALSE(tooltip.get_content().empty());

    const auto bounds = tooltip.calculate_bounds(320.0F, 90.0F);
    EXPECT_EQ(bounds.x, 150.0F);
    EXPECT_EQ(bounds.y, 200.0F);
    EXPECT_EQ(bounds.width, 320.0F);
    EXPECT_EQ(bounds.height, 90.0F);

    tooltip.hide();
    EXPECT_FALSE(tooltip.is_visible());
}



