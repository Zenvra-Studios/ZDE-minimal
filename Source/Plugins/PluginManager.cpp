#include "Plugins/PluginManager.h"
#include "Language/LanguageServerManager.h"
#include "Language/Registry/ServerRegistry.h"
#include "Language/Syntax/GrammarRegistry.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Zenvra::Plugins
{

namespace
{

std::filesystem::path resolve_default_home()
{
#if defined(_WIN32)
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile)
    {
        return std::filesystem::path(userprofile) / ".zde";
    }
    const char* appdata = std::getenv("APPDATA");
    if (appdata)
    {
        return std::filesystem::path(appdata) / "ZDE";
    }
    return "C:\\.zde";
#else
    const char* home = std::getenv("HOME");
    if (home)
    {
        return std::filesystem::path(home) / ".zde";
    }
    return "/tmp/.zde";
#endif
}

std::filesystem::path resolve_default_plugins_root()
{
    std::error_code ec;
    auto cur = std::filesystem::current_path(ec);
    if (!ec && std::filesystem::is_directory(cur / "plugins", ec))
    {
        return cur / "plugins";
    }
    auto p = cur;
    for (int i = 0; i < 4 && !p.empty(); ++i)
    {
        if (std::filesystem::is_directory(p / "plugins", ec))
        {
            return p / "plugins";
        }
        if (p == p.parent_path()) break;
        p = p.parent_path();
    }
    return cur / "plugins";
}

} // namespace

PluginManager& PluginManager::instance() noexcept
{
    static PluginManager inst;
    return inst;
}

PluginManager::PluginManager() = default;

void PluginManager::initialize(
    std::optional<std::filesystem::path> custom_home,
    std::optional<std::filesystem::path> custom_bundled)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return;

    m_zde_home = resolve_default_home();
    if (custom_home.has_value())
    {
        m_zde_home = *custom_home;
        m_plugins_root = *custom_home / "plugins";
    }
    else
    {
        m_plugins_root = resolve_default_plugins_root();
    }

    std::error_code ec;
    std::filesystem::create_directories(m_plugins_root / "lsp", ec);
    std::filesystem::create_directories(m_plugins_root / "emulators", ec);
    std::filesystem::create_directories(m_plugins_root / "tools", ec);
    std::filesystem::create_directories(m_plugins_root / "themes", ec);
    std::filesystem::create_directories(m_plugins_root / "apps", ec);
    std::filesystem::create_directories(m_plugins_root / "installed", ec);
    std::filesystem::create_directories(m_plugins_root / ".staging", ec);
    std::filesystem::create_directories(m_plugins_root / ".build-cache", ec);
    std::filesystem::create_directories(m_plugins_root / ".registry", ec);
    std::filesystem::create_directories(m_plugins_root / ".cache" / "icons", ec);

    // Initialize Toolchain
    Toolchain::ToolManager::instance().initialize(m_zde_home);
    Toolchain::ToolManager::instance().scan_and_register_known_tools();

    // Initialize Installer
    m_installer = std::make_unique<Installer::PluginInstaller>(
        m_plugins_root,
        m_plugins_root / ".build-cache",
        &Toolchain::ToolManager::instance());

    // Load Plugin Registry
    m_registry_file = m_plugins_root / ".registry" / "plugins.json";
    m_registry.load_from_disk(m_registry_file);

    // Bundled plugins directory
    if (custom_bundled.has_value())
    {
        m_bundled_dir = *custom_bundled;
    }
    else
    {
        m_bundled_dir = m_plugins_root / "bundled";
    }

    // Scan directories for plugins
    scan_plugins();

    // Set marketplace cache dir
    m_marketplace.set_cache_dir(m_plugins_root / ".cache" / "icons");

    // Load static marketplace catalog if available
    std::filesystem::path catalog_file = m_plugins_root / ".cache" / "marketplace.json";
    if (std::filesystem::exists(catalog_file, ec))
    {
        m_marketplace.load_catalog_from_file(catalog_file);
    }
    m_marketplace.ensure_default_catalog_if_empty();

    // Activate enabled plugins
    for (auto& plugin : m_registry.get_enabled_plugins())
    {
        activate_plugin(plugin);
    }

    m_initialized = true;
}

void PluginManager::scan_plugins()
{
    std::error_code ec;

    auto scan_dir_for_plugins = [this](const std::filesystem::path& dir, PluginSource source) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;

        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (!entry.is_directory(ec)) continue;

            std::string fname = entry.path().filename().string();
            if (fname.starts_with(".")) continue;

            std::filesystem::path manifest_path = entry.path() / "plugin.json";
            if (!std::filesystem::exists(manifest_path, ec))
            {
                manifest_path = entry.path() / "package.json";
            }

            if (std::filesystem::exists(manifest_path, ec))
            {
                if (auto manifest = PluginManifest::from_file(manifest_path))
                {
                    if (!m_registry.has_plugin(manifest->get_id()))
                    {
                        auto plugin = std::make_shared<Plugin>(*manifest, entry.path(), source);
                        plugin->set_state(PluginState::Installed);
                        plugin->set_enabled(true);
                        m_registry.register_plugin(plugin);
                    }
                }
            }
            else
            {
                // Category subfolder (e.g. plugins/lsp, plugins/emulators, plugins/tools, etc.)
                for (const auto& sub_entry : std::filesystem::directory_iterator(entry.path(), ec))
                {
                    if (!sub_entry.is_directory(ec)) continue;
                    std::string sub_name = sub_entry.path().filename().string();
                    if (sub_name.starts_with(".")) continue;

                    std::filesystem::path sub_manifest = sub_entry.path() / "plugin.json";
                    if (!std::filesystem::exists(sub_manifest, ec))
                    {
                        sub_manifest = sub_entry.path() / "package.json";
                    }

                    if (std::filesystem::exists(sub_manifest, ec))
                    {
                        if (auto manifest = PluginManifest::from_file(sub_manifest))
                        {
                            if (!m_registry.has_plugin(manifest->get_id()))
                            {
                                auto plugin = std::make_shared<Plugin>(*manifest, sub_entry.path(), source);
                                plugin->set_state(PluginState::Installed);
                                plugin->set_enabled(true);
                                m_registry.register_plugin(plugin);
                            }
                        }
                    }
                }
            }
        }
    };

    scan_dir_for_plugins(m_bundled_dir, PluginSource::Bundled);
    scan_dir_for_plugins(m_plugins_root, PluginSource::Local);
    scan_dir_for_plugins(m_zde_home / "plugins" / "installed", PluginSource::Local);

    if (!m_registry_file.empty())
    {
        m_registry.save_to_disk(m_registry_file);
    }
}

bool PluginManager::activate_plugin(std::shared_ptr<Plugin> plugin)
{
    if (!plugin) return false;

    // 1. Register TextMate/JSON syntax grammars
    for (const auto& grammar_file : plugin->get_grammar_files())
    {
        Language::Syntax::GrammarRegistry::instance().load_grammar_from_file(grammar_file);
    }
    std::error_code ec;
    std::filesystem::path syntaxes_dir = plugin->get_install_path() / "syntaxes";
    if (std::filesystem::is_directory(syntaxes_dir, ec))
    {
        for (const auto& entry : std::filesystem::directory_iterator(syntaxes_dir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
            {
                Language::Syntax::GrammarRegistry::instance().load_grammar_from_file(entry.path());
            }
        }
    }

    // 2. Map language files and LSP configurations
    std::unordered_map<std::string, std::vector<std::string>> lang_extensions;
    for (const auto& lang_file : plugin->get_language_files())
    {
        try
        {
            std::ifstream ifs(lang_file);
            if (ifs.is_open())
            {
                nlohmann::json lj;
                ifs >> lj;
                std::string lid = lj.value("id", "");
                if (!lid.empty() && lj.contains("extensions") && lj["extensions"].is_array())
                {
                    for (const auto& ext : lj["extensions"])
                    {
                        if (ext.is_string()) lang_extensions[lid].push_back(ext.get<std::string>());
                    }
                }
            }
        }
        catch (...) {}
    }

    bool registered_any_profile = false;

    // A. Check lsp/*.json files
    for (const auto& lsp_file : plugin->get_lsp_files())
    {
        try
        {
            std::ifstream ifs(lsp_file);
            if (ifs.is_open())
            {
                nlohmann::json lspj;
                ifs >> lspj;

                std::string lid = lspj.value("language", "");
                std::string tool_id = lspj.value("toolId", "");
                if (tool_id.empty() && lspj.contains("executable"))
                {
                    if (lspj["executable"].is_string())
                    {
                        tool_id = lspj["executable"].get<std::string>();
                    }
                    else if (lspj["executable"].is_object() && lspj["executable"].contains("command"))
                    {
                        tool_id = lspj["executable"]["command"].get<std::string>();
                    }
                }

                Language::Registry::ServerProfile profile;
                if (auto std_p = Language::Registry::ServerRegistry::create_standard_profile_for(lid.empty() ? tool_id : lid))
                {
                    profile = std::move(*std_p);
                }
                else
                {
                    profile.language_id = lid.empty() ? tool_id : lid;
                    profile.executable_name = tool_id;
                }

                profile.plugin_id = plugin->get_id();
                if (lang_extensions.contains(profile.language_id))
                {
                    profile.extensions = lang_extensions[profile.language_id];
                }

                if (lspj.contains("extensions") && lspj["extensions"].is_array())
                {
                    profile.extensions.clear();
                    for (const auto& ext : lspj["extensions"])
                    {
                        if (ext.is_string()) profile.extensions.push_back(ext.get<std::string>());
                    }
                }

                if (lspj.contains("args") && lspj["args"].is_array())
                {
                    profile.default_args.clear();
                    for (const auto& arg : lspj["args"])
                    {
                        if (arg.is_string()) profile.default_args.push_back(arg.get<std::string>());
                    }
                }

                Language::Registry::ServerRegistry::instance().register_profile(std::move(profile));
                registered_any_profile = true;
            }
        }
        catch (...) {}
    }

    // B. Check plugin.json for "lsp" config
    std::filesystem::path plugin_json_path = plugin->get_install_path() / "plugin.json";
    if (std::filesystem::exists(plugin_json_path, ec))
    {
        try
        {
            std::ifstream ifs(plugin_json_path);
            if (ifs.is_open())
            {
                nlohmann::json pj;
                ifs >> pj;
                if (pj.contains("lsp") && pj["lsp"].is_object())
                {
                    const auto& lspj = pj["lsp"];
                    std::string lid = lspj.value("language", "");
                    std::string exe = lspj.value("executable", "");

                    Language::Registry::ServerProfile profile;
                    if (auto std_p = Language::Registry::ServerRegistry::create_standard_profile_for(lid.empty() ? exe : lid))
                    {
                        profile = std::move(*std_p);
                    }
                    else
                    {
                        profile.language_id = lid.empty() ? exe : lid;
                        profile.executable_name = exe;
                    }

                    profile.plugin_id = plugin->get_id();
                    if (lspj.contains("extensions") && lspj["extensions"].is_array())
                    {
                        profile.extensions.clear();
                        for (const auto& ext : lspj["extensions"])
                        {
                            if (ext.is_string()) profile.extensions.push_back(ext.get<std::string>());
                        }
                    }
                    if (lspj.contains("args") && lspj["args"].is_array())
                    {
                        profile.default_args.clear();
                        for (const auto& arg : lspj["args"])
                        {
                            if (arg.is_string()) profile.default_args.push_back(arg.get<std::string>());
                        }
                    }
                    Language::Registry::ServerRegistry::instance().register_profile(std::move(profile));
                    registered_any_profile = true;
                }
            }
        }
        catch (...) {}
    }

    // C. If not registered yet, deduce standard profile from plugin ID, capabilities, or tools
    if (!registered_any_profile)
    {
        std::string candidate_keys[] = {
            plugin->get_id(),
            plugin->get_manifest().get_name(),
        };

        std::optional<Language::Registry::ServerProfile> deduced;
        for (const auto& key : candidate_keys)
        {
            deduced = Language::Registry::ServerRegistry::create_standard_profile_for(key);
            if (deduced.has_value()) break;
        }

        if (!deduced.has_value())
        {
            for (const auto& tool_dep : plugin->get_manifest().get_tool_dependencies())
            {
                deduced = Language::Registry::ServerRegistry::create_standard_profile_for(tool_dep.id);
                if (deduced.has_value()) break;
            }
        }

        if (deduced.has_value())
        {
            deduced->plugin_id = plugin->get_id();

            // Check if local bundled binary exists in plugin install path
            std::string exe_name = deduced->executable_name;
            std::filesystem::path local_candidates[] = {
                plugin->get_install_path() / exe_name,
                plugin->get_install_path() / "bin" / exe_name,
#if defined(_WIN32)
                plugin->get_install_path() / (exe_name + ".exe"),
                plugin->get_install_path() / "bin" / (exe_name + ".exe"),
#endif
            };
            for (const auto& lc : local_candidates)
            {
                if (std::filesystem::exists(lc, ec) && std::filesystem::is_regular_file(lc, ec))
                {
                    deduced->custom_executable_path = lc;
                    break;
                }
            }

            Language::Registry::ServerRegistry::instance().register_profile(std::move(*deduced));
            registered_any_profile = true;
        }
    }

    const std::string cat = plugin->get_manifest().get_category();
    if (cat == "tools" || cat == "emulators" || cat == "apps" || (cat != "lsp" && cat != "themes" && !cat.empty()))
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_active_tool_plugin_id.empty())
        {
            m_active_tool_plugin_id = plugin->get_id();
        }
    }

    plugin->set_state(PluginState::Enabled);
    for (const auto& cb : m_lifecycle_callbacks)
    {
        if (cb) cb(plugin, true);
    }
    return true;
}

void PluginManager::deactivate_plugin(std::shared_ptr<Plugin> plugin)
{
    if (!plugin) return;

    // Find and stop active language clients for this plugin
    auto all_profiles = Language::Registry::ServerRegistry::instance().get_all_profiles();
    for (const auto* prof : all_profiles)
    {
        if (prof && prof->plugin_id == plugin->get_id())
        {
            Language::LanguageServerManager::instance().stop_client_for_language(prof->language_id);
        }
    }

    Language::Registry::ServerRegistry::instance().unregister_profiles_for_plugin(plugin->get_id());
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_active_tool_plugin_id == plugin->get_id())
        {
            m_active_tool_plugin_id.clear();
        }
    }
    plugin->set_state(PluginState::Disabled);
    for (const auto& cb : m_lifecycle_callbacks)
    {
        if (cb) cb(plugin, false);
    }
}

Installer::InstallResult PluginManager::install_from_local(const std::filesystem::path& path)
{
    if (!m_installer)
    {
        Installer::InstallResult r;
        r.error_message = "Plugin installer is not initialized.";
        return r;
    }

    auto res = m_installer->install_from_local(path);
    if (res.success && res.installed_plugin)
    {
        m_registry.register_plugin(res.installed_plugin);
        activate_plugin(res.installed_plugin);
        m_registry.save_to_disk(m_registry_file);
    }
    return res;
}

Installer::InstallResult PluginManager::install_from_git(
    const std::string& git_url,
    std::string_view branch,
    std::function<void(std::string_view)> log_callback)
{
    if (!m_installer)
    {
        Installer::InstallResult r;
        r.error_message = "Plugin installer is not initialized.";
        return r;
    }

    auto res = m_installer->install_from_git(git_url, branch, "0.1.0", log_callback);
    if (res.success && res.installed_plugin)
    {
        m_registry.register_plugin(res.installed_plugin);
        activate_plugin(res.installed_plugin);
        m_registry.save_to_disk(m_registry_file);
    }
    return res;
}

bool PluginManager::uninstall_plugin(std::string_view plugin_id)
{
    auto p = m_registry.get_plugin(plugin_id);
    if (!p) return false;

    deactivate_plugin(p);
    m_registry.unregister_plugin(plugin_id);

    if (m_installer)
    {
        m_installer->uninstall_plugin(plugin_id);
    }

    m_registry.save_to_disk(m_registry_file);
    return true;
}

bool PluginManager::enable_plugin(std::string_view plugin_id)
{
    auto p = m_registry.get_plugin(plugin_id);
    if (!p) return false;

    p->set_enabled(true);
    activate_plugin(p);
    m_registry.save_to_disk(m_registry_file);
    return true;
}

bool PluginManager::disable_plugin(std::string_view plugin_id)
{
    auto p = m_registry.get_plugin(plugin_id);
    if (!p) return false;

    p->set_enabled(false);
    deactivate_plugin(p);
    m_registry.save_to_disk(m_registry_file);
    return true;
}

std::shared_ptr<Plugin> PluginManager::get_plugin(std::string_view plugin_id) const
{
    return m_registry.get_plugin(plugin_id);
}

std::vector<std::shared_ptr<Plugin>> PluginManager::get_all_plugins() const
{
    return m_registry.get_all_plugins();
}

std::vector<std::shared_ptr<Plugin>> PluginManager::get_installed_tool_plugins() const
{
    std::vector<std::shared_ptr<Plugin>> tools;
    for (const auto& p : get_all_plugins())
    {
        if (!p) continue;
        std::error_code ec;
        if (!std::filesystem::exists(p->get_install_path(), ec)) continue;

        const std::string cat = p->get_manifest().get_category();
        if (cat == "tools" || cat == "emulators" || cat == "apps" ||
            (cat != "lsp" && cat != "themes" && !cat.empty()))
        {
            tools.push_back(p);
        }
    }
    return tools;
}

std::string PluginManager::get_active_tool_plugin_id() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_active_tool_plugin_id.empty())
    {
        auto p = m_registry.get_plugin(m_active_tool_plugin_id);
        if (p)
        {
            return m_active_tool_plugin_id;
        }
    }

    auto tools = get_installed_tool_plugins();
    if (!tools.empty())
    {
        return tools.front()->get_id();
    }
    return {};
}

void PluginManager::set_active_tool_plugin_id(std::string_view plugin_id)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_active_tool_plugin_id = std::string(plugin_id);
}

std::shared_ptr<Plugin> PluginManager::get_active_tool_plugin() const
{
    const std::string id = get_active_tool_plugin_id();
    if (id.empty()) return nullptr;
    return m_registry.get_plugin(id);
}

void PluginManager::register_lifecycle_listener(PluginLifecycleCallback cb)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lifecycle_callbacks.push_back(std::move(cb));
}

} // namespace Zenvra::Plugins
