#include <gtest/gtest.h>

#include "Services/Build/CMakeBuildSystem.h"
#include "Services/Build/CargoBuildSystem.h"
#include "Services/Output/BuildOutput.h"
#include "Services/Output/OutputLogManager.h"
#include "Services/Output/OutputService.h"
#include "Services/Process/ProcessRunner.h"
#include "Services/Project/ProjectDetector.h"
#include "Services/Run/RunConfigurationResolver.h"
#include "Services/Run/RunConfigurationStore.h"
#include "Services/Toolchain/ToolchainDetector.h"

#include <filesystem>
#include <fstream>

class ServicesTests : public ::testing::Test
{
protected:
    std::filesystem::path temp_dir;

    void SetUp() override
    {
        temp_dir = std::filesystem::temp_directory_path() / "zde_services_test";
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
        std::filesystem::create_directories(temp_dir, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
    }

    void write_file(const std::string& rel_path, const std::string& content)
    {
        const auto p = temp_dir / rel_path;
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        f << content;
    }
};

TEST_F(ServicesTests, BuildOutputParserGccAndClangDiagnostics)
{
    const std::string output =
        "src/main.cpp:42:10: error: 'vector' was not declared in this scope\n"
        "src/utils.cpp:18:5: warning: unused parameter 'argc'\n"
        "Random line of compiler noise\n";

    const auto issues = Zenvra::Services::Output::BuildOutputParser::parse(output);
    ASSERT_EQ(issues.size(), 2u);

    EXPECT_EQ(issues[0].file_path, "src/main.cpp");
    EXPECT_EQ(issues[0].line, 42);
    EXPECT_EQ(issues[0].column, 10);
    EXPECT_EQ(issues[0].severity, Zenvra::Services::Output::IssueSeverity::Error);

    EXPECT_EQ(issues[1].file_path, "src/utils.cpp");
    EXPECT_EQ(issues[1].line, 18);
    EXPECT_EQ(issues[1].column, 5);
    EXPECT_EQ(issues[1].severity, Zenvra::Services::Output::IssueSeverity::Warning);
}

TEST_F(ServicesTests, BuildOutputParserMsvcDiagnostics)
{
    const std::string output =
        "C:\\Project\\Main.cpp(102,15): error C2065: 'my_var': undeclared identifier\n";

    const auto issues = Zenvra::Services::Output::BuildOutputParser::parse(output);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].line, 102);
    EXPECT_EQ(issues[0].column, 15);
    EXPECT_EQ(issues[0].severity, Zenvra::Services::Output::IssueSeverity::Error);
}

TEST_F(ServicesTests, OutputServiceLoggingAndRetrieval)
{
    Zenvra::Services::Output::OutputService service;
    service.clear_all();

    service.log_build("Build started for target ZDE");
    service.log_run("Process exited with code 0");

    const auto build_lines = service.get_lines(Zenvra::Services::Output::OutputCategory::Build);
    const auto runner_lines = service.get_lines(Zenvra::Services::Output::OutputCategory::Runner);

    ASSERT_FALSE(build_lines.empty());
    EXPECT_EQ(build_lines.front(), "Build started for target ZDE");

    ASSERT_FALSE(runner_lines.empty());
    EXPECT_EQ(runner_lines.front(), "Process exited with code 0");
}

TEST_F(ServicesTests, BuildSystemsDetectCMakeAndCargo)
{
    write_file("CMakeLists.txt", "project(MyGame CXX)\nadd_executable(MyGame main.cpp)\n");
    write_file("Cargo.toml", "[package]\nname = \"my_crate\"\n");

    Zenvra::Services::Build::CMakeBuildSystem cmake_bs;
    EXPECT_TRUE(cmake_bs.detect(temp_dir));

    Zenvra::Services::Build::CargoBuildSystem cargo_bs;
    EXPECT_TRUE(cargo_bs.detect(temp_dir));

    const auto cmake_targets = cmake_bs.get_targets(temp_dir);
    ASSERT_FALSE(cmake_targets.empty());
    EXPECT_EQ(cmake_targets.front().name, "MyGame");
}

TEST_F(ServicesTests, RunConfigurationResolverPythonScript)
{
    write_file("script.py", "print('hello from test')\n");

    Zenvra::Services::Run::RunConfiguration cfg;
    cfg.name = "My Script";
    cfg.type = Zenvra::Services::Run::ExecutionType::InterpreterScript;
    cfg.target_file = "script.py";
    cfg.arguments = {"--verbose", "--flag"};
    cfg.working_directory = temp_dir;

    const auto resolved = Zenvra::Services::Run::RunConfigurationResolver::resolve(cfg, temp_dir);
    EXPECT_TRUE(resolved.error_message.empty());
    EXPECT_FALSE(resolved.needs_build);
    EXPECT_FALSE(resolved.process_spec.executable.empty());

    ASSERT_GE(resolved.process_spec.arguments.size(), 3u);
    EXPECT_NE(resolved.process_spec.arguments[0].find("script.py"), std::string::npos);
    EXPECT_EQ(resolved.process_spec.arguments[1], "--verbose");
    EXPECT_EQ(resolved.process_spec.arguments[2], "--flag");
}

TEST_F(ServicesTests, RunConfigurationStorePopulateDefaults)
{
    write_file("CMakeLists.txt", "project(Engine CXX)\nadd_executable(EngineApp main.cpp)\n");
    write_file("app.py", "print('hi')\n");

    const auto info = Zenvra::Services::Project::ProjectDetector::detect(temp_dir);

    Zenvra::Services::Run::RunConfigurationStore store;
    store.populate_defaults(info, temp_dir);

    const auto& cfgs = store.get_configurations();
    ASSERT_GE(cfgs.size(), 2u);

    bool found_binary = false;
    bool found_script = false;

    for (const auto& c : cfgs)
    {
        if (c.type == Zenvra::Services::Run::ExecutionType::BinaryExecutable && c.name == "EngineApp")
        {
            found_binary = true;
            EXPECT_TRUE(c.build_before_run);
        }
        if (c.type == Zenvra::Services::Run::ExecutionType::InterpreterScript && c.name == "app.py")
        {
            found_script = true;
            EXPECT_FALSE(c.build_before_run);
        }
    }

    EXPECT_TRUE(found_binary);
    EXPECT_TRUE(found_script);
}
