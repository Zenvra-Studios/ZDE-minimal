#include "UI/Theme/ThemeManager.h"
#include "Plugins/Plugin.h"
#include "Plugins/PluginManager.h"
#include "Settings/SettingsService.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>

namespace Zenvra::UI::Theme
{

namespace
{

int parse_hex_digit(char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string trim_string(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
    {
        s.remove_suffix(1);
    }
    return std::string(s);
}

} // namespace

ThemeManager& ThemeManager::instance() noexcept
{
    static ThemeManager s_instance;
    return s_instance;
}

void ThemeManager::initialize()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return;

    register_builtin_themes();
    m_initialized = true;

    // Listen to plugin lifecycle events to discover/unregister themes automatically
    Plugins::PluginManager::instance().register_lifecycle_listener(
        [this](std::shared_ptr<Plugins::Plugin> plugin, bool is_activated) {
            if (!plugin) return;
            if (is_activated)
            {
                scan_and_register_plugin_themes(*plugin);
            }
            else
            {
                unregister_themes_for_plugin(plugin->get_id());
            }
        }
    );

    refresh_from_plugins();
    sync_with_settings_service();
}

void ThemeManager::register_builtin_themes()
{
    // 1. Zenvra Dark (Default)
    ThemeInfo dark_info{
        .id = "zenvra-dark",
        .label = "Zenvra Dark",
        .plugin_id = "",
        .file_path = {},
        .is_builtin = true,
        .theme_data = StudioTheme::zenvra_dark()
    };
    m_themes.push_back(std::move(dark_info));

    // 2. Zenvra Light
    ThemeInfo light_info{
        .id = "zenvra-light",
        .label = "Zenvra Light",
        .plugin_id = "",
        .file_path = {},
        .is_builtin = true,
        .theme_data = StudioTheme::zenvra_light()
    };
    m_themes.push_back(std::move(light_info));

    // 3. Zenvra Dark Modern (OS Blur)
    ThemeInfo dark_mod_info{
        .id = "zenvra-dark-modern",
        .label = "Zenvra Dark Modern",
        .plugin_id = "",
        .file_path = {},
        .is_builtin = true,
        .theme_data = StudioTheme::zenvra_dark_modern()
    };
    m_themes.push_back(std::move(dark_mod_info));

    // 4. Zenvra Light Modern (OS Blur)
    ThemeInfo light_mod_info{
        .id = "zenvra-light-modern",
        .label = "Zenvra Light Modern",
        .plugin_id = "",
        .file_path = {},
        .is_builtin = true,
        .theme_data = StudioTheme::zenvra_light_modern()
    };
    m_themes.push_back(std::move(light_mod_info));

    // 5. High Contrast Theme
    ThemeInfo hc_info{
        .id = "high-contrast",
        .label = "High Contrast",
        .plugin_id = "",
        .file_path = {},
        .is_builtin = true,
        .theme_data = StudioTheme::high_contrast()
    };
    m_themes.push_back(std::move(hc_info));
}

void ThemeManager::register_theme(ThemeInfo info)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& existing : m_themes)
    {
        if (existing.id == info.id || existing.label == info.label)
        {
            existing = std::move(info);
            sync_with_settings_service();
            return;
        }
    }
    m_themes.push_back(std::move(info));
    sync_with_settings_service();
}

bool ThemeManager::unregister_theme(std::string_view theme_id)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const auto it = std::find_if(m_themes.begin(), m_themes.end(), [&](const ThemeInfo& t) {
        return !t.is_builtin && (t.id == theme_id || t.label == theme_id);
    });

    if (it != m_themes.end())
    {
        m_themes.erase(it);
        sync_with_settings_service();
        return true;
    }
    return false;
}

void ThemeManager::unregister_themes_for_plugin(std::string_view plugin_id)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (plugin_id.empty()) return;

    const auto prev_size = m_themes.size();
    std::erase_if(m_themes, [&](const ThemeInfo& t) {
        return !t.is_builtin && t.plugin_id == plugin_id;
    });

    if (m_themes.size() != prev_size)
    {
        sync_with_settings_service();
    }
}

std::vector<ThemeInfo> ThemeManager::get_all_themes() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_themes;
}

std::optional<ThemeInfo> ThemeManager::find_theme(std::string_view id_or_label) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& t : m_themes)
    {
        if (t.id == id_or_label || t.label == id_or_label)
        {
            return t;
        }
    }

    // Backwards-compatible aliases
    if (id_or_label == "Dark" || id_or_label == "dark" ||
        id_or_label == "Old" || id_or_label == "old" ||
        id_or_label == "Classic" || id_or_label == "classic" ||
        id_or_label == "Solid" || id_or_label == "solid" ||
        id_or_label == "Dark Old" || id_or_label == "Dark Classic" ||
        id_or_label == "Dark Solid" || id_or_label == "zenvra-dark-classic" ||
        id_or_label == "Zenvra Dark (Classic)" || id_or_label == "Zenvra Dark (Solid)" ||
        id_or_label == "Zenvra Dark (Old)")
    {
        for (const auto& t : m_themes)
        {
            if (t.id == "zenvra-dark" || t.label == "Zenvra Dark") return t;
        }
    }
    if (id_or_label == "Light" || id_or_label == "light" ||
        id_or_label == "Light Old" || id_or_label == "Light Classic" ||
        id_or_label == "Light Solid" || id_or_label == "zenvra-light-classic" ||
        id_or_label == "Zenvra Light (Classic)" || id_or_label == "Zenvra Light (Solid)" ||
        id_or_label == "Zenvra Light (Old)")
    {
        for (const auto& t : m_themes)
        {
            if (t.id == "zenvra-light" || t.label == "Zenvra Light") return t;
        }
    }
    if (id_or_label == "Modern" || id_or_label == "modern" ||
        id_or_label == "Dark Modern" || id_or_label == "dark modern")
    {
        for (const auto& t : m_themes)
        {
            if (t.id == "zenvra-dark-modern" || t.label == "Zenvra Dark Modern") return t;
        }
    }
    if (id_or_label == "Light Modern" || id_or_label == "light modern")
    {
        for (const auto& t : m_themes)
        {
            if (t.id == "zenvra-light-modern" || t.label == "Zenvra Light Modern") return t;
        }
    }

    // Case-insensitive fallback
    auto to_lower = [](std::string_view sv) {
        std::string res;
        res.reserve(sv.size());
        for (char c : sv) res += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return res;
    };
    const std::string lower_target = to_lower(id_or_label);
    for (const auto& t : m_themes)
    {
        if (to_lower(t.id) == lower_target || to_lower(t.label) == lower_target)
        {
            return t;
        }
    }

    return std::nullopt;
}

StudioTheme ThemeManager::get_theme(std::string_view id_or_label) const
{
    if (auto t = find_theme(id_or_label))
    {
        return t->theme_data;
    }
    return StudioTheme::zenvra_dark();
}

StudioTheme ThemeManager::get_current_theme() const
{
    const std::string active_name = Settings::SettingsService::instance().get<std::string>("theme.current");
    if (active_name.empty() || active_name == "Zenvra Dark Modern" || active_name == "Dark Modern" ||
        active_name == "Modern" || active_name == "zenvra-dark-modern")
    {
        return StudioTheme::zenvra_dark_modern();
    }
    return get_theme(active_name);
}

void ThemeManager::set_current_theme(std::string_view id_or_label)
{
    std::string target(id_or_label);
    if (auto t = find_theme(id_or_label))
    {
        target = t->label;
    }
    Settings::SettingsService::instance().set("theme.current", target);
}

Color ThemeManager::parse_color(std::string_view color_str, Color fallback) noexcept
{
    std::string s = trim_string(color_str);
    if (s.empty()) return fallback;

    if (s.front() == '#')
    {
        s.erase(0, 1);
    }

    if (s.size() == 3)
    {
        // #RGB
        int r = parse_hex_digit(s[0]);
        int g = parse_hex_digit(s[1]);
        int b = parse_hex_digit(s[2]);
        if (r < 0 || g < 0 || b < 0) return fallback;
        return Color{static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17), static_cast<uint8_t>(b * 17), 255};
    }

    if (s.size() == 4)
    {
        // #RGBA
        int r = parse_hex_digit(s[0]);
        int g = parse_hex_digit(s[1]);
        int b = parse_hex_digit(s[2]);
        int a = parse_hex_digit(s[3]);
        if (r < 0 || g < 0 || b < 0 || a < 0) return fallback;
        return Color{static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17), static_cast<uint8_t>(b * 17), static_cast<uint8_t>(a * 17)};
    }

    if (s.size() == 6)
    {
        // #RRGGBB
        unsigned int val = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + 6, val, 16);
        if (ec != std::errc{}) return fallback;
        return Color{
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF),
            255
        };
    }

    if (s.size() == 8)
    {
        // #RRGGBBAA
        unsigned int val = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + 8, val, 16);
        if (ec != std::errc{}) return fallback;
        return Color{
            static_cast<uint8_t>((val >> 24) & 0xFF),
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF)
        };
    }

    return fallback;
}

std::optional<StudioTheme> ThemeManager::parse_theme_json(
    const nlohmann::json& j,
    StudioTheme fallback)
{
    if (!j.is_object()) return std::nullopt;

    StudioTheme theme = fallback;

    if (j.contains("type") && j["type"].is_string())
    {
        const std::string type_str = j["type"].get<std::string>();
        if (type_str == "light") theme.is_dark = false;
        else if (type_str == "dark" || type_str == "hc") theme.is_dark = true;
    }
    if (j.contains("uiTheme") && j["uiTheme"].is_string())
    {
        const std::string ui_theme = j["uiTheme"].get<std::string>();
        if (ui_theme == "vs") theme.is_dark = false;
        else if (ui_theme == "vs-dark" || ui_theme == "hc-black") theme.is_dark = true;
    }

    const nlohmann::json* colors = nullptr;
    if (j.contains("colors") && j["colors"].is_object())
    {
        colors = &j["colors"];
    }

    auto get_color_str = [&](std::string_view key) -> std::optional<std::string> {
        if (colors && colors->contains(key) && (*colors)[key].is_string())
        {
            return (*colors)[key].get<std::string>();
        }
        if (j.contains(key) && j[key].is_string())
        {
            return j[key].get<std::string>();
        }
        return std::nullopt;
    };

    auto apply_first_match = [&](std::initializer_list<std::string_view> keys, Color& target) {
        for (auto k : keys)
        {
            if (auto val = get_color_str(k))
            {
                target = parse_color(*val, target);
                return;
            }
        }
    };

    // 1. Window Background
    apply_first_match({"editor.background", "workbench.background", "window_background"}, theme.window_background);

    // 2. Titlebar Background
    apply_first_match({"titleBar.activeBackground", "titlebar_background"}, theme.titlebar_background);

    // 3. Titlebar Border
    apply_first_match({"titleBar.border", "titlebar_border"}, theme.titlebar_border);

    // 4. Panel Background
    apply_first_match({"sideBar.background", "panel.background", "panel_background"}, theme.panel_background);

    // 5. Primary Text
    apply_first_match({"editor.foreground", "foreground", "text_primary"}, theme.text_primary);

    // 6. Secondary Text
    apply_first_match({"descriptionForeground", "sideBarTitle.foreground", "text_secondary"}, theme.text_secondary);

    // 7. Accent
    apply_first_match({"activityBarBadge.background", "focusBorder", "accent"}, theme.accent);

    // 8. Hover
    apply_first_match({"list.hoverBackground", "hover"}, theme.hover);

    // 9. Pressed
    apply_first_match({"list.activeSelectionBackground", "pressed"}, theme.pressed);

    // 10. Command Center
    apply_first_match({"commandCenter.background", "command_center_background"}, theme.command_center_background);
    apply_first_match({"commandCenter.border", "command_center_border"}, theme.command_center_border);

    // 11. Close Hover
    apply_first_match({"close_hover"}, theme.close_hover);

    return theme;
}

std::optional<StudioTheme> ThemeManager::load_theme_from_file(
    const std::filesystem::path& file_path,
    StudioTheme fallback)
{
    std::ifstream file(file_path);
    if (!file.is_open()) return std::nullopt;

    try
    {
        nlohmann::json j;
        file >> j;
        return parse_theme_json(j, fallback);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

void ThemeManager::scan_and_register_plugin_themes(const Plugins::Plugin& plugin)
{
    const auto& install_dir = plugin.get_install_path();
    std::error_code ec;
    if (!std::filesystem::exists(install_dir, ec)) return;

    // Check package.json or plugin.json for contributes.themes
    std::filesystem::path manifest_files[] = {
        install_dir / "package.json",
        install_dir / "plugin.json"
    };

    bool found_contributes = false;

    for (const auto& mf_path : manifest_files)
    {
        if (std::filesystem::exists(mf_path, ec))
        {
            try
            {
                std::ifstream ifs(mf_path);
                if (ifs.is_open())
                {
                    nlohmann::json j;
                    ifs >> j;
                    const nlohmann::json* themes_arr = nullptr;
                    if (j.contains("contributes") && j["contributes"].is_object() &&
                        j["contributes"].contains("themes") && j["contributes"]["themes"].is_array())
                    {
                        themes_arr = &j["contributes"]["themes"];
                    }
                    else if (j.contains("themes") && j["themes"].is_array())
                    {
                        themes_arr = &j["themes"];
                    }

                    if (themes_arr)
                    {
                        for (const auto& t_entry : *themes_arr)
                        {
                            if (!t_entry.is_object()) continue;

                            std::string label = t_entry.value("label", t_entry.value("name", ""));
                            std::string id = t_entry.value("id", label);
                            std::string path_str = t_entry.value("path", "");

                            if (label.empty()) label = plugin.get_name();
                            if (id.empty()) id = plugin.get_id();

                            std::filesystem::path theme_file = install_dir / path_str;
                            if (auto parsed = load_theme_from_file(theme_file))
                            {
                                ThemeInfo tinfo{
                                    .id = plugin.get_id() + "." + id,
                                    .label = label,
                                    .plugin_id = plugin.get_id(),
                                    .file_path = theme_file,
                                    .is_builtin = false,
                                    .theme_data = *parsed
                                };
                                register_theme(std::move(tinfo));
                                found_contributes = true;
                            }
                        }
                    }
                }
            }
            catch (...) {}
        }
    }

    // Also discover any theme files directly via plugin.get_theme_files()
    for (const auto& tf : plugin.get_theme_files())
    {
        if (auto parsed = load_theme_from_file(tf))
        {
            std::string stem = tf.stem().string();
            std::string label = plugin.get_name();
            if (stem != "theme" && stem != "package" && stem != "plugin")
            {
                label = stem;
            }

            ThemeInfo tinfo{
                .id = plugin.get_id() + "." + stem,
                .label = label,
                .plugin_id = plugin.get_id(),
                .file_path = tf,
                .is_builtin = false,
                .theme_data = *parsed
            };
            register_theme(std::move(tinfo));
        }
    }
}

void ThemeManager::refresh_from_plugins()
{
    auto& pm = Plugins::PluginManager::instance();
    for (const auto& p : pm.get_all_plugins())
    {
        if (p && p->is_enabled())
        {
            scan_and_register_plugin_themes(*p);
        }
    }
}

void ThemeManager::sync_with_settings_service()
{
    auto& service = Settings::SettingsService::instance();
    auto& schema = service.get_schema();

    std::vector<Settings::SettingOption> options;

    // Ensure built-in options are always present in canonical order
    options.push_back({.label = "Zenvra Dark", .value = "Zenvra Dark"});
    options.push_back({.label = "Zenvra Light", .value = "Zenvra Light"});
    options.push_back({.label = "Zenvra Dark Modern", .value = "Zenvra Dark Modern"});
    options.push_back({.label = "Zenvra Light Modern", .value = "Zenvra Light Modern"});
    options.push_back({.label = "High Contrast", .value = "High Contrast"});

    // Add all registered themes
    for (const auto& t : m_themes)
    {
        if (t.is_builtin) continue;
        bool already = false;
        for (const auto& opt : options)
        {
            if (opt.value == t.label)
            {
                already = true;
                break;
            }
        }
        if (!already)
        {
            options.push_back({.label = t.label, .value = t.label});
        }
    }

    Settings::SettingDefinition theme_def{
        .id = "theme.current",
        .title = "Color Theme",
        .description = "Specifies the active color theme applied to the studio interface.",
        .type = Settings::SettingType::Enum,
        .defaultValue = "Zenvra Dark Modern",
        .category = "Appearance",
        .subcategory = "Theme",
        .tags = {"theme", "color", "dark", "light", "vscode"},
        .enum_values = std::move(options)
    };

    schema.register_setting(std::move(theme_def));
}

} // namespace Zenvra::UI::Theme
