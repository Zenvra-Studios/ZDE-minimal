#pragma once

#include "UI/Theme/StudioTheme.h"
#include "UI/Theme/ThemeDefinition.h"

#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Plugins
{
class Plugin;
}

namespace Zenvra::UI::Theme
{

class ThemeManager
{
public:
    static ThemeManager& instance() noexcept;

    void initialize();
    [[nodiscard]] bool is_initialized() const noexcept { return m_initialized; }

    void register_theme(ThemeInfo info);
    bool unregister_theme(std::string_view theme_id);
    void unregister_themes_for_plugin(std::string_view plugin_id);

    [[nodiscard]] std::vector<ThemeInfo> get_all_themes() const;
    [[nodiscard]] std::optional<ThemeInfo> find_theme(std::string_view id_or_label) const;
    [[nodiscard]] StudioTheme get_theme(std::string_view id_or_label) const;
    [[nodiscard]] StudioTheme get_current_theme() const;

    void set_current_theme(std::string_view id_or_label);

    [[nodiscard]] static Color parse_color(std::string_view color_str, Color fallback = {0, 0, 0, 255}) noexcept;
    [[nodiscard]] static std::optional<StudioTheme> parse_theme_json(
        const nlohmann::json& j,
        StudioTheme fallback = StudioTheme::zenvra_dark());
    [[nodiscard]] static std::optional<StudioTheme> load_theme_from_file(
        const std::filesystem::path& file_path,
        StudioTheme fallback = StudioTheme::zenvra_dark());

    void scan_and_register_plugin_themes(const Plugins::Plugin& plugin);
    void refresh_from_plugins();

    void sync_with_settings_service();

private:
    ThemeManager() = default;
    ~ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void register_builtin_themes();

    mutable std::recursive_mutex m_mutex;
    std::vector<ThemeInfo> m_themes;
    bool m_initialized{false};
};

} // namespace Zenvra::UI::Theme
