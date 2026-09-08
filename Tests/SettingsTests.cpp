#include "Settings/JsonSettingsStore.h"
#include "Settings/SettingsDefinition.h"
#include "Settings/SettingsEvent.h"
#include "Settings/SettingsSchema.h"
#include "Settings/SettingsService.h"
#include "Settings/SettingsValue.h"
#include "UI/Settings/SettingsControl.h"
#include "UI/Settings/SettingsWindow.h"
#include "Utility/Ascii/AsciiArtConverter.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace Zenvra::Settings;

class SettingsTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_temp_dir = std::filesystem::temp_directory_path() / "zde_settings_unit_tests";
        std::error_code ec;
        std::filesystem::remove_all(m_temp_dir, ec);
        std::filesystem::create_directories(m_temp_dir, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_temp_dir, ec);
    }

    std::filesystem::path m_temp_dir;
};

TEST_F(SettingsTestFixture, SettingsValuePrimitivesAndSerialization)
{
    SettingsValue b_val(true);
    EXPECT_TRUE(b_val.is_bool());
    EXPECT_TRUE(b_val.as_bool());
    EXPECT_EQ(b_val.to_display_string(), "true");

    SettingsValue i_val(42);
    EXPECT_TRUE(i_val.is_int());
    EXPECT_EQ(i_val.as_int(), 42);
    EXPECT_EQ(i_val.to_display_string(), "42");

    SettingsValue f_val(18.5);
    EXPECT_TRUE(f_val.is_float());
    EXPECT_DOUBLE_EQ(f_val.as_float(), 18.5);

    SettingsValue s_val("Cascadia Code");
    EXPECT_TRUE(s_val.is_string());
    EXPECT_EQ(s_val.as_string(), "Cascadia Code");

    // JSON serialization roundtrip
    const auto json_obj = s_val.to_json();
    const auto restored = SettingsValue::from_json(json_obj);
    EXPECT_EQ(restored.as_string(), "Cascadia Code");
}

TEST_F(SettingsTestFixture, SchemaRegistrationAndSearch)
{
    SettingsSchema schema;

    schema.register_setting({
        .id = "editor.fontSize",
        .title = "Font Size",
        .description = "Code editor font size in pixels.",
        .type = SettingType::Integer,
        .defaultValue = 14,
        .category = "Editor",
        .subcategory = "Typography",
        .tags = {"font", "size", "zoom"},
        .minimum = 8.0,
        .maximum = 72.0,
        .step = 1.0,
    });

    schema.register_setting({
        .id = "theme.current",
        .title = "Color Theme",
        .description = "Active color theme.",
        .type = SettingType::Enum,
        .defaultValue = "Dark",
        .category = "Appearance",
        .subcategory = "Theme",
        .tags = {"theme", "dark", "light"},
        .enum_values = {{"Dark", "Dark"}, {"Light", "Light"}},
    });

    EXPECT_TRUE(schema.has_setting("editor.fontSize"));
    EXPECT_TRUE(schema.has_setting("theme.current"));
    EXPECT_FALSE(schema.has_setting("non.existent"));

    const auto* font_def = schema.get_setting("editor.fontSize");
    ASSERT_NE(font_def, nullptr);
    EXPECT_EQ(font_def->defaultValue.as_int(), 14);

    // Search query matching
    const auto search_font = schema.search("font");
    EXPECT_EQ(search_font.size(), 1u);
    EXPECT_EQ(search_font[0]->id, "editor.fontSize");

    const auto search_theme = schema.search("theme");
    EXPECT_EQ(search_theme.size(), 1u);
    EXPECT_EQ(search_theme[0]->id, "theme.current");
}

TEST_F(SettingsTestFixture, SchemaValidationAndClamping)
{
    SettingsSchema schema;
    schema.register_setting({
        .id = "editor.fontSize",
        .title = "Font Size",
        .type = SettingType::Integer,
        .defaultValue = 14,
        .minimum = 8.0,
        .maximum = 72.0,
    });

    schema.register_setting({
        .id = "theme.current",
        .title = "Theme",
        .type = SettingType::Enum,
        .defaultValue = "Dark",
        .enum_values = {{"Dark", "Dark"}, {"Light", "Light"}},
    });

    // Clamping too low
    SettingsValue clamped_low;
    EXPECT_FALSE(schema.validate("editor.fontSize", SettingsValue(4), &clamped_low));
    EXPECT_EQ(clamped_low.as_int(), 8);

    // Clamping too high
    SettingsValue clamped_high;
    EXPECT_FALSE(schema.validate("editor.fontSize", SettingsValue(120), &clamped_high));
    EXPECT_EQ(clamped_high.as_int(), 72);

    // Valid value
    SettingsValue valid;
    EXPECT_TRUE(schema.validate("editor.fontSize", SettingsValue(18), &valid));
    EXPECT_EQ(valid.as_int(), 18);

    // Enum validation
    SettingsValue valid_enum;
    EXPECT_TRUE(schema.validate("theme.current", SettingsValue("Light"), &valid_enum));

    SettingsValue invalid_enum;
    EXPECT_FALSE(schema.validate("theme.current", SettingsValue("Solarized"), &invalid_enum));
    EXPECT_EQ(invalid_enum.as_string(), "Dark"); // Fallback to default
}

TEST_F(SettingsTestFixture, JsonStorePersistenceAndRecovery)
{
    const auto store_path = m_temp_dir / "user_settings.json";

    {
        JsonSettingsStore store(store_path);
        EXPECT_TRUE(store.load());
        store.set("editor.fontSize", SettingsValue(18));
        store.set("editor.wordWrap", SettingsValue(true));
        EXPECT_TRUE(store.contains("editor.fontSize"));
    }

    // Verify persisted on disk
    EXPECT_TRUE(std::filesystem::exists(store_path));

    {
        JsonSettingsStore reloaded_store(store_path);
        EXPECT_TRUE(reloaded_store.load());
        const auto val = reloaded_store.get("editor.fontSize");
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(val->as_int(), 18);
        EXPECT_TRUE(reloaded_store.get("editor.wordWrap")->as_bool());
    }

    // Corrupt file resilience test
    {
        std::ofstream corrupt(store_path, std::ios::trunc);
        corrupt << "{ this is not valid json : [[";
    }

    {
        JsonSettingsStore corrupt_store(store_path);
        // Loading corrupt file should fail gracefully without crashing
        EXPECT_FALSE(corrupt_store.load());
        // Backup file .bak should have been created
        EXPECT_TRUE(std::filesystem::exists(store_path.string() + ".bak"));
        // Setting values should work cleanly after corruption recovery
        corrupt_store.set("editor.fontSize", SettingsValue(16));
        EXPECT_EQ(corrupt_store.get("editor.fontSize")->as_int(), 16);
    }
}

TEST_F(SettingsTestFixture, ServiceScopeHierarchyAndOverride)
{
    const auto user_path = m_temp_dir / "user_settings.json";
    const auto ws_path = m_temp_dir / "workspace_settings.json";

    SettingsService service;
    service.initialize(user_path, ws_path);

    // Default value before any write
    EXPECT_EQ(service.get<int>("editor.fontSize"), 14);
    EXPECT_EQ(service.get_effective_scope("editor.fontSize"), SettingsScope::Default);

    // Set User scope
    service.set("editor.fontSize", 16, SettingsScope::User);
    EXPECT_EQ(service.get<int>("editor.fontSize"), 16);
    EXPECT_EQ(service.get_effective_scope("editor.fontSize"), SettingsScope::User);

    // Set Workspace scope (overrides User)
    service.set("editor.fontSize", 22, SettingsScope::Workspace);
    EXPECT_EQ(service.get<int>("editor.fontSize"), 22);
    EXPECT_EQ(service.get_effective_scope("editor.fontSize"), SettingsScope::Workspace);

    // Reset workspace override -> should fall back to User setting (16)
    service.reset("editor.fontSize", SettingsScope::Workspace);
    EXPECT_EQ(service.get<int>("editor.fontSize"), 16);
    EXPECT_EQ(service.get_effective_scope("editor.fontSize"), SettingsScope::User);

    // Reset user setting -> should fall back to default (14)
    service.reset("editor.fontSize", SettingsScope::User);
    EXPECT_EQ(service.get<int>("editor.fontSize"), 14);
    EXPECT_EQ(service.get_effective_scope("editor.fontSize"), SettingsScope::Default);
}

TEST_F(SettingsTestFixture, ServicePubSubEventNotification)
{
    const auto user_path = m_temp_dir / "pubsub_settings.json";

    SettingsService service;
    service.initialize(user_path);

    int callback_invocations = 0;
    std::string last_changed_id;
    int64_t last_new_val = 0;

    const auto sub_id = service.subscribe("editor.fontSize", [&](const SettingsChangedEvent& event) {
        callback_invocations++;
        last_changed_id = event.id;
        last_new_val = event.new_value.as_int();
    });

    service.set("editor.fontSize", 20);
    EXPECT_EQ(callback_invocations, 1);
    EXPECT_EQ(last_changed_id, "editor.fontSize");
    EXPECT_EQ(last_new_val, 20);

    // Setting same value should not trigger redundant event
    service.set("editor.fontSize", 20);
    EXPECT_EQ(callback_invocations, 1);

    // Unsubscribe
    service.unsubscribe(sub_id);
    service.set("editor.fontSize", 24);
    EXPECT_EQ(callback_invocations, 1); // No new invocation
}

TEST_F(SettingsTestFixture, SettingRowLayoutInteractionsAndHover)
{
    const auto user_path = m_temp_dir / "layout_settings.json";
    SettingsService service;
    service.initialize(user_path);

    Zenvra::UI::Settings::SettingRowLayout row;
    row.bounds = {0.0F, 0.0F, 500.0F, 80.0F};
    row.checkbox_bounds = {24.0F, 40.0F, 18.0F, 18.0F};
    row.minus_btn_bounds = {24.0F, 40.0F, 28.0F, 24.0F};
    row.value_label_bounds = {52.0F, 40.0F, 60.0F, 24.0F};
    row.plus_btn_bounds = {112.0F, 40.0F, 28.0F, 24.0F};
    row.reset_btn_bounds = {450.0F, 10.0F, 22.0F, 22.0F};
    row.is_modified = true;

    // Check hit testing
    EXPECT_TRUE(row.is_interactive_point(30.0F, 45.0F));   // minus/checkbox area
    EXPECT_TRUE(row.is_interactive_point(120.0F, 45.0F));  // plus area
    EXPECT_TRUE(row.is_interactive_point(460.0F, 15.0F));  // reset button
    EXPECT_FALSE(row.is_interactive_point(300.0F, 15.0F)); // non-interactive area

    // Check hover
    EXPECT_TRUE(row.handle_pointer_move(460.0F, 15.0F));
    EXPECT_TRUE(row.is_reset_hovered);
    EXPECT_TRUE(row.handle_pointer_move(120.0F, 45.0F));
    EXPECT_FALSE(row.is_reset_hovered);
    EXPECT_TRUE(row.is_plus_hovered);

    // Test Integer Stepper interaction
    const auto* font_def = service.get_schema().get_setting("editor.fontSize");
    ASSERT_NE(font_def, nullptr);
    const int64_t initial_val = service.get<int64_t>("editor.fontSize");

    // Click plus button
    EXPECT_TRUE(row.handle_pointer_press(120.0F, 45.0F, *font_def, service, SettingsScope::User));
    EXPECT_EQ(service.get<int64_t>("editor.fontSize"), initial_val + 1);

    // Click minus button
    EXPECT_TRUE(row.handle_pointer_press(30.0F, 45.0F, *font_def, service, SettingsScope::User));
    EXPECT_EQ(service.get<int64_t>("editor.fontSize"), initial_val);

    // Test Reset button interaction
    row.is_modified = true;
    EXPECT_TRUE(row.handle_pointer_press(460.0F, 15.0F, *font_def, service, SettingsScope::User));
}

TEST_F(SettingsTestFixture, SettingsWindowSidebarLayoutAndScroll)
{
    Zenvra::UI::Settings::SettingsWindow win;
    const auto layout = win.calculate_layout(880.0F, 620.0F);

    // Verify layout borders exist
    EXPECT_FALSE(layout.header_separator_bounds.is_empty());
    EXPECT_FALSE(layout.sidebar_divider_bounds.is_empty());
    EXPECT_FALSE(layout.category_items.empty());

    // Sidebar items should be positioned inside sidebar bounds
    for (const auto& item : layout.category_items) {
        EXPECT_GE(item.bounds.x, 0.0F);
        EXPECT_LE(item.bounds.right(), layout.sidebar_divider_bounds.x);
    }

    // Expand "Workbench" and "Features" to overflow sidebar height
    for (const auto& item : layout.category_items) {
        if (item.name == "Workbench" && item.has_children) {
            win.handle_pointer_press(item.chevron_bounds.x + 2.0F, item.chevron_bounds.y + 2.0F, layout);
            break;
        }
    }

    auto exp_layout = win.calculate_layout(880.0F, 620.0F);
    for (const auto& item : exp_layout.category_items) {
        if (item.name == "Features" && item.has_children) {
            win.handle_pointer_press(item.chevron_bounds.x + 2.0F, item.chevron_bounds.y + 2.0F, exp_layout);
            break;
        }
    }

    const auto overflow_layout = win.calculate_layout(880.0F, 620.0F);
    // Overflow should produce a visible sidebar scrollbar thumb
    EXPECT_FALSE(overflow_layout.sidebar_scrollbar_thumb.is_empty());
    EXPECT_GT(overflow_layout.sidebar_scrollbar_thumb.height, 0.0F);

    // Scrolling downward over the sidebar
    EXPECT_TRUE(win.handle_scroll(-2.0F, overflow_layout, 50.0F, 200.0F));
    const auto after_scroll_layout = win.calculate_layout(880.0F, 620.0F);
    EXPECT_LT(after_scroll_layout.category_items[0].bounds.y, overflow_layout.category_items[0].bounds.y);

    // Scrolling over the content area
    EXPECT_TRUE(win.handle_scroll(-1.0F, after_scroll_layout, 400.0F, 200.0F));
}

TEST_F(SettingsTestFixture, EditorAndTerminalSettingsPubSub)
{
    const auto user_path = m_temp_dir / "user_settings.json";
    const auto ws_path = m_temp_dir / "workspace_settings.json";

    SettingsService service;
    service.initialize(user_path, ws_path);

    int editor_font_size_updates = 0;
    int terminal_font_size_updates = 0;
    std::string last_cursor_style;
    std::string last_whitespace_mode;
    int last_tab_size = 0;

    static_cast<void>(service.subscribe("editor.fontSize", [&](const SettingsChangedEvent& e) {
        editor_font_size_updates++;
        EXPECT_EQ(e.new_value.as_int(), 18);
    }));

    static_cast<void>(service.subscribe("terminal.fontSize", [&](const SettingsChangedEvent& e) {
        terminal_font_size_updates++;
        EXPECT_EQ(e.new_value.as_int(), 16);
    }));

    static_cast<void>(service.subscribe("editor.cursorStyle", [&](const SettingsChangedEvent& e) {
        last_cursor_style = e.new_value.as_string();
    }));

    static_cast<void>(service.subscribe("editor.renderWhitespace", [&](const SettingsChangedEvent& e) {
        last_whitespace_mode = e.new_value.as_string();
    }));

    static_cast<void>(service.subscribe("editor.tabSize", [&](const SettingsChangedEvent& e) {
        last_tab_size = e.new_value.as_int();
    }));

    service.set("editor.fontSize", 18);
    service.set("terminal.fontSize", 16);
    service.set("editor.cursorStyle", std::string("Block"));
    service.set("editor.renderWhitespace", std::string("all"));
    service.set("editor.tabSize", 2);

    EXPECT_EQ(editor_font_size_updates, 1);
    EXPECT_EQ(terminal_font_size_updates, 1);
    EXPECT_EQ(last_cursor_style, "Block");
    EXPECT_EQ(last_whitespace_mode, "all");
    EXPECT_EQ(last_tab_size, 2);
}

TEST_F(SettingsTestFixture, MascotAssetSlotAndImageFormats)
{
    const auto user_path = m_temp_dir / "user_settings.json";
    const auto ws_path = m_temp_dir / "workspace_settings.json";

    SettingsService service;
    service.initialize(user_path, ws_path);

    // 1. Verify schema registration and default mascot
    const auto* mascot_def = service.get_schema().get_setting("workbench.mascot.image");
    ASSERT_NE(mascot_def, nullptr);
    EXPECT_EQ(mascot_def->type, SettingType::String);
    EXPECT_EQ(mascot_def->defaultValue.as_string(), "zenvra_logo.png");
    EXPECT_EQ(service.get<std::string>("workbench.mascot.image"), "zenvra_logo.png");

    // 2. Test PubSub event notification on change
    int mascot_updates = 0;
    std::string latest_mascot_path;
    static_cast<void>(service.subscribe("workbench.mascot.image", [&](const SettingsChangedEvent& e) {
        mascot_updates++;
        latest_mascot_path = e.new_value.as_string();
    }));

    // 3. Test various supported formats (.png, .jpg, .jpeg, .bmp)
    const std::vector<std::string> test_formats = {
        "C:/Assets/mascot_fox.png",
        "D:/Wallpapers/dragon.jpg",
        "C:/Images/photo.jpeg",
        "E:/Icons/custom_logo.bmp"
    };

    for (const auto& path : test_formats) {
        service.set("workbench.mascot.image", path, SettingsScope::User);
        EXPECT_EQ(service.get<std::string>("workbench.mascot.image"), path);
    }
    EXPECT_EQ(mascot_updates, 4);
    EXPECT_EQ(latest_mascot_path, "E:/Icons/custom_logo.bmp");

    // 4. Test Reset to default
    service.reset("workbench.mascot.image", SettingsScope::User);
    EXPECT_EQ(service.get<std::string>("workbench.mascot.image"), "zenvra_logo.png");

    // 5. Test SettingsWindow layout: Unreal Engine asset slot bounds
    Zenvra::UI::Settings::SettingsWindow win;
    const auto layout = win.calculate_layout(900.0F, 700.0F);

    bool mascot_row_found = false;
    for (const auto& row : layout.rows) {
        if (row.def && row.def->id == "workbench.mascot.image") {
            mascot_row_found = true;
            EXPECT_GT(row.bounds.height, 100.0F); // 126px row height
            EXPECT_FALSE(row.thumbnail_bounds.is_empty());
            EXPECT_FALSE(row.browse_btn_bounds.is_empty());
            EXPECT_FALSE(row.reset_btn_bounds.is_empty());
            EXPECT_FALSE(row.input_bounds.is_empty());

            // 6. Test hover interaction on thumbnail and browse button
            Zenvra::UI::Settings::SettingRowLayout interactive_row = row;
            EXPECT_TRUE(interactive_row.handle_pointer_move(row.thumbnail_bounds.x + 5.0F, row.thumbnail_bounds.y + 5.0F));
            EXPECT_TRUE(interactive_row.is_thumbnail_hovered);

            EXPECT_TRUE(interactive_row.handle_pointer_move(row.browse_btn_bounds.x + 5.0F, row.browse_btn_bounds.y + 5.0F));
            EXPECT_TRUE(interactive_row.is_browse_hovered);
            break;
        }
    }
    EXPECT_TRUE(mascot_row_found);
}

TEST_F(SettingsTestFixture, WelcomeAppTitleSettingAndLayout)
{
    const auto user_path = m_temp_dir / "user_settings.json";
    const auto ws_path = m_temp_dir / "workspace_settings.json";

    SettingsService service;
    service.initialize(user_path, ws_path);

    // 1. Verify schema registration and default title
    const auto* title_def = service.get_schema().get_setting("workbench.app.title");
    ASSERT_NE(title_def, nullptr);
    EXPECT_EQ(title_def->type, SettingType::String);
    EXPECT_EQ(title_def->defaultValue.as_string(), "Zenvra Development Studio");
    EXPECT_EQ(service.get<std::string>("workbench.app.title"), "Zenvra Development Studio");

    // 2. Test PubSub event notification on title change
    int title_updates = 0;
    std::string latest_title;
    static_cast<void>(service.subscribe("workbench.app.title", [&](const SettingsChangedEvent& e) {
        title_updates++;
        latest_title = e.new_value.as_string();
    }));

    service.set("workbench.app.title", std::string("My Custom Studio"), SettingsScope::User);
    EXPECT_EQ(service.get<std::string>("workbench.app.title"), "My Custom Studio");
    EXPECT_EQ(title_updates, 1);
    EXPECT_EQ(latest_title, "My Custom Studio");

    // 3. Test Reset to default
    service.reset("workbench.app.title", SettingsScope::User);
    EXPECT_EQ(service.get<std::string>("workbench.app.title"), "Zenvra Development Studio");

    // 4. Test SettingsWindow layout: verify workbench.app.title is directly above workbench.mascot.image
    Zenvra::UI::Settings::SettingsWindow win;
    const auto layout = win.calculate_layout(900.0F, 700.0F);

    int title_index = -1;
    int mascot_index = -1;
    for (std::size_t i = 0; i < layout.rows.size(); ++i) {
        if (layout.rows[i].def) {
            if (layout.rows[i].def->id == "workbench.app.title") {
                title_index = static_cast<int>(i);
                EXPECT_FALSE(layout.rows[i].input_bounds.is_empty());
                EXPECT_TRUE(layout.rows[i].is_interactive_point(
                    layout.rows[i].input_bounds.x + 5.0F,
                    layout.rows[i].input_bounds.y + 5.0F));
            } else if (layout.rows[i].def->id == "workbench.mascot.image") {
                mascot_index = static_cast<int>(i);
            }
        }
    }

    EXPECT_NE(title_index, -1);
    EXPECT_NE(mascot_index, -1);
    EXPECT_EQ(title_index + 1, mascot_index);
}

TEST_F(SettingsTestFixture, AsciiArtConverterAndMascotRenderMode)
{
    const auto user_path = m_temp_dir / "user_settings.json";
    const auto ws_path = m_temp_dir / "workspace_settings.json";

    SettingsService service;
    service.initialize(user_path, ws_path);

    // 1. Verify schema registration, type, and default value
    const auto* mode_def = service.get_schema().get_setting("workbench.mascot.renderMode");
    ASSERT_NE(mode_def, nullptr);
    EXPECT_EQ(mode_def->type, SettingType::Enum);
    EXPECT_EQ(mode_def->defaultValue.as_string(), "default");
    EXPECT_EQ(service.get<std::string>("workbench.mascot.renderMode"), "default");

    // Check enum options
    ASSERT_EQ(mode_def->enum_values.size(), 2);
    EXPECT_EQ(mode_def->enum_values[0].value, "default");
    EXPECT_EQ(mode_def->enum_values[1].value, "ascii");

    // 2. PubSub event notification on renderMode change
    int mode_events = 0;
    std::string latest_mode;
    static_cast<void>(service.subscribe("workbench.mascot.renderMode", [&](const SettingsChangedEvent& e) {
        mode_events++;
        latest_mode = e.new_value.as_string();
    }));

    service.set("workbench.mascot.renderMode", std::string("ascii"), SettingsScope::User);
    EXPECT_EQ(service.get<std::string>("workbench.mascot.renderMode"), "ascii");
    EXPECT_EQ(mode_events, 1);
    EXPECT_EQ(latest_mode, "ascii");

    service.set("workbench.mascot.renderMode", std::string("default"), SettingsScope::User);
    EXPECT_EQ(service.get<std::string>("workbench.mascot.renderMode"), "default");
    EXPECT_EQ(mode_events, 2);
    EXPECT_EQ(latest_mode, "default");

    // 3. Test dedicated SettingsWindow row for workbench.mascot.renderMode
    Zenvra::UI::Settings::SettingsWindow win;
    const auto layout = win.calculate_layout(900.0F, 700.0F);
    bool render_mode_row_found = false;
    for (const auto& row : layout.rows) {
        if (row.def && row.def->id == "workbench.mascot.renderMode") {
            render_mode_row_found = true;
            EXPECT_FALSE(row.switch_default_btn_bounds.is_empty());
            EXPECT_FALSE(row.switch_ascii_btn_bounds.is_empty());

            Zenvra::UI::Settings::SettingRowLayout interactive_row = row;
            EXPECT_TRUE(interactive_row.handle_pointer_move(row.switch_default_btn_bounds.x + 5.0F, row.switch_default_btn_bounds.y + 5.0F));
            EXPECT_TRUE(interactive_row.is_switch_default_hovered);

            EXPECT_TRUE(interactive_row.handle_pointer_move(row.switch_ascii_btn_bounds.x + 5.0F, row.switch_ascii_btn_bounds.y + 5.0F));
            EXPECT_TRUE(interactive_row.is_switch_ascii_hovered);

            EXPECT_TRUE(interactive_row.handle_pointer_press(
                row.switch_ascii_btn_bounds.x + 5.0F, row.switch_ascii_btn_bounds.y + 5.0F,
                *row.def, service, SettingsScope::User));
            EXPECT_EQ(service.get<std::string>("workbench.mascot.renderMode"), "ascii");

            EXPECT_TRUE(interactive_row.handle_pointer_press(
                row.switch_default_btn_bounds.x + 5.0F, row.switch_default_btn_bounds.y + 5.0F,
                *row.def, service, SettingsScope::User));
            EXPECT_EQ(service.get<std::string>("workbench.mascot.renderMode"), "default");
            break;
        }
    }
    EXPECT_TRUE(render_mode_row_found);

    // 4. Test AsciiArtConverter unit logic with synthetic RGBA pixels
    // Create an 8x8 test pattern: top half bright white (255), bottom half dark black (0)
    constexpr int img_w = 8;
    constexpr int img_h = 8;
    std::vector<uint8_t> pixels(img_w * img_h * 4, 0);

    for (int y = 0; y < img_h; ++y) {
        for (int x = 0; x < img_w; ++x) {
            const int idx = (y * img_w + x) * 4;
            if (y < 4) {
                // Bright white
                pixels[idx] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = 255;
            } else {
                // Dark
                pixels[idx] = 0;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 0;
                pixels[idx + 3] = 255;
            }
        }
    }

    Zenvra::Utility::Ascii::AsciiConvertOptions opts;
    opts.target_width = 8;
    opts.target_height = 4;
    opts.colored = true;

    const auto art = Zenvra::Utility::Ascii::AsciiArtConverter::convert_raw_pixels(
        pixels.data(), img_w, img_h, opts);

    EXPECT_TRUE(art.is_valid());
    EXPECT_EQ(art.width, 8);
    EXPECT_EQ(art.height, 4);
    EXPECT_EQ(art.cells.size(), 32);

    // Top row should be dense/bright character (e.g. '@' or '#')
    const auto* top_cell = art.cell_at(0, 0);
    ASSERT_NE(top_cell, nullptr);
    EXPECT_NE(top_cell->character, ' ');
    EXPECT_EQ(top_cell->r, 255);
    EXPECT_EQ(top_cell->g, 255);
    EXPECT_EQ(top_cell->b, 255);

    // Bottom row should be dark character (e.g. ' ' or '.')
    const auto* bottom_cell = art.cell_at(0, 3);
    ASSERT_NE(bottom_cell, nullptr);
    EXPECT_EQ(bottom_cell->r, 0);
    EXPECT_EQ(bottom_cell->g, 0);
    EXPECT_EQ(bottom_cell->b, 0);

    // Formatted plain text should contain newlines separating 4 rows
    const std::string text = Zenvra::Utility::Ascii::AsciiArtConverter::to_plain_string(art);
    EXPECT_FALSE(text.empty());
    int line_breaks = 0;
    for (char c : text) {
        if (c == '\n') line_breaks++;
    }
    EXPECT_EQ(line_breaks, 3);
}
