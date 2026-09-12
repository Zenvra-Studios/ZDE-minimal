#include "UI/Theme/StudioTheme.h"
#include "UI/Theme/ThemeDefinition.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Editor/StudioEditorModel.h"
#include "Plugins/Plugin.h"
#include "Plugins/PluginManifest.h"
#include "Plugins/PluginManager.h"
#include "Plugins/Marketplace/MarketplaceClient.h"
#include "Settings/SettingsService.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

using namespace Zenvra;
using namespace Zenvra::UI::Theme;

class ThemeTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_temp_dir = std::filesystem::temp_directory_path() / "zde_theme_unit_tests";
        std::error_code ec;
        std::filesystem::remove_all(m_temp_dir, ec);
        std::filesystem::create_directories(m_temp_dir, ec);

        ThemeManager::instance().initialize();
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_temp_dir, ec);
    }

    std::filesystem::path m_temp_dir;
};

TEST_F(ThemeTests, BuiltinThemesRegistered)
{
    auto& tm = ThemeManager::instance();

    // 1. Zenvra Dark (Canonical + Alias)
    auto dark = tm.find_theme("Zenvra Dark");
    ASSERT_TRUE(dark.has_value());
    EXPECT_TRUE(dark->is_builtin);
    EXPECT_EQ(dark->theme_data.window_background.red, 30);
    EXPECT_EQ(dark->theme_data.window_background.green, 31);
    EXPECT_EQ(dark->theme_data.window_background.blue, 34);
    EXPECT_TRUE(dark->theme_data.is_dark);
    EXPECT_FALSE(dark->theme_data.enable_os_blur);

    auto dark_alias = tm.find_theme("Dark");
    ASSERT_TRUE(dark_alias.has_value());
    EXPECT_EQ(dark_alias->label, "Zenvra Dark");

    // 2. Zenvra Light (Canonical + Alias)
    auto light = tm.find_theme("Zenvra Light");
    ASSERT_TRUE(light.has_value());
    EXPECT_TRUE(light->is_builtin);
    EXPECT_EQ(light->theme_data.window_background.red, 255);
    EXPECT_EQ(light->theme_data.window_background.green, 255);
    EXPECT_EQ(light->theme_data.window_background.blue, 255);
    EXPECT_FALSE(light->theme_data.is_dark);
    EXPECT_FALSE(light->theme_data.enable_os_blur);

    auto light_alias = tm.find_theme("Light");
    ASSERT_TRUE(light_alias.has_value());
    EXPECT_EQ(light_alias->label, "Zenvra Light");

    // 3. Zenvra Dark Modern (Solid)
    auto dark_mod = tm.find_theme("Zenvra Dark Modern");
    ASSERT_TRUE(dark_mod.has_value());
    EXPECT_TRUE(dark_mod->is_builtin);
    EXPECT_TRUE(dark_mod->theme_data.is_dark);
    EXPECT_TRUE(dark_mod->theme_data.is_modern);
    EXPECT_FALSE(dark_mod->theme_data.enable_os_blur);
    EXPECT_EQ(dark_mod->theme_data.backdrop_effect, BackdropEffect::None);
    EXPECT_EQ(dark_mod->theme_data.titlebar_background.alpha, 255);

    // 4. Zenvra Light Modern (Solid)
    auto light_mod = tm.find_theme("Zenvra Light Modern");
    ASSERT_TRUE(light_mod.has_value());
    EXPECT_TRUE(light_mod->is_builtin);
    EXPECT_FALSE(light_mod->theme_data.is_dark);
    EXPECT_TRUE(light_mod->theme_data.is_modern);
    EXPECT_FALSE(light_mod->theme_data.enable_os_blur);
    EXPECT_EQ(light_mod->theme_data.backdrop_effect, BackdropEffect::None);
    EXPECT_EQ(light_mod->theme_data.titlebar_background.alpha, 255);

    // 5. High Contrast
    auto hc = tm.find_theme("High Contrast");
    ASSERT_TRUE(hc.has_value());
    EXPECT_TRUE(hc->is_builtin);
    EXPECT_EQ(hc->theme_data.window_background.red, 0);
    EXPECT_EQ(hc->theme_data.window_background.green, 0);
    EXPECT_EQ(hc->theme_data.window_background.blue, 0);

    // Non-existent theme fallback
    StudioTheme fallback = tm.get_theme("NonExistentThemeXYZ");
    EXPECT_EQ(fallback.window_background, StudioTheme::zenvra_dark().window_background);
}

TEST_F(ThemeTests, ModernThemesAndEditorPalette)
{
    // Light palette test
    auto light_theme = StudioTheme::zenvra_light();
    EXPECT_FALSE(light_theme.is_modern);
    auto light_palette = UI::Editor::StudioEditorPalette::from_theme(light_theme);
    EXPECT_FALSE(light_palette.is_modern);
    EXPECT_EQ(light_palette.workspace_background.red, 255);
    EXPECT_EQ(light_palette.workspace_background.green, 255);
    EXPECT_EQ(light_palette.workspace_background.blue, 255);
    EXPECT_EQ(light_palette.workspace_background.alpha, 255);
    EXPECT_LT(light_palette.text_primary.red, 50); // Dark text for light mode

    // Light Modern palette test
    auto light_mod_theme = StudioTheme::zenvra_light_modern();
    EXPECT_TRUE(light_mod_theme.is_modern);
    EXPECT_FALSE(light_mod_theme.enable_os_blur);
    EXPECT_EQ(light_mod_theme.titlebar_background.alpha, 255); // Solid titlebar
    EXPECT_EQ(light_mod_theme.window_background.alpha, 255);   // Solid window backdrop
    EXPECT_EQ(light_mod_theme.titlebar_border.alpha, 255);     // Solid border
    auto light_mod_palette = UI::Editor::StudioEditorPalette::from_theme(light_mod_theme);
    EXPECT_TRUE(light_mod_palette.is_modern);
    EXPECT_EQ(light_mod_palette.border.alpha, 255);               // Solid border

    // Dark Modern palette test
    auto dark_mod_theme = StudioTheme::zenvra_dark_modern();
    EXPECT_TRUE(dark_mod_theme.is_modern);
    EXPECT_FALSE(dark_mod_theme.enable_os_blur);
    EXPECT_EQ(dark_mod_theme.titlebar_background.alpha, 255); // Solid titlebar
    EXPECT_EQ(dark_mod_theme.window_background.alpha, 255);   // Solid window backdrop
    EXPECT_EQ(dark_mod_theme.titlebar_border.alpha, 255);     // Solid border
    auto dark_mod_palette = UI::Editor::StudioEditorPalette::from_theme(dark_mod_theme);
    EXPECT_TRUE(dark_mod_palette.is_modern);
    EXPECT_EQ(dark_mod_palette.workspace_background.alpha, 255); // Solid workspace
    EXPECT_EQ(dark_mod_palette.border.alpha, 255);               // Solid border
}

TEST_F(ThemeTests, HexColorParsing)
{
    // 6-digit hex
    Color c1 = ThemeManager::parse_color("#1e1f22");
    EXPECT_EQ(c1.red, 0x1e);
    EXPECT_EQ(c1.green, 0x1f);
    EXPECT_EQ(c1.blue, 0x22);
    EXPECT_EQ(c1.alpha, 255);

    // 8-digit hex (with alpha)
    Color c2 = ThemeManager::parse_color("#1e1f2280");
    EXPECT_EQ(c2.red, 0x1e);
    EXPECT_EQ(c2.green, 0x1f);
    EXPECT_EQ(c2.blue, 0x22);
    EXPECT_EQ(c2.alpha, 0x80);

    // 3-digit shorthand #RGB -> #RRGGBB
    Color c3 = ThemeManager::parse_color("#abc");
    EXPECT_EQ(c3.red, 0xaa);
    EXPECT_EQ(c3.green, 0xbb);
    EXPECT_EQ(c3.blue, 0xcc);
    EXPECT_EQ(c3.alpha, 255);

    // Fallback on invalid
    Color fallback{1, 2, 3, 4};
    Color c4 = ThemeManager::parse_color("invalid-color", fallback);
    EXPECT_EQ(c4.red, 1);
    EXPECT_EQ(c4.green, 2);
    EXPECT_EQ(c4.blue, 3);
    EXPECT_EQ(c4.alpha, 4);
}

TEST_F(ThemeTests, VSCodeThemeJsonParsing)
{
    nlohmann::json vscode_json = {
        {"name", "VSCode Test Theme"},
        {"type", "dark"},
        {"colors", {
            {"editor.background", "#282c34"},
            {"editor.foreground", "#abb2bf"},
            {"titleBar.activeBackground", "#21252b"},
            {"titleBar.border", "#181a1f"},
            {"sideBar.background", "#21252b"},
            {"activityBarBadge.background", "#61afef"},
            {"list.hoverBackground", "#2c313a"},
            {"list.activeSelectionBackground", "#3a3f4b"},
            {"commandCenter.background", "#2c313a"},
            {"commandCenter.border", "#3a3f4b"}
        }}
    };

    auto parsed = ThemeManager::parse_theme_json(vscode_json);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_EQ(parsed->window_background.red, 0x28);
    EXPECT_EQ(parsed->window_background.green, 0x2c);
    EXPECT_EQ(parsed->window_background.blue, 0x34);

    EXPECT_EQ(parsed->text_primary.red, 0xab);
    EXPECT_EQ(parsed->text_primary.green, 0xb2);
    EXPECT_EQ(parsed->text_primary.blue, 0xbf);

    EXPECT_EQ(parsed->titlebar_background.red, 0x21);
    EXPECT_EQ(parsed->titlebar_background.green, 0x25);
    EXPECT_EQ(parsed->titlebar_background.blue, 0x2b);

    EXPECT_EQ(parsed->accent.red, 0x61);
    EXPECT_EQ(parsed->accent.green, 0xaf);
    EXPECT_EQ(parsed->accent.blue, 0xef);
}

TEST_F(ThemeTests, PluginContributedThemeAndSettingsIntegration)
{
    // Create a mock VS Code extension directory structure on disk
    std::filesystem::path plugin_dir = m_temp_dir / "theme-onedarkpro";
    std::filesystem::path themes_dir = plugin_dir / "themes";
    std::filesystem::create_directories(themes_dir);

    std::filesystem::path theme_file = themes_dir / "OneDark-Pro.json";
    {
        std::ofstream out(theme_file);
        out << R"({
            "name": "One Dark Pro",
            "type": "dark",
            "colors": {
                "editor.background": "#282c34",
                "editor.foreground": "#abb2bf",
                "titleBar.activeBackground": "#21252b",
                "sideBar.background": "#21252b",
                "activityBarBadge.background": "#61afef"
            }
        })";
    }

    std::filesystem::path pkg_file = plugin_dir / "package.json";
    {
        std::ofstream out(pkg_file);
        out << R"({
            "name": "theme-onedarkpro",
            "publisher": "binaryify",
            "version": "1.0.0",
            "description": "Atom's iconic One Dark theme",
            "contributes": {
                "themes": [
                    {
                        "id": "one-dark-pro",
                        "label": "One Dark Pro",
                        "uiTheme": "vs-dark",
                        "path": "./themes/OneDark-Pro.json"
                    }
                ]
            }
        })";
    }

    auto manifest = Plugins::PluginManifest::from_file(pkg_file);
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->get_id(), "binaryify.theme-onedarkpro");

    auto plugin = std::make_shared<Plugins::Plugin>(*manifest, plugin_dir, Plugins::PluginSource::Local);
    plugin->set_enabled(true);

    auto& tm = ThemeManager::instance();
    tm.scan_and_register_plugin_themes(*plugin);

    // 1. Verify theme is registered in ThemeManager
    auto found = tm.find_theme("One Dark Pro");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->label, "One Dark Pro");
    EXPECT_EQ(found->plugin_id, "binaryify.theme-onedarkpro");
    EXPECT_EQ(found->theme_data.window_background.red, 0x28);

    // 2. Verify theme is synced to SettingsService "theme.current"
    auto& settings = Settings::SettingsService::instance();
    const auto* theme_def = settings.get_schema().get_setting("theme.current");
    ASSERT_NE(theme_def, nullptr);

    bool option_exists = false;
    for (const auto& opt : theme_def->enum_values)
    {
        if (opt.value == "One Dark Pro")
        {
            option_exists = true;
            break;
        }
    }
    EXPECT_TRUE(option_exists);

    // 3. Verify user can set "theme.current" to the installed theme
    settings.set("theme.current", "One Dark Pro");
    EXPECT_EQ(settings.get<std::string>("theme.current"), "One Dark Pro");

    // 4. Verify ThemeManager::get_current_theme() returns the active installed theme
    StudioTheme active_theme = tm.get_current_theme();
    EXPECT_EQ(active_theme.window_background.red, 0x28);
    EXPECT_EQ(active_theme.window_background.green, 0x2c);
    EXPECT_EQ(active_theme.window_background.blue, 0x34);

    // 5. Unregister plugin themes (simulating plugin uninstallation / disabling)
    tm.unregister_themes_for_plugin("binaryify.theme-onedarkpro");
    EXPECT_FALSE(tm.find_theme("One Dark Pro").has_value());

    // 6. Verify Settings enum values no longer contain the uninstalled theme
    const auto* updated_def = settings.get_schema().get_setting("theme.current");
    ASSERT_NE(updated_def, nullptr);
    bool still_exists = false;
    for (const auto& opt : updated_def->enum_values)
    {
        if (opt.value == "One Dark Pro")
        {
            still_exists = true;
            break;
        }
    }
    EXPECT_FALSE(still_exists);

    // Reset setting back to Dark
    settings.set("theme.current", "Dark");
}

TEST_F(ThemeTests, MarketplaceThemeCatalogEntries)
{
    Plugins::Marketplace::MarketplaceClient marketplace;
    marketplace.ensure_default_catalog_if_empty();

    auto all_entries = marketplace.get_all_entries();
    ASSERT_FALSE(all_entries.empty());

    // Search by category "themes"
    auto theme_entries = marketplace.search("themes");
    ASSERT_FALSE(theme_entries.empty());

    // Verify presence of popular theme plugins from GitHub
    auto one_dark = marketplace.find_entry("zde.theme.one-dark-pro");
    ASSERT_TRUE(one_dark.has_value());
    EXPECT_EQ(one_dark->category, "themes");
    EXPECT_FALSE(one_dark->repository_url.empty());

    auto dracula = marketplace.find_entry("zde.theme.dracula");
    ASSERT_TRUE(dracula.has_value());
    EXPECT_EQ(dracula->category, "themes");
    EXPECT_FALSE(dracula->repository_url.empty());

    auto catppuccin = marketplace.find_entry("zde.theme.catppuccin");
    ASSERT_TRUE(catppuccin.has_value());
    EXPECT_EQ(catppuccin->category, "themes");
}

TEST_F(ThemeTests, ThemeSwitchingSolidAndModern)
{
    auto& tm = ThemeManager::instance();
    auto& settings = Settings::SettingsService::instance();

    // 1. Default when empty is Modern
    settings.set("theme.current", "");
    StudioTheme def_theme = tm.get_current_theme();
    EXPECT_TRUE(def_theme.is_modern);
    EXPECT_FALSE(def_theme.enable_os_blur);

    // 2. Explicit Modern Dark
    settings.set("theme.current", "Zenvra Dark Modern");
    StudioTheme modern_theme = tm.get_current_theme();
    EXPECT_TRUE(modern_theme.is_modern);
    EXPECT_FALSE(modern_theme.enable_os_blur);
    EXPECT_EQ(modern_theme.backdrop_effect, BackdropEffect::None);

    // 3. Switch to Solid/Old Dark ("Zenvra Dark")
    settings.set("theme.current", "Zenvra Dark");
    StudioTheme solid_theme = tm.get_current_theme();
    EXPECT_FALSE(solid_theme.is_modern);
    EXPECT_FALSE(solid_theme.enable_os_blur);
    EXPECT_EQ(solid_theme.backdrop_effect, BackdropEffect::None);
    EXPECT_EQ(solid_theme.window_background.red, 30);

    // 4. Aliases for Solid Dark (Dark, Old, Solid, Classic)
    settings.set("theme.current", "Dark");
    EXPECT_FALSE(tm.get_current_theme().is_modern);
    settings.set("theme.current", "Old");
    EXPECT_FALSE(tm.get_current_theme().is_modern);
    settings.set("theme.current", "Solid");
    EXPECT_FALSE(tm.get_current_theme().is_modern);
    settings.set("theme.current", "Classic");
    EXPECT_FALSE(tm.get_current_theme().is_modern);

    // 5. Solid Light and Modern Light
    settings.set("theme.current", "Zenvra Light");
    StudioTheme solid_light = tm.get_current_theme();
    EXPECT_FALSE(solid_light.is_modern);
    EXPECT_FALSE(solid_light.enable_os_blur);

    settings.set("theme.current", "Zenvra Light Modern");
    StudioTheme modern_light = tm.get_current_theme();
    EXPECT_TRUE(modern_light.is_modern);
    EXPECT_FALSE(modern_light.enable_os_blur);

    // Reset back to user's setting or default
    settings.set("theme.current", "Zenvra Dark");
}

