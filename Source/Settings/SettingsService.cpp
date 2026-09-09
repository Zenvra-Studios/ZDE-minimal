#include "Settings/SettingsService.h"
#include "Platform/HostSystem.h"

#include <algorithm>

namespace Zenvra::Settings
{

SettingsService& SettingsService::instance() noexcept
{
    static SettingsService s_instance;
    return s_instance;
}

SettingsService::SettingsService()
{
    register_default_settings();
}

SettingsService::~SettingsService() = default;

std::filesystem::path SettingsService::get_default_user_settings_path()
{
    const auto home = Platform::HostSystem::get_user_home_directory();
    return home / ".zenvra" / "settings.json";
}

void SettingsService::initialize(
    const std::filesystem::path& user_settings_path,
    const std::filesystem::path& workspace_settings_path)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto u_path = user_settings_path.empty()
        ? get_default_user_settings_path()
        : user_settings_path;

    m_user_store = std::make_unique<JsonSettingsStore>(u_path);
    m_user_store->load();

    if (!workspace_settings_path.empty()) {
        m_workspace_store = std::make_unique<JsonSettingsStore>(workspace_settings_path);
        m_workspace_store->load();
    }
}

void SettingsService::set_workspace_path(const std::filesystem::path& workspace_root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (workspace_root.empty()) {
        m_workspace_store.reset();
        return;
    }

    const auto ws_settings = workspace_root / ".zenvra" / "settings.json";
    m_workspace_store = std::make_unique<JsonSettingsStore>(ws_settings);
    m_workspace_store->load();
}

void SettingsService::close_workspace()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_workspace_store.reset();
}

SettingsValue SettingsService::get(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_workspace_store && m_workspace_store->contains(id)) {
        if (auto val = m_workspace_store->get(id)) {
            return *val;
        }
    }

    if (m_user_store && m_user_store->contains(id)) {
        if (auto val = m_user_store->get(id)) {
            return *val;
        }
    }

    if (const auto* def = m_schema.get_setting(id)) {
        return def->defaultValue;
    }

    return SettingsValue();
}

void SettingsService::set(std::string_view id, SettingsValue value, SettingsScope scope)
{
    SettingsValue validated_value = value;
    static_cast<void>(m_schema.validate(id, value, &validated_value));

    SettingsValue old_value = get(id);
    if (old_value == validated_value) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (scope == SettingsScope::Workspace) {
            if (!m_workspace_store) {
                // If workspace store is not open, fallback to user store
                if (!m_user_store) {
                    m_user_store = std::make_unique<JsonSettingsStore>(get_default_user_settings_path());
                    m_user_store->load();
                }
                m_user_store->set(id, validated_value);
                scope = SettingsScope::User;
            } else {
                m_workspace_store->set(id, validated_value);
            }
        } else {
            if (!m_user_store) {
                m_user_store = std::make_unique<JsonSettingsStore>(get_default_user_settings_path());
                m_user_store->load();
            }
            m_user_store->set(id, validated_value);
        }
    }

    notify_listeners({std::string(id), old_value, validated_value, scope});
}

bool SettingsService::has(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_workspace_store && m_workspace_store->contains(id)) return true;
    if (m_user_store && m_user_store->contains(id)) return true;
    return m_schema.has_setting(id);
}

bool SettingsService::is_modified(std::string_view id, SettingsScope scope) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (scope == SettingsScope::Workspace) {
        return m_workspace_store && m_workspace_store->contains(id);
    }
    return m_user_store && m_user_store->contains(id);
}

SettingsScope SettingsService::get_effective_scope(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_workspace_store && m_workspace_store->contains(id)) {
        return SettingsScope::Workspace;
    }
    if (m_user_store && m_user_store->contains(id)) {
        return SettingsScope::User;
    }
    return SettingsScope::Default;
}

void SettingsService::reset(std::string_view id, SettingsScope scope)
{
    SettingsValue old_val = get(id);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (scope == SettingsScope::Workspace) {
            if (m_workspace_store) {
                m_workspace_store->remove(id);
            }
        } else {
            if (m_user_store) {
                m_user_store->remove(id);
            }
        }
    }

    SettingsValue new_val = get(id);
    if (old_val != new_val) {
        notify_listeners({std::string(id), old_val, new_val, scope});
    }
}

void SettingsService::reset_category(std::string_view category, SettingsScope scope)
{
    const auto settings = m_schema.get_settings_by_category(category);
    for (const auto* def : settings) {
        reset(def->id, scope);
    }
}

void SettingsService::reset_all(SettingsScope scope)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (scope == SettingsScope::Workspace) {
            if (m_workspace_store) m_workspace_store->clear();
        } else {
            if (m_user_store) m_user_store->clear();
        }
    }

    for (const auto* def : m_schema.get_all_settings()) {
        notify_listeners({def->id, def->defaultValue, def->defaultValue, scope});
    }
}

void SettingsService::register_setting(SettingDefinition definition)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_schema.register_setting(std::move(definition));
}

std::size_t SettingsService::subscribe(SettingsChangeCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t sub_id = m_next_subscription_id++;
    m_subscriptions.push_back({sub_id, std::nullopt, std::move(callback)});
    return sub_id;
}

std::size_t SettingsService::subscribe(std::string_view id, SettingsChangeCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t sub_id = m_next_subscription_id++;
    m_subscriptions.push_back({sub_id, std::string(id), std::move(callback)});
    return sub_id;
}

void SettingsService::unsubscribe(std::size_t subscription_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscriptions.erase(
        std::remove_if(
            m_subscriptions.begin(),
            m_subscriptions.end(),
            [subscription_id](const Subscription& sub) {
                return sub.id == subscription_id;
            }),
        m_subscriptions.end()
    );
}

void SettingsService::notify_listeners(const SettingsChangedEvent& event)
{
    std::vector<SettingsChangeCallback> callbacks_to_invoke;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& sub : m_subscriptions) {
            if (!sub.target_setting_id.has_value() || *sub.target_setting_id == event.id) {
                callbacks_to_invoke.push_back(sub.callback);
            }
        }
    }

    for (const auto& cb : callbacks_to_invoke) {
        if (cb) {
            cb(event);
        }
    }
}

void SettingsService::register_default_settings()
{
    // 1. Editor settings
    m_schema.register_setting({
        .id = "editor.interactionMode",
        .title = "Editor Interaction Mode",
        .description = "Controls the interaction mode for the editor: Default (standard modern IDE editing) or Vim (modal editing).",
        .type = SettingType::Enum,
        .defaultValue = "default",
        .category = "Editor",
        .subcategory = "DefaultMode",
        .tags = {"default", "vim", "mode", "modal", "vi", "standard", "interaction"},
        .enum_values = {
            {.label = "Default (Standard Mode)", .value = "default"},
            {.label = "Vim (Modal Editing)", .value = "vim"}
        },
    });

    m_schema.register_setting({
        .id = "editor.lineNumbers",
        .title = "Line Numbers",
        .description = "Controls the display of line numbers: on (standard absolute line numbers), relative (relative to cursor line), or off.",
        .type = SettingType::Enum,
        .defaultValue = "on",
        .category = "Editor",
        .subcategory = "DefaultMode",
        .tags = {"line", "numbers", "relative", "gutter", "default"},
        .enum_values = {
            {.label = "On (Absolute)", .value = "on"},
            {.label = "Relative", .value = "relative"},
            {.label = "Off", .value = "off"}
        },
    });

    m_schema.register_setting({
        .id = "vim.enabled",
        .title = "Enable Vim Mode",
        .description = "Enables Vim emulation in the editor.",
        .type = SettingType::Boolean,
        .defaultValue = false,
        .category = "Editor",
        .subcategory = "Vim",
        .tags = {"vim", "enable", "vi", "mode"},
    });

    m_schema.register_setting({
        .id = "vim.startMode",
        .title = "Vim Start Mode",
        .description = "The default mode when entering Vim (Normal or Insert).",
        .type = SettingType::Enum,
        .defaultValue = "normal",
        .category = "Editor",
        .subcategory = "Vim",
        .tags = {"vim", "start", "mode"},
        .enum_values = {
            {.label = "Normal Mode", .value = "normal"},
            {.label = "Insert Mode", .value = "insert"}
        },
    });

    m_schema.register_setting({
        .id = "vim.leaderKey",
        .title = "Vim Leader Key",
        .description = "The leader key for custom Vim keybindings.",
        .type = SettingType::String,
        .defaultValue = "\\",
        .category = "Editor",
        .subcategory = "Vim",
        .tags = {"vim", "leader"},
    });

    m_schema.register_setting({
        .id = "vim.escapeKey",
        .title = "Vim Escape Key",
        .description = "Key used to exit insert/visual modes.",
        .type = SettingType::String,
        .defaultValue = "Escape",
        .category = "Editor",
        .subcategory = "Vim",
        .tags = {"vim", "escape"},
    });

    m_schema.register_setting({
        .id = "vim.relativeLineNumbers",
        .title = "Vim Relative Line Numbers",
        .description = "Shows line numbers relative to cursor in Vim mode.",
        .type = SettingType::Boolean,
        .defaultValue = false,
        .category = "Editor",
        .subcategory = "Vim",
        .tags = {"vim", "line", "numbers"},
    });

    m_schema.register_setting({
        .id = "vim.showModeIndicator",
        .title = "Vim Show Mode Indicator",
        .description = "Displays the current Vim mode in the status bar.",
        .type = SettingType::Boolean,
        .defaultValue = true,
        .category = "Editor",
        .subcategory = "Vim",
        .tags = {"vim", "status", "mode"},
    });

    m_schema.register_setting({
        .id = "vim.timeout",
        .title = "Vim Sequence Timeout",
        .description = "Timeout in milliseconds for pending Vim key sequences.",
        .type = SettingType::Integer,
        .defaultValue = 1000,
        .category = "Editor",
        .subcategory = "Vim",
        .tags = {"vim", "timeout"},
        .minimum = 100.0,
        .maximum = 5000.0,
        .step = 100.0,
    });

    m_schema.register_setting({
        .id = "editor.fontSize",
        .title = "Font Size",
        .description = "Controls the font size in pixels for the code editor.",
        .type = SettingType::Integer,
        .defaultValue = 14,
        .category = "Editor",
        .subcategory = "Font",
        .tags = {"font", "size", "zoom", "text"},
        .minimum = 8.0,
        .maximum = 72.0,
        .step = 1.0,
    });

    m_schema.register_setting({
        .id = "editor.fontFamily",
        .title = "Font Family",
        .description = "Controls the font family used in the code editor.",
        .type = SettingType::Enum,
        .defaultValue = "Consolas, 'Courier New', monospace",
        .category = "Editor",
        .subcategory = "Font",
        .tags = {"font", "family", "typeface", "coding", "monospace"},
        .enum_values = {
            {"Consolas, 'Courier New', monospace", "Consolas, 'Courier New', monospace"},
            {"Cascadia Code, Consolas, monospace", "Cascadia Code, Consolas, monospace"},
            {"Fira Code, Consolas, monospace", "Fira Code, Consolas, monospace"},
            {"JetBrains Mono, monospace", "JetBrains Mono, monospace"},
            {"Source Code Pro, monospace", "Source Code Pro, monospace"},
            {"Hack, monospace", "Hack, monospace"},
            {"Hack", "Hack"},
            {"Consolas", "Consolas"},
            {"Courier New", "Courier New"}
        },
    });

    m_schema.register_setting({
        .id = "editor.tabSize",
        .title = "Tab Size",
        .description = "The number of spaces a tab is equal to.",
        .type = SettingType::Integer,
        .defaultValue = 4,
        .category = "Editor",
        .subcategory = "Indentation",
        .tags = {"tab", "size", "indent", "spaces"},
        .minimum = 1.0,
        .maximum = 8.0,
        .step = 1.0,
    });

    m_schema.register_setting({
        .id = "editor.renderWhitespace",
        .title = "Render Whitespace",
        .description = "Controls how the editor should render whitespace characters.",
        .type = SettingType::Enum,
        .defaultValue = "selection",
        .category = "Editor",
        .subcategory = "Display",
        .tags = {"whitespace", "render", "space"},
        .enum_values = {
            {"none", "none"},
            {"boundary", "boundary"},
            {"selection", "selection"},
            {"trailing", "trailing"},
            {"all", "all"}
        },
    });

    m_schema.register_setting({
        .id = "editor.lineHeight",
        .title = "Line Height",
        .description = "Controls the line height in pixels for the editor.",
        .type = SettingType::Integer,
        .defaultValue = 20,
        .category = "Editor",
        .subcategory = "Layout",
        .tags = {"line", "height", "spacing"},
        .minimum = 10.0,
        .maximum = 60.0,
        .step = 1.0,
    });

    m_schema.register_setting({
        .id = "editor.wordWrap",
        .title = "Word Wrap",
        .description = "Controls whether lines wrap around or scroll horizontally.",
        .type = SettingType::Boolean,
        .defaultValue = false,
        .category = "Editor",
        .subcategory = "Display",
        .tags = {"wrap", "line", "overflow"},
    });

    m_schema.register_setting({
        .id = "editor.minimap.enabled",
        .title = "Enable Minimap",
        .description = "Controls whether the code overview minimap is rendered on the right side.",
        .type = SettingType::Boolean,
        .defaultValue = true,
        .category = "Editor",
        .subcategory = "Minimap",
        .tags = {"minimap", "overview", "scroll"},
    });

    m_schema.register_setting({
        .id = "editor.cursorStyle",
        .title = "Cursor Style",
        .description = "Controls the visual caret style used inside the editor: Line, Block, or Underline.",
        .type = SettingType::Enum,
        .defaultValue = "Line",
        .category = "Editor",
        .subcategory = "Caret",
        .tags = {"cursor", "caret", "style", "default", "line", "block", "underline"},
        .enum_values = {
            {"Line", "Line"},
            {"Block", "Block"},
            {"Underline", "Underline"}
        },
    });

    // 2. UI / Appearance settings
    m_schema.register_setting({
        .id = "ui.fontSize",
        .title = "UI Font Size",
        .description = "Controls the font size in pixels for general user interface elements.",
        .type = SettingType::Integer,
        .defaultValue = 12,
        .category = "Appearance",
        .subcategory = "Typography",
        .tags = {"ui", "font", "size"},
        .minimum = 9.0,
        .maximum = 24.0,
        .step = 1.0,
    });

    m_schema.register_setting({
        .id = "ui.fontFamily",
        .title = "UI Font Family",
        .description = "Controls the font family used across application chrome and panels.",
        .type = SettingType::Enum,
        .defaultValue = "Open Sans",
        .category = "Appearance",
        .subcategory = "Typography",
        .tags = {"ui", "font", "family"},
        .enum_values = {
            {"Open Sans", "Open Sans"},
            {"Segoe UI", "Segoe UI"},
            {"Arial", "Arial"}
        },
    });

    m_schema.register_setting({
        .id = "ui.scale",
        .title = "UI Scale",
        .description = "Controls the overall user interface scale factor.",
        .type = SettingType::Float,
        .defaultValue = 1.0,
        .category = "Appearance",
        .subcategory = "Display",
        .tags = {"scale", "dpi", "zoom"},
        .minimum = 0.5,
        .maximum = 3.0,
        .step = 0.1,
    });

    m_schema.register_setting({
        .id = "theme.current",
        .title = "Color Theme",
        .description = "Specifies the active color theme applied to the studio interface.",
        .type = SettingType::Enum,
        .defaultValue = "Dark",
        .category = "Appearance",
        .subcategory = "Theme",
        .tags = {"theme", "color", "dark", "light"},
        .enum_values = {
            {"Dark", "Dark"},
            {"Light", "Light"},
            {"High Contrast", "High Contrast"}
        },
    });

    // 3. Window settings
    m_schema.register_setting({
        .id = "window.zoomLevel",
        .title = "Zoom Level",
        .description = "Adjusts the zoom level of the studio window.",
        .type = SettingType::Integer,
        .defaultValue = 0,
        .category = "Window",
        .subcategory = "View",
        .tags = {"zoom", "window"},
        .minimum = -2.0,
        .maximum = 5.0,
        .step = 1.0,
    });

    m_schema.register_setting({
        .id = "window.fullscreen",
        .title = "Fullscreen on Start",
        .description = "Controls whether the IDE window launches in fullscreen mode.",
        .type = SettingType::Boolean,
        .defaultValue = false,
        .category = "Window",
        .subcategory = "Startup",
        .tags = {"fullscreen", "window", "display"},
    });

    // 4. Workbench settings
    m_schema.register_setting({
        .id = "workbench.sidebar.position",
        .title = "Sidebar Position",
        .description = "Controls the docked location of the tool sidebar.",
        .type = SettingType::Enum,
        .defaultValue = "Left",
        .category = "Workbench",
        .subcategory = "Layout",
        .tags = {"sidebar", "dock", "position"},
        .enum_values = {
            {"Left", "Left"},
            {"Right", "Right"}
        },
    });

    m_schema.register_setting({
        .id = "workbench.panel.position",
        .title = "Panel Position",
        .description = "Controls the location of the bottom console and terminal panels.",
        .type = SettingType::Enum,
        .defaultValue = "Bottom",
        .category = "Workbench",
        .subcategory = "Layout",
        .tags = {"panel", "terminal", "bottom"},
        .enum_values = {
            {"Bottom", "Bottom"},
            {"Right", "Right"}
        },
    });

    m_schema.register_setting({
        .id = "workbench.activityBar.visible",
        .title = "Activity Bar Visibility",
        .description = "Controls whether the vertical activity sidebar is visible.",
        .type = SettingType::Boolean,
        .defaultValue = true,
        .category = "Workbench",
        .subcategory = "Layout",
        .tags = {"activity", "bar", "sidebar", "toggle"},
    });

    m_schema.register_setting({
        .id = "workbench.app.title",
        .title = "App Title",
        .description = "Controls the application title displayed on the empty welcome screen.",
        .type = SettingType::String,
        .defaultValue = std::string("Zenvra Development Studio"),
        .category = "Workbench",
        .subcategory = "Appearance",
        .tags = {"title", "welcome", "app", "workbench", "branding", "name"},
    });

    m_schema.register_setting({
        .id = "workbench.mascot.image",
        .title = "Mascot Image",
        .description = "Controls the mascot / splash image displayed on the empty welcome screen. Supports PNG, JPG, JPEG, and BMP files.",
        .type = SettingType::String,
        .defaultValue = std::string("zenvra_logo.png"),
        .category = "Workbench",
        .subcategory = "Appearance",
        .tags = {"mascot", "logo", "splash", "welcome", "image", "workbench", "branding"},
    });

    m_schema.register_setting({
        .id = "workbench.mascot.renderMode",
        .title = "Mascot Render Mode",
        .description = "Controls whether the mascot is rendered as a solid graphic or dynamic ASCII art.",
        .type = SettingType::Enum,
        .defaultValue = std::string("default"),
        .category = "Workbench",
        .subcategory = "Appearance",
        .tags = {"mascot", "ascii", "solid", "render", "style", "workbench", "logo"},
        .enum_values = {
            {.label = "Default (Solid Image)", .value = "default"},
            {.label = "ASCII Art", .value = "ascii"}
        },
    });

    // 5. Terminal settings
    m_schema.register_setting({
        .id = "terminal.fontSize",
        .title = "Terminal Font Size",
        .description = "Controls the font size in pixels for the integrated terminal emulator.",
        .type = SettingType::Integer,
        .defaultValue = 14,
        .category = "Terminal",
        .subcategory = "Font",
        .tags = {"terminal", "font", "size", "console"},
        .minimum = 8.0,
        .maximum = 36.0,
        .step = 1.0,
    });

    m_schema.register_setting({
        .id = "terminal.fontFamily",
        .title = "Terminal Font Family",
        .description = "Controls the font family used inside the integrated terminal.",
        .type = SettingType::Enum,
        .defaultValue = "Hack",
        .category = "Terminal",
        .subcategory = "Font",
        .tags = {"terminal", "font", "family"},
        .enum_values = {
            {"Hack", "Hack"},
            {"Consolas", "Consolas"},
            {"Courier New", "Courier New"}
        },
    });

    // 6. Shader settings
    m_schema.register_setting({
        .id = "shader.preview.enabled",
        .title = "Enable Shader Preview",
        .description = "Enables real-time shader preview simulation in the sandbox viewport.",
        .type = SettingType::Boolean,
        .defaultValue = true,
        .category = "Shader",
        .subcategory = "Preview",
        .tags = {"shader", "preview", "sandbox", "render"},
    });

    m_schema.register_setting({
        .id = "shader.preview.backend",
        .title = "Shader Backend",
        .description = "Selects the rasterization engine used for rendering shader stages.",
        .type = SettingType::Enum,
        .defaultValue = "CPU",
        .category = "Shader",
        .subcategory = "Engine",
        .tags = {"shader", "backend", "cpu", "gpu"},
        .enum_values = {
            {"CPU", "CPU"},
            {"Software", "Software"},
            {"GPU", "GPU"}
        },
    });

    m_schema.register_setting({
        .id = "shader.preview.fpsLimit",
        .title = "Shader FPS Limit",
        .description = "Limits the frame rate cap for shader execution to optimize power usage.",
        .type = SettingType::Integer,
        .defaultValue = 60,
        .category = "Shader",
        .subcategory = "Performance",
        .tags = {"shader", "fps", "limit", "performance"},
        .minimum = 15.0,
        .maximum = 240.0,
        .step = 15.0,
    });

    m_schema.register_setting({
        .id = "shader.preview.vsync",
        .title = "Shader VSync",
        .description = "Locks shader viewport refresh rate to display vertical sync.",
        .type = SettingType::Boolean,
        .defaultValue = true,
        .category = "Shader",
        .subcategory = "Performance",
        .tags = {"shader", "vsync", "sync"},
    });

    m_schema.register_setting({
        .id = "shader.preview.autoReload",
        .title = "Shader Auto Reload",
        .description = "Automatically recompiles and hot-reloads shader when code changes.",
        .type = SettingType::Boolean,
        .defaultValue = true,
        .category = "Shader",
        .subcategory = "Hot Reload",
        .tags = {"shader", "reload", "watch"},
    });

    // 7. Browser settings
    m_schema.register_setting({
        .id = "browser.defaultZoom",
        .title = "Browser Default Zoom",
        .description = "Default zoom level for embedded web preview documents.",
        .type = SettingType::Float,
        .defaultValue = 1.0,
        .category = "Browser",
        .subcategory = "View",
        .tags = {"browser", "web", "zoom"},
        .minimum = 0.25,
        .maximum = 4.0,
        .step = 0.1,
    });

    m_schema.register_setting({
        .id = "browser.devtools.enabled",
        .title = "Enable Browser DevTools",
        .description = "Enables developer tools inspection console for embedded browser.",
        .type = SettingType::Boolean,
        .defaultValue = false,
        .category = "Browser",
        .subcategory = "Developer",
        .tags = {"browser", "devtools", "inspect"},
    });

    // 8. Application settings
    m_schema.register_setting({
        .id = "application.update.mode",
        .title = "Update: Mode",
        .description = "Configure whether the studio should automatically check and download updates.",
        .type = SettingType::Enum,
        .defaultValue = "default",
        .category = "Application",
        .subcategory = "Update",
        .tags = {"update", "application", "upgrade"},
        .enum_values = {
            {"default", "default"},
            {"manual", "manual"},
            {"none", "none"}
        },
    });

    m_schema.register_setting({
        .id = "application.telemetry.enabled",
        .title = "Telemetry: Enable Telemetry",
        .description = "Enable anonymous crash reporting and performance telemetry.",
        .type = SettingType::Boolean,
        .defaultValue = false,
        .category = "Application",
        .subcategory = "Telemetry",
        .tags = {"telemetry", "privacy", "crash"},
    });

    m_schema.register_setting({
        .id = "application.proxy.support",
        .title = "Proxy: Support",
        .description = "Use proxy support for network connections and extension downloads.",
        .type = SettingType::Enum,
        .defaultValue = "override",
        .category = "Application",
        .subcategory = "Proxy",
        .tags = {"proxy", "network", "connection"},
        .enum_values = {
            {"override", "override"},
            {"on", "on"},
            {"off", "off"}
        },
    });

    // 9. Security settings
    m_schema.register_setting({
        .id = "security.workspace.trust.enabled",
        .title = "Workspace Trust: Enabled",
        .description = "Controls whether Workspace Trust is enabled to protect against untrusted folder scripts.",
        .type = SettingType::Boolean,
        .defaultValue = true,
        .category = "Security",
        .subcategory = "Workspace",
        .tags = {"security", "trust", "workspace"},
    });
}

} // namespace Zenvra::Settings
