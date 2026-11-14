#include <gtest/gtest.h>

#include "Services/Language/Definitions/LanguageDefinition.h"
#include "Services/Language/Detection/LanguageDetector.h"
#include "Services/Language/Registry/LanguageRegistry.h"
#include "Services/Language/Registry/LanguageValidator.h"
#include "Language/LanguageConfiguration.h"

using namespace Zenvra::Services::Language::Definitions;
using namespace Zenvra::Services::Language::Detection;
using namespace Zenvra::Services::Language::Registry;

TEST(LanguageRegistryTests, BuiltinLanguagesRegistered)
{
    auto& reg = LanguageRegistry::instance();
    EXPECT_TRUE(reg.has_language("cpp"));
    EXPECT_TRUE(reg.has_language("rust"));
    EXPECT_TRUE(reg.has_language("python"));
    EXPECT_TRUE(reg.has_language("typescript"));
    EXPECT_TRUE(reg.has_language("javascript"));
    EXPECT_TRUE(reg.has_language("go"));
    EXPECT_TRUE(reg.has_language("java"));
    EXPECT_TRUE(reg.has_language("cmake"));
    EXPECT_TRUE(reg.has_language("html"));
    EXPECT_TRUE(reg.has_language("css"));
    EXPECT_TRUE(reg.has_language("json"));
    EXPECT_TRUE(reg.has_language("shell"));
    EXPECT_TRUE(reg.has_language("php"));
    EXPECT_TRUE(reg.has_language("yaml"));
    EXPECT_TRUE(reg.has_language("lua"));
    EXPECT_TRUE(reg.has_language("zig"));
}

TEST(LanguageRegistryTests, DetectByExtension)
{
    auto& reg = LanguageRegistry::instance();

    EXPECT_EQ(reg.detect_language("main.cpp").language_id, "cpp");
    EXPECT_EQ(reg.detect_language("lib.rs").language_id, "rust");
    EXPECT_EQ(reg.detect_language("script.py").language_id, "python");
    EXPECT_EQ(reg.detect_language("app.ts").language_id, "typescript");
    EXPECT_EQ(reg.detect_language("server.go").language_id, "go");
    EXPECT_EQ(reg.detect_language("build.zig").language_id, "zig");
}

TEST(LanguageRegistryTests, DetectBySpecialFilename)
{
    auto& reg = LanguageRegistry::instance();

    EXPECT_EQ(reg.detect_language("CMakeLists.txt").language_id, "cmake");
    EXPECT_EQ(reg.detect_language("cmakelists.txt").language_id, "cmake");
    EXPECT_EQ(reg.detect_language("Cargo.lock").language_id, "rust");
    EXPECT_EQ(reg.detect_language("package.json").language_id, "json");
    EXPECT_EQ(reg.detect_language("tsconfig.json").language_id, "typescript");
}

TEST(LanguageRegistryTests, DetectByFirstLinePattern)
{
    auto& reg = LanguageRegistry::instance();

    const std::string python_script = "#!/usr/bin/env python3\nprint('hello')";
    auto result_py = reg.detect_language("mystery_script", python_script);
    EXPECT_EQ(result_py.language_id, "python");
    EXPECT_EQ(result_py.reason, DetectionMatchReason::FirstLinePattern);

    const std::string bash_script = "#!/bin/bash\necho hello";
    auto result_sh = reg.detect_language("run_tool", bash_script);
    EXPECT_EQ(result_sh.language_id, "shell");
    EXPECT_EQ(result_sh.reason, DetectionMatchReason::FirstLinePattern);
}

TEST(LanguageRegistryTests, DetectStlHeaderWithoutExtension)
{
    auto& reg = LanguageRegistry::instance();

    auto result_vector = reg.detect_language("vector");
    EXPECT_EQ(result_vector.language_id, "cpp");
    EXPECT_EQ(result_vector.reason, DetectionMatchReason::ContentHeuristic);

    auto result_algo = reg.detect_language("algorithm");
    EXPECT_EQ(result_algo.language_id, "cpp");
}

TEST(LanguageRegistryTests, UserOverrideAssociations)
{
    auto& reg = LanguageRegistry::instance();

    reg.detector().set_user_association("*.mycustom", "python");
    EXPECT_EQ(reg.detect_language("data.mycustom").language_id, "python");

    reg.detector().set_user_association("special_config", "json");
    EXPECT_EQ(reg.detect_language("special_config").language_id, "json");

    reg.detector().clear_user_associations();
}

TEST(LanguageRegistryTests, EditorSettingsResolution)
{
    auto& reg = LanguageRegistry::instance();

    auto cpp_settings = reg.get_editor_settings("cpp");
    EXPECT_EQ(cpp_settings.tab_size, 4u);
    EXPECT_EQ(cpp_settings.line_comment, "//");
    EXPECT_EQ(cpp_settings.block_comment_start, "/*");
    EXPECT_EQ(cpp_settings.block_comment_end, "*/");

    auto py_settings = reg.get_editor_settings("python");
    EXPECT_EQ(py_settings.tab_size, 4u);
    EXPECT_EQ(py_settings.line_comment, "#");

    auto ts_settings = reg.get_editor_settings("typescript");
    EXPECT_EQ(ts_settings.tab_size, 2u);
    EXPECT_EQ(ts_settings.line_comment, "//");

    auto html_settings = reg.get_editor_settings("html");
    EXPECT_EQ(html_settings.tab_size, 2u);
    EXPECT_EQ(html_settings.line_comment, "<!--");
}

TEST(LanguageRegistryTests, BackwardCompatibilityLanguageConfiguration)
{
    // Test that the legacy Zenvra::Language::LanguageConfiguration API routes to the unified registry
    auto config_cpp = Zenvra::Language::LanguageConfiguration::get_for_extension(".cpp");
    EXPECT_EQ(config_cpp.tab_size, 4u);
    EXPECT_EQ(config_cpp.line_comment, "//");

    auto config_py = Zenvra::Language::LanguageConfiguration::get_for_extension(".py");
    EXPECT_EQ(config_py.tab_size, 4u);
    EXPECT_EQ(config_py.line_comment, "#");

    auto config_ts = Zenvra::Language::LanguageConfiguration::get_for_extension(".ts");
    EXPECT_EQ(config_ts.tab_size, 2u);
    EXPECT_EQ(config_ts.line_comment, "//");

    auto config_file = Zenvra::Language::LanguageConfiguration::get_for_file("CMakeLists.txt");
    EXPECT_EQ(config_file.tab_size, 4u);
    EXPECT_EQ(config_file.line_comment, "#");
}

TEST(LanguageRegistryTests, ValidationPreventsOverridingBuiltin)
{
    LanguageDefinition malicious_override;
    malicious_override.id = "cpp"; // Builtin ID
    malicious_override.is_builtin = false;
    malicious_override.plugin_id = "malicious.plugin";

    auto& reg = LanguageRegistry::instance();
    // Registering external plugin that overrides protected built-in definition should fail
    bool registered = reg.register_language(malicious_override);
    EXPECT_FALSE(registered);
}

