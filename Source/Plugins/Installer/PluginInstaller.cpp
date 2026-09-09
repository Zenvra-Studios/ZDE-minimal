#include "Plugins/Installer/PluginInstaller.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <sstream>

namespace Zenvra::Plugins::Installer
{

namespace
{

struct StagingCleaner
{
    std::filesystem::path path;
    bool should_clean{true};

    ~StagingCleaner()
    {
        if (should_clean && !path.empty())
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    }
};

std::string generate_random_tag()
{
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> dist;
    return std::to_string(dist(rng));
}

int run_command_with_output(
    const std::string& cmd_str,
    std::string& output_log,
    std::function<void(std::string_view)> callback = nullptr)
{
#if defined(_WIN32)
    const std::string wrapped = "cmd.exe /d /c \"" + cmd_str + " 2>&1\"";
    FILE* pipe = _popen(wrapped.c_str(), "r");
#else
    const std::string wrapped = cmd_str + " 2>&1";
    FILE* pipe = popen(wrapped.c_str(), "r");
#endif

    if (!pipe)
    {
        output_log += "Failed to spawn process for command: " + cmd_str + "\n";
        return -1;
    }

    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        output_log += buffer.data();
        if (callback)
        {
            callback(buffer.data());
        }
    }

#if defined(_WIN32)
    int exit_code = _pclose(pipe);
#else
    int exit_code = pclose(pipe);
#endif
    return exit_code;
}

} // namespace

PluginInstaller::PluginInstaller(
    std::filesystem::path plugins_dir,
    std::filesystem::path build_cache_dir,
    Toolchain::ToolManager* tool_manager)
    : m_plugins_root(std::move(plugins_dir))
    , m_installed_dir(m_plugins_root / "installed")
    , m_staging_dir(m_plugins_root / ".staging")
    , m_build_cache_dir(std::move(build_cache_dir))
    , m_tool_manager(tool_manager)
{
    std::error_code ec;
    std::filesystem::create_directories(m_plugins_root / "lsp", ec);
    std::filesystem::create_directories(m_plugins_root / "emulators", ec);
    std::filesystem::create_directories(m_plugins_root / "tools", ec);
    std::filesystem::create_directories(m_plugins_root / "themes", ec);
    std::filesystem::create_directories(m_plugins_root / "apps", ec);
    std::filesystem::create_directories(m_installed_dir, ec);
    std::filesystem::create_directories(m_staging_dir, ec);
    std::filesystem::create_directories(m_build_cache_dir, ec);
}

std::filesystem::path PluginInstaller::get_target_path(std::string_view category, std::string_view plugin_id) const
{
    std::string cat = category.empty() ? "general" : std::string(category);
    return m_plugins_root / cat / plugin_id;
}

InstallResult PluginInstaller::validate_directory(const std::filesystem::path& dir_path) const
{
    InstallResult result;
    std::error_code ec;

    if (!std::filesystem::is_directory(dir_path, ec))
    {
        result.error_message = "Source path is not a valid directory: " + dir_path.string();
        return result;
    }

    std::filesystem::path manifest_path = dir_path / "plugin.json";
    std::optional<PluginManifest> manifest;
    if (std::filesystem::exists(manifest_path, ec))
    {
        manifest = PluginManifest::from_file(manifest_path);
    }
    else if (std::filesystem::exists(dir_path / "package.json", ec))
    {
        manifest_path = dir_path / "package.json";
        manifest = PluginManifest::from_file(manifest_path);
    }
    else
    {
        result.error_message = "plugin.json or package.json not found in directory: " + dir_path.string();
        return result;
    }

    if (!manifest.has_value())
    {
        result.error_message = "Failed to parse manifest in: " + manifest_path.string();
        return result;
    }

    result.success = true;
    result.installed_plugin = std::make_shared<Plugin>(*manifest, dir_path, PluginSource::Local);
    result.installed_plugin->set_state(PluginState::Validated);
    return result;
}

InstallResult PluginInstaller::install_from_local(
    const std::filesystem::path& source_path,
    std::string_view current_zde_version)
{
    InstallResult val = validate_directory(source_path);
    if (!val.success || !val.installed_plugin)
    {
        return val;
    }

    const auto& manifest = val.installed_plugin->get_manifest();
    if (!manifest.is_compatible_with_zde(current_zde_version))
    {
        InstallResult res;
        res.error_message = "Plugin '" + manifest.get_id() + "' requires ZDE version " +
                            manifest.get_minimum_zde_version() + ", but current ZDE is " +
                            std::string(current_zde_version);
        return res;
    }

    // Prepare atomic staging
    std::string tag = manifest.get_id() + "_" + generate_random_tag();
    std::filesystem::path staging_path = m_staging_dir / tag;
    std::string cat = manifest.get_category().empty() ? manifest.deduce_category() : manifest.get_category();
    std::filesystem::path target_path = get_target_path(cat, manifest.get_id());

    std::error_code ec;
    std::filesystem::remove_all(staging_path, ec);

    StagingCleaner cleaner{staging_path, true};

    // Copy to staging
    std::filesystem::copy(source_path, staging_path,
                          std::filesystem::copy_options::recursive |
                          std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        InstallResult res;
        res.error_message = "Failed to copy files to staging: " + ec.message();
        return res;
    }

    // Atomic promotion to installed directory
    if (std::filesystem::exists(target_path, ec))
    {
        std::filesystem::remove_all(target_path, ec);
    }

    std::filesystem::rename(staging_path, target_path, ec);
    if (ec)
    {
        // Fallback if rename fails across partitions
        std::filesystem::copy(staging_path, target_path,
                              std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove_all(staging_path, ec);
    }

    cleaner.should_clean = false; // Successfully installed

    InstallResult res;
    res.success = true;
    res.installed_plugin = std::make_shared<Plugin>(manifest, target_path, PluginSource::Local);
    res.installed_plugin->set_state(PluginState::Installed);
    return res;
}

bool PluginInstaller::execute_source_build(
    const PluginManifest& manifest,
    const std::filesystem::path& source_dir,
    std::string& out_error,
    std::function<void(std::string_view)> log_callback)
{
    const auto& build_opt = manifest.get_build_info();
    if (!build_opt.has_value())
    {
        return true; // No build step required
    }

    const auto& binfo = *build_opt;
    std::filesystem::path plugin_build_cache = m_build_cache_dir / manifest.get_id();
    std::error_code ec;
    std::filesystem::create_directories(plugin_build_cache, ec);

    std::string build_log;

    if (binfo.system == "cmake" || binfo.system == "cmake-ninja")
    {
        std::ostringstream cfg_cmd;
        cfg_cmd << "cmake -S \"" << source_dir.string() << "\" -B \"" << plugin_build_cache.string() << "\"";
        if (binfo.system == "cmake-ninja")
        {
            cfg_cmd << " -G Ninja";
        }
        for (const auto& arg : binfo.configure_args)
        {
            cfg_cmd << " " << arg;
        }

        if (log_callback) log_callback("[Plugin Build] Configuring: " + cfg_cmd.str() + "\n");
        int res = run_command_with_output(cfg_cmd.str(), build_log, log_callback);
        if (res != 0)
        {
            out_error = "CMake configure failed with code " + std::to_string(res) + ":\n" + build_log;
            return false;
        }

        std::ostringstream build_cmd;
        build_cmd << "cmake --build \"" << plugin_build_cache.string() << "\" --config Release";
        if (log_callback) log_callback("[Plugin Build] Building: " + build_cmd.str() + "\n");
        res = run_command_with_output(build_cmd.str(), build_log, log_callback);
        if (res != 0)
        {
            out_error = "CMake build failed with code " + std::to_string(res) + ":\n" + build_log;
            return false;
        }

        // Copy built artifacts into source_dir / bin
        std::filesystem::path dest_bin = source_dir / "bin";
        std::filesystem::create_directories(dest_bin, ec);
        std::filesystem::copy(plugin_build_cache, dest_bin,
                              std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing, ec);
        return true;
    }
    else if (binfo.system == "cargo")
    {
        std::ostringstream cargo_cmd;
        cargo_cmd << "cargo build --release --manifest-path \""
                  << (source_dir / "Cargo.toml").string() << "\"";
        if (log_callback) log_callback("[Plugin Build] Building Cargo: " + cargo_cmd.str() + "\n");
        int res = run_command_with_output(cargo_cmd.str(), build_log, log_callback);
        if (res != 0)
        {
            out_error = "Cargo build failed with code " + std::to_string(res) + ":\n" + build_log;
            return false;
        }
        return true;
    }

    return true;
}

InstallResult PluginInstaller::install_from_git(
    const std::string& git_url,
    std::string_view branch,
    std::string_view current_zde_version,
    std::function<void(std::string_view)> log_callback)
{
    InstallResult result;
    if (git_url.empty())
    {
        result.error_message = "Git URL cannot be empty.";
        return result;
    }

    std::string tag = "git_" + generate_random_tag();
    std::filesystem::path staging_path = m_staging_dir / tag;
    std::error_code ec;
    std::filesystem::create_directories(staging_path, ec);

    StagingCleaner cleaner{staging_path, true};

    std::ostringstream clone_cmd;
    clone_cmd << "git clone --depth 1 ";
    if (!branch.empty())
    {
        clone_cmd << "-b " << branch << " ";
    }
    clone_cmd << "\"" << git_url << "\" \"" << staging_path.string() << "\"";

    if (log_callback) log_callback("[Git Installer] Executing: " + clone_cmd.str() + "\n");

    std::string clone_log;
    int clone_code = run_command_with_output(clone_cmd.str(), clone_log, log_callback);
    if (clone_code != 0)
    {
        result.error_message = "git clone failed with exit code " + std::to_string(clone_code) + ":\n" + clone_log;
        return result;
    }

    std::filesystem::path manifest_file = staging_path / "plugin.json";
    std::optional<PluginManifest> manifest;
    if (std::filesystem::exists(manifest_file, ec))
    {
        manifest = PluginManifest::from_file(manifest_file);
    }
    else if (std::filesystem::exists(staging_path / "package.json", ec))
    {
        manifest = PluginManifest::from_file(staging_path / "package.json");
    }

    if (!manifest.has_value())
    {
        // Synthesize manifest from git repo name
        std::string repo_name = git_url;
        auto last_slash = repo_name.rfind('/');
        if (last_slash != std::string::npos) repo_name = repo_name.substr(last_slash + 1);
        if (repo_name.ends_with(".git")) repo_name = repo_name.substr(0, repo_name.size() - 4);

        PluginManifest synth;
        synth.set_id(repo_name);
        synth.set_name(repo_name);
        synth.set_version("1.0.0");
        synth.set_description("Direct Git plugin from " + git_url);
        manifest = synth;
    }

    if (!manifest->is_compatible_with_zde(current_zde_version))
    {
        result.error_message = "Plugin '" + manifest->get_id() + "' requires ZDE version " +
                               manifest->get_minimum_zde_version() + ", but current ZDE is " +
                               std::string(current_zde_version);
        return result;
    }

    // Execute source build if specified
    std::string build_error;
    if (!execute_source_build(*manifest, staging_path, build_error, log_callback))
    {
        result.error_message = build_error;
        return result;
    }

    // Atomic promotion to categorized directory
    std::string cat = manifest->get_category().empty() ? manifest->deduce_category() : manifest->get_category();
    std::filesystem::path target_path = get_target_path(cat, manifest->get_id());
    std::filesystem::create_directories(target_path.parent_path(), ec);
    if (std::filesystem::exists(target_path, ec))
    {
        std::filesystem::remove_all(target_path, ec);
    }

    std::filesystem::rename(staging_path, target_path, ec);
    if (ec)
    {
        std::filesystem::copy(staging_path, target_path,
                              std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove_all(staging_path, ec);
    }

    cleaner.should_clean = false;

    result.success = true;
    result.installed_plugin = std::make_shared<Plugin>(*manifest, target_path, PluginSource::Git);
    result.installed_plugin->set_state(PluginState::Installed);
    return result;
}

bool PluginInstaller::uninstall_plugin(std::string_view plugin_id)
{
    std::error_code ec;
    const std::string_view categories[] = {
        "lsp", "emulators", "tools", "themes", "apps", "general", "installed"
    };
    bool removed_any = false;
    for (std::string_view cat : categories)
    {
        std::filesystem::path target = m_plugins_root / cat / plugin_id;
        if (std::filesystem::exists(target, ec))
        {
            std::filesystem::remove_all(target, ec);
            if (!ec) removed_any = true;
        }
    }
    std::filesystem::path legacy_target = m_installed_dir / plugin_id;
    if (std::filesystem::exists(legacy_target, ec))
    {
        std::filesystem::remove_all(legacy_target, ec);
        if (!ec) removed_any = true;
    }
    std::filesystem::path direct_target = m_plugins_root / plugin_id;
    if (std::filesystem::exists(direct_target, ec))
    {
        std::filesystem::remove_all(direct_target, ec);
        if (!ec) removed_any = true;
    }
    return removed_any;
}

} // namespace Zenvra::Plugins::Installer
