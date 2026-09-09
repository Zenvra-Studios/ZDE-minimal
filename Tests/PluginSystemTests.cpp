#include <gtest/gtest.h>

#include "Plugins/PluginManifest.h"
#include "Plugins/Plugin.h"
#include "Plugins/PluginRegistry.h"
#include "Plugins/PluginManager.h"
#include "Plugins/Toolchain/ToolInfo.h"
#include "Plugins/Toolchain/ToolRegistry.h"
#include "Plugins/Toolchain/ToolManager.h"
#include "Plugins/Installer/DependencyResolver.h"
#include "Plugins/Installer/PluginInstaller.h"
#include "Plugins/Marketplace/MarketplaceClient.h"
#include "Language/Registry/ServerRegistry.h"
#include "Language/LanguageServerManager.h"

#include <filesystem>
#include <fstream>

using namespace Zenvra::Plugins;

class PluginSystemTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::error_code ec;
        m_test_root = std::filesystem::temp_directory_path() / "zde_plugin_test_sandbox";
        std::filesystem::remove_all(m_test_root, ec);
        std::filesystem::create_directories(m_test_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_test_root, ec);
    }

    std::filesystem::path m_test_root;
};

TEST_F(PluginSystemTests, PluginManifestParsingAndCompatibility)
{
    const std::string manifest_json = R"({
        "id": "org.zenvra.rust",
        "name": "Rust Support",
        "version": "1.2.0",
        "publisher": {
            "id": "zenvra",
            "name": "Zenvra Studios"
        },
        "description": "Rust language and toolchain support",
        "zde": {
            "minimumVersion": "0.1.0",
            "maximumVersion": "0.9.x"
        },
        "platforms": ["windows-x64", "linux-x64", "macos-arm64"],
        "capabilities": ["language", "grammar", "lsp", "toolchain"],
        "dependencies": {
            "plugins": [
                { "id": "org.zenvra.cargo", "version": ">=1.0.0", "optional": false }
            ],
            "tools": [
                { "id": "rust-analyzer", "version": ">=1.70.0", "optional": false, "description": "Rust LSP" }
            ]
        }
    })";

    auto manifest_opt = PluginManifest::from_json(nlohmann::json::parse(manifest_json));
    ASSERT_TRUE(manifest_opt.has_value());

    const auto& manifest = *manifest_opt;
    EXPECT_EQ(manifest.get_id(), "org.zenvra.rust");
    EXPECT_EQ(manifest.get_name(), "Rust Support");
    EXPECT_EQ(manifest.get_version(), "1.2.0");
    EXPECT_EQ(manifest.get_publisher_id(), "zenvra");
    EXPECT_EQ(manifest.get_publisher_name(), "Zenvra Studios");

    EXPECT_TRUE(manifest.has_capability("language"));
    EXPECT_TRUE(manifest.has_capability("lsp"));
    EXPECT_FALSE(manifest.has_capability("ui"));

    // ZDE Compatibility checks
    EXPECT_TRUE(manifest.is_compatible_with_zde("0.1.0"));
    EXPECT_TRUE(manifest.is_compatible_with_zde("0.5.2"));
    EXPECT_FALSE(manifest.is_compatible_with_zde("0.0.9"));
    EXPECT_FALSE(manifest.is_compatible_with_zde("1.0.0"));

    // Dependencies
    ASSERT_EQ(manifest.get_plugin_dependencies().size(), 1u);
    EXPECT_EQ(manifest.get_plugin_dependencies()[0].id, "org.zenvra.cargo");

    ASSERT_EQ(manifest.get_tool_dependencies().size(), 1u);
    EXPECT_EQ(manifest.get_tool_dependencies()[0].id, "rust-analyzer");

    // Serialization roundtrip
    nlohmann::json roundtrip = manifest.to_json();
    auto manifest_rt = PluginManifest::from_json(roundtrip);
    ASSERT_TRUE(manifest_rt.has_value());
    EXPECT_EQ(manifest_rt->get_id(), "org.zenvra.rust");
}

TEST_F(PluginSystemTests, ToolRegistryPersistenceAndQuery)
{
    Toolchain::ToolRegistry registry;
    std::filesystem::path registry_file = m_test_root / "registry" / "tools.json";

    Toolchain::ToolInfo clangd_tool;
    clangd_tool.id = "clangd";
    clangd_tool.name = "Clangd Language Server";
    clangd_tool.executable_path = "C:\\Program Files\\LLVM\\bin\\clangd.exe";
    clangd_tool.source = Toolchain::ToolSource::System;
    clangd_tool.status = Toolchain::ToolStatus::Available;
    clangd_tool.version = "19.1.0";

    registry.register_tool(clangd_tool);
    EXPECT_TRUE(registry.has_tool("clangd"));

    auto retrieved = registry.get_tool("clangd");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->name, "Clangd Language Server");
    EXPECT_EQ(retrieved->status, Toolchain::ToolStatus::Available);

    // Save to disk
    EXPECT_TRUE(registry.save_to_disk(registry_file));
    EXPECT_TRUE(std::filesystem::exists(registry_file));

    // Reload in fresh registry
    Toolchain::ToolRegistry reloaded_registry;
    EXPECT_TRUE(reloaded_registry.load_from_disk(registry_file));
    EXPECT_TRUE(reloaded_registry.has_tool("clangd"));
    auto reloaded_tool = reloaded_registry.get_tool("clangd");
    ASSERT_TRUE(reloaded_tool.has_value());
    EXPECT_EQ(reloaded_tool->version, "19.1.0");
    EXPECT_EQ(reloaded_tool->source, Toolchain::ToolSource::System);
}

TEST_F(PluginSystemTests, DependencyResolutionAndTopologicalSort)
{
    Installer::DependencyResolver resolver;

    // Create 3 plugin manifests:
    // Core (no deps)
    // Cpp (depends on Core)
    // Cmake (depends on Cpp)
    PluginManifest core_manifest;
    core_manifest.set_id("org.zenvra.core");

    PluginManifest cpp_manifest;
    cpp_manifest.set_id("org.zenvra.cpp");
    std::string cpp_json = R"({
        "id": "org.zenvra.cpp",
        "dependencies": {
            "plugins": [{ "id": "org.zenvra.core", "version": ">=0.1.0" }]
        }
    })";
    cpp_manifest = *PluginManifest::from_json(nlohmann::json::parse(cpp_json));

    PluginManifest cmake_manifest;
    cmake_manifest.set_id("org.zenvra.cmake");
    std::string cmake_json = R"({
        "id": "org.zenvra.cmake",
        "dependencies": {
            "plugins": [{ "id": "org.zenvra.cpp", "version": ">=0.1.0" }]
        }
    })";
    cmake_manifest = *PluginManifest::from_json(nlohmann::json::parse(cmake_json));

    std::unordered_map<std::string, PluginManifest> plugins = {
        {"org.zenvra.core", core_manifest},
        {"org.zenvra.cpp", cpp_manifest},
        {"org.zenvra.cmake", cmake_manifest}
    };

    auto sort_result = resolver.sort_topological(plugins);
    ASSERT_TRUE(sort_result.success);
    ASSERT_EQ(sort_result.install_order.size(), 3u);

    // Topological order: core must come before cpp, cpp must come before cmake
    auto idx_core = std::find(sort_result.install_order.begin(), sort_result.install_order.end(), "org.zenvra.core");
    auto idx_cpp = std::find(sort_result.install_order.begin(), sort_result.install_order.end(), "org.zenvra.cpp");
    auto idx_cmake = std::find(sort_result.install_order.begin(), sort_result.install_order.end(), "org.zenvra.cmake");

    EXPECT_LT(idx_core, idx_cpp);
    EXPECT_LT(idx_cpp, idx_cmake);

    // Test circular dependency rejection
    std::string cycle_a_json = R"({
        "id": "A", "dependencies": { "plugins": [{ "id": "B" }] }
    })";
    std::string cycle_b_json = R"({
        "id": "B", "dependencies": { "plugins": [{ "id": "A" }] }
    })";

    std::unordered_map<std::string, PluginManifest> cycle_plugins = {
        {"A", *PluginManifest::from_json(nlohmann::json::parse(cycle_a_json))},
        {"B", *PluginManifest::from_json(nlohmann::json::parse(cycle_b_json))}
    };

    auto cycle_result = resolver.sort_topological(cycle_plugins);
    EXPECT_FALSE(cycle_result.success);
    EXPECT_NE(cycle_result.error_message.find("Circular dependency"), std::string::npos);
}

TEST_F(PluginSystemTests, AtomicStagingAndLocalInstallation)
{
    // Setup temporary source plugin directory
    std::filesystem::path src_dir = m_test_root / "my_source_plugin";
    std::filesystem::create_directories(src_dir / "grammar");
    std::filesystem::create_directories(src_dir / "language");

    const std::string plugin_json = R"({
        "id": "org.test.sample",
        "name": "Sample Test Plugin",
        "version": "1.0.0",
        "capabilities": ["language", "grammar"],
        "zde": { "minimumVersion": "0.1.0" }
    })";

    {
        std::ofstream manifest_file(src_dir / "plugin.json");
        manifest_file << plugin_json;
    }

    {
        std::ofstream grammar_file(src_dir / "grammar" / "sample.json");
        grammar_file << R"({"name": "Sample", "scopeName": "source.sample", "patterns": []})";
    }

    // Initialize PluginInstaller with sandbox paths
    std::filesystem::path plugins_dir = m_test_root / "plugins";
    std::filesystem::path cache_dir = m_test_root / "build-cache";

    Installer::PluginInstaller installer(plugins_dir, cache_dir);

    // Install from local directory
    auto install_res = installer.install_from_local(src_dir, "0.1.0");
    ASSERT_TRUE(install_res.success);
    ASSERT_NE(install_res.installed_plugin, nullptr);
    EXPECT_EQ(install_res.installed_plugin->get_id(), "org.test.sample");
    EXPECT_EQ(install_res.installed_plugin->get_state(), PluginState::Installed);

    // Check that target path exists in categorized folder
    std::filesystem::path installed_path = install_res.installed_plugin->get_install_path();
    EXPECT_TRUE(std::filesystem::exists(installed_path / "plugin.json"));
    EXPECT_TRUE(std::filesystem::exists(installed_path / "grammar" / "sample.json"));
    EXPECT_EQ(install_res.installed_plugin->get_manifest().get_category(), "lsp");
    EXPECT_EQ(installed_path, plugins_dir / "lsp" / "org.test.sample");

    // Check that staging area is clean
    for (const auto& entry : std::filesystem::directory_iterator(installer.get_staging_dir()))
    {
        FAIL() << "Staging directory was not cleaned up: " << entry.path().string();
    }

    // Verify Plugin helper methods
    auto grammar_files = install_res.installed_plugin->get_grammar_files();
    ASSERT_EQ(grammar_files.size(), 1u);
    EXPECT_EQ(grammar_files[0].filename().string(), "sample.json");

    // Uninstall
    EXPECT_TRUE(installer.uninstall_plugin("org.test.sample"));
    EXPECT_FALSE(std::filesystem::exists(installed_path));
}

TEST_F(PluginSystemTests, MarketplaceCatalogSearchAndDiscovery)
{
    Marketplace::MarketplaceClient client;

    const std::string catalog_json = R"({
        "version": 1,
        "plugins": [
            {
                "id": "org.zenvra.cpp",
                "name": "C/C++ Support",
                "version": "1.0.0",
                "description": "Comprehensive C and C++ language integration with clangd",
                "publisher": "Zenvra Studios",
                "repository": "https://github.com/Zenvra-Studios/zde-cpp.git",
                "tags": ["cpp", "c", "clangd", "language"]
            },
            {
                "id": "org.zenvra.rust",
                "name": "Rust Support",
                "version": "1.1.0",
                "description": "Rust language support with rust-analyzer and cargo",
                "publisher": "Zenvra Studios",
                "repository": "https://github.com/Zenvra-Studios/zde-rust.git",
                "tags": ["rust", "cargo", "rust-analyzer"]
            },
            {
                "id": "org.zenvra.theme.dracula",
                "name": "Dracula Theme",
                "version": "2.0.0",
                "description": "A dark theme for ZDE Studio",
                "publisher": "Community",
                "tags": ["theme", "ui", "dark"]
            }
        ]
    })";

    ASSERT_TRUE(client.load_catalog_from_json(nlohmann::json::parse(catalog_json)));
    EXPECT_EQ(client.get_all_entries().size(), 3u);

    // Search by query
    auto rust_results = client.search("rust");
    ASSERT_EQ(rust_results.size(), 1u);
    EXPECT_EQ(rust_results[0].id, "org.zenvra.rust");

    auto tag_results = client.search("clangd");
    ASSERT_EQ(tag_results.size(), 1u);
    EXPECT_EQ(tag_results[0].id, "org.zenvra.cpp");

    auto empty_search = client.search("");
    EXPECT_EQ(empty_search.size(), 3u);

    auto entry_opt = client.find_entry("org.zenvra.theme.dracula");
    ASSERT_TRUE(entry_opt.has_value());
    EXPECT_EQ(entry_opt->name, "Dracula Theme");
}

TEST_F(PluginSystemTests, PluginManagerIntegrationAndLifecycle)
{
    // Setup PluginManager in isolated sandbox
    auto& manager = PluginManager::instance();
    manager.initialize(m_test_root / ".zde", m_test_root / "bundled");

    EXPECT_EQ(manager.get_zde_home(), m_test_root / ".zde");

    // Verify subdirectories created
    EXPECT_TRUE(std::filesystem::exists(m_test_root / ".zde" / "plugins" / "installed"));
    EXPECT_TRUE(std::filesystem::exists(m_test_root / ".zde" / "tools"));
    EXPECT_TRUE(std::filesystem::exists(m_test_root / ".zde" / "registry"));

    // Verify ToolManager accessible
    auto& tool_mgr = manager.get_tool_manager();
    EXPECT_FALSE(tool_mgr.get_managed_tools_dir().empty());

    // Create a local plugin and install it via PluginManager
    std::filesystem::path dev_plugin = m_test_root / "dev_plugin";
    std::filesystem::create_directories(dev_plugin / "language");
    {
        std::ofstream mf(dev_plugin / "plugin.json");
        mf << R"({
            "id": "org.sample.editor",
            "name": "Editor Addon",
            "version": "0.1.0",
            "capabilities": ["language"]
        })";
    }

    auto install_result = manager.install_from_local(dev_plugin);
    ASSERT_TRUE(install_result.success);
    EXPECT_TRUE(manager.get_registry().has_plugin("org.sample.editor"));

    // Test enable / disable lifecycle
    EXPECT_TRUE(manager.disable_plugin("org.sample.editor"));
    auto p = manager.get_plugin("org.sample.editor");
    ASSERT_NE(p, nullptr);
    EXPECT_FALSE(p->is_enabled());

    EXPECT_TRUE(manager.enable_plugin("org.sample.editor"));
    EXPECT_TRUE(p->is_enabled());

    // Test uninstall
    EXPECT_TRUE(manager.uninstall_plugin("org.sample.editor"));
    EXPECT_FALSE(manager.get_registry().has_plugin("org.sample.editor"));
}

TEST_F(PluginSystemTests, CategoryDeductionAndEmulatorCatalog)
{
    Marketplace::MarketplaceClient client;
    client.ensure_default_catalog_if_empty();

    auto emulators = client.search("category:emulators");
    EXPECT_GE(emulators.size(), 3u);

    bool has_qemu = false;
    for (const auto& e : emulators)
    {
        if (e.id == "org.zenvra.emulator.qemu")
        {
            has_qemu = true;
            EXPECT_EQ(e.category, "emulators");
        }
    }
    EXPECT_TRUE(has_qemu);

    auto lsp_results = client.search("@category:lsp");
    EXPECT_GE(lsp_results.size(), 5u);
}

TEST_F(PluginSystemTests, PluginDrivenLspRegistrationAndLifecycle)
{
    auto& server_reg = Zenvra::Language::Registry::ServerRegistry::instance();
    auto& lsp_mgr = Zenvra::Language::LanguageServerManager::instance();

    // 1. Initially clear any leftover profiles to simulate clean boot
    server_reg.clear_all_profiles();
    EXPECT_FALSE(server_reg.has_profile_for_language("cpp"));
    EXPECT_EQ(server_reg.find_profile_for_filename("main.cpp"), nullptr);
    EXPECT_FALSE(lsp_mgr.is_language_supported("cpp"));

    // 2. Create a C++ LSP plugin in sandbox
    std::filesystem::path cpp_plugin_dir = m_test_root / "plugins" / "lsp" / "org.zenvra.cpp";
    std::filesystem::create_directories(cpp_plugin_dir);

    const std::string cpp_plugin_json = R"({
        "id": "org.zenvra.cpp",
        "name": "C/C++ Support",
        "version": "1.0.0",
        "category": "lsp",
        "capabilities": ["language", "lsp"],
        "dependencies": {
            "tools": [{ "id": "clangd" }]
        }
    })";
    {
        std::ofstream mf(cpp_plugin_dir / "plugin.json");
        mf << cpp_plugin_json;
    }

    auto manifest = *PluginManifest::from_file(cpp_plugin_dir / "plugin.json");
    auto plugin = std::make_shared<Plugin>(manifest, cpp_plugin_dir, PluginSource::Local);

    // 3. Activate plugin via PluginManager
    auto& pm = PluginManager::instance();
    EXPECT_TRUE(pm.activate_plugin(plugin));

    // 4. Verify standard LSP profile registered dynamically from plugin!
    EXPECT_TRUE(server_reg.has_profile_for_language("cpp"));
    EXPECT_TRUE(lsp_mgr.is_language_supported("cpp"));

    const auto* prof = server_reg.find_profile_for_filename("main.cpp");
    ASSERT_NE(prof, nullptr);
    EXPECT_EQ(prof->language_id, "cpp");
    EXPECT_EQ(prof->executable_name, "clangd");
    EXPECT_EQ(prof->plugin_id, "org.zenvra.cpp");

    // 5. Deactivate / uninstall plugin
    pm.deactivate_plugin(plugin);

    // 6. Verify LSP profile is cleanly unregistered and no longer active
    EXPECT_FALSE(server_reg.has_profile_for_language("cpp"));
    EXPECT_FALSE(lsp_mgr.is_language_supported("cpp"));
    EXPECT_EQ(server_reg.find_profile_for_filename("main.cpp"), nullptr);
}

