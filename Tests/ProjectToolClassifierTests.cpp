#include <gtest/gtest.h>

#include "Application/ViewModels/StudioViewModel.h"
#include "Tools/Classification/ProjectToolClassifier.h"
#include "UI/Toolbar/ToolbarTypes.h"

#include <filesystem>
#include <fstream>

using namespace Zenvra::Tools::Classification;
using namespace Zenvra::UI::Toolbar;

class ProjectToolClassifierTest : public ::testing::Test
{
protected:
    std::filesystem::path temp_dir;

    void SetUp() override
    {
        temp_dir = std::filesystem::temp_directory_path() / "zde_classifier_test";
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
        std::filesystem::create_directories(temp_dir, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
    }

    void touch_file(const std::string& name)
    {
        std::ofstream ofs(temp_dir / name);
        ofs << "\n";
    }

    void write_file(const std::string& name, const std::string& content)
    {
        std::ofstream ofs(temp_dir / name);
        ofs << content;
    }
};

TEST_F(ProjectToolClassifierTest, ClassifiesFilesCorrectly)
{
    // CMake & C/C++
    EXPECT_EQ(ProjectToolClassifier::classify_file("CMakeLists.txt"), ToolClassification::CMake);
    EXPECT_EQ(ProjectToolClassifier::classify_file("src/main.cpp"), ToolClassification::CMake);
    EXPECT_EQ(ProjectToolClassifier::classify_file("module.cmake"), ToolClassification::CMake);
    EXPECT_EQ(ProjectToolClassifier::classify_file("test.cxx"), ToolClassification::CMake);

    // Cargo & TOML
    EXPECT_EQ(ProjectToolClassifier::classify_file("Cargo.toml"), ToolClassification::TomlCargo);
    EXPECT_EQ(ProjectToolClassifier::classify_file("src/lib.rs"), ToolClassification::TomlCargo);
    EXPECT_EQ(ProjectToolClassifier::classify_file("config.toml"), ToolClassification::TomlCargo);

    // Python
    EXPECT_EQ(ProjectToolClassifier::classify_file("script.py"), ToolClassification::Python);
    EXPECT_EQ(ProjectToolClassifier::classify_file("pyproject.toml"), ToolClassification::Python);
    EXPECT_EQ(ProjectToolClassifier::classify_file("requirements.txt"), ToolClassification::Python);
    EXPECT_EQ(ProjectToolClassifier::classify_file("app.pyw"), ToolClassification::Python);

    // Java
    EXPECT_EQ(ProjectToolClassifier::classify_file("Main.java"), ToolClassification::Java);
    EXPECT_EQ(ProjectToolClassifier::classify_file("pom.xml"), ToolClassification::Java);
    EXPECT_EQ(ProjectToolClassifier::classify_file("build.gradle"), ToolClassification::Java);

    // Pascal
    EXPECT_EQ(ProjectToolClassifier::classify_file("program.pas"), ToolClassification::Pascal);
    EXPECT_EQ(ProjectToolClassifier::classify_file("unit.pp"), ToolClassification::Pascal);
    EXPECT_EQ(ProjectToolClassifier::classify_file("project.lpi"), ToolClassification::Pascal);
    EXPECT_EQ(ProjectToolClassifier::classify_file("app.dpr"), ToolClassification::Pascal);
}

TEST_F(ProjectToolClassifierTest, ResolvesCorrectIcons)
{
    EXPECT_EQ(get_classification_icon(ToolClassification::CMake), "Assets/icons/material-icon-theme/cmake.svg");
    EXPECT_EQ(get_classification_icon(ToolClassification::TomlCargo), "Assets/icons/material-icon-theme/toml.svg");
    EXPECT_EQ(get_classification_icon(ToolClassification::Pascal), "Assets/icons/material-icon-theme/pascal.svg");
    EXPECT_EQ(get_classification_icon(ToolClassification::Java), "Assets/icons/material-icon-theme/java.svg");
    EXPECT_EQ(get_classification_icon(ToolClassification::Python), "Assets/icons/material-icon-theme/python.svg");
    EXPECT_EQ(get_classification_icon(ToolClassification::CustomExecutable), "Assets/icons/terminal.svg");
}

TEST_F(ProjectToolClassifierTest, CreatesProfileForFile)
{
    const auto py_profile = ProjectToolClassifier::create_profile_for_file("my_script.py");
    EXPECT_EQ(py_profile.name, "my_script.py");
    EXPECT_EQ(py_profile.classification, ToolClassification::Python);
    EXPECT_EQ(py_profile.icon_asset, "Assets/icons/material-icon-theme/python.svg");

    const auto java_profile = ProjectToolClassifier::create_profile_for_file("App.java");
    EXPECT_EQ(java_profile.name, "App.java");
    EXPECT_EQ(java_profile.classification, ToolClassification::Java);
    EXPECT_EQ(java_profile.icon_asset, "Assets/icons/material-icon-theme/java.svg");

    const auto pas_profile = ProjectToolClassifier::create_profile_for_file("test.pas");
    EXPECT_EQ(pas_profile.name, "test.pas");
    EXPECT_EQ(pas_profile.classification, ToolClassification::Pascal);
    EXPECT_EQ(pas_profile.icon_asset, "Assets/icons/material-icon-theme/pascal.svg");
}

TEST_F(ProjectToolClassifierTest, DetectsWorkspaceConfigurations)
{
    touch_file("CMakeLists.txt");
    touch_file("Cargo.toml");
    touch_file("main.py");
    touch_file("pom.xml");
    touch_file("program.pas");

    const auto targets = ProjectToolClassifier::detect_configurations(temp_dir);
    EXPECT_GE(targets.size(), 5u);

    bool found_cmake = false;
    bool found_cargo = false;
    bool found_python = false;
    bool found_java = false;
    bool found_pascal = false;

    for (const auto& t : targets)
    {
        if (t.classification == ToolClassification::CMake) found_cmake = true;
        if (t.classification == ToolClassification::TomlCargo) found_cargo = true;
        if (t.classification == ToolClassification::Python) found_python = true;
        if (t.classification == ToolClassification::Java) found_java = true;
        if (t.classification == ToolClassification::Pascal) found_pascal = true;
    }

    EXPECT_TRUE(found_cmake);
    EXPECT_TRUE(found_cargo);
    EXPECT_TRUE(found_python);
    EXPECT_TRUE(found_java);
    EXPECT_TRUE(found_pascal);
}

TEST_F(ProjectToolClassifierTest, ActiveFileContextSurfacesAsFirstTarget)
{
    touch_file("CMakeLists.txt");

    const auto targets = ProjectToolClassifier::detect_configurations(temp_dir, "analysis.py");
    ASSERT_FALSE(targets.empty());

    EXPECT_EQ(targets.front().name, "analysis.py");
    EXPECT_EQ(targets.front().classification, ToolClassification::Python);
    EXPECT_EQ(targets.front().icon_asset, "Assets/icons/material-icon-theme/python.svg");
    EXPECT_TRUE(targets.front().is_default);
}

TEST_F(ProjectToolClassifierTest, GeneratesRunAndDebugCommands)
{
    // Python
    BinaryTargetProfile py_target;
    py_target.name = "test.py";
    py_target.classification = ToolClassification::Python;
    py_target.executable_path = "test.py";

    const auto py_run = ProjectToolClassifier::create_run_command(py_target, temp_dir);
#if defined(_WIN32)
    EXPECT_EQ(py_run.program, "python");
#else
    EXPECT_EQ(py_run.program, "python3");
#endif
    ASSERT_FALSE(py_run.arguments.empty());
    EXPECT_EQ(py_run.arguments.front(), "test.py");

    const auto py_dbg = ProjectToolClassifier::create_debug_command(py_target, temp_dir);
    ASSERT_GE(py_dbg.arguments.size(), 2u);
    EXPECT_EQ(py_dbg.arguments[0], "-m");
    EXPECT_EQ(py_dbg.arguments[1], "pdb");

    // Cargo
    BinaryTargetProfile cargo_target;
    cargo_target.name = "Cargo: Run";
    cargo_target.classification = ToolClassification::TomlCargo;

    const auto cargo_run = ProjectToolClassifier::create_run_command(cargo_target, temp_dir);
    EXPECT_EQ(cargo_run.program, "cargo");
    ASSERT_FALSE(cargo_run.arguments.empty());
    EXPECT_EQ(cargo_run.arguments.front(), "run");

    const auto cargo_dbg = ProjectToolClassifier::create_debug_command(cargo_target, temp_dir);
    EXPECT_EQ(cargo_dbg.program, "cargo");
    ASSERT_FALSE(cargo_dbg.arguments.empty());
    EXPECT_EQ(cargo_dbg.arguments.front(), "build");

    // Pascal
    BinaryTargetProfile pas_target;
    pas_target.name = "app.pas";
    pas_target.classification = ToolClassification::Pascal;
    pas_target.executable_path = "app.pas";

    const auto pas_run = ProjectToolClassifier::create_run_command(pas_target, temp_dir);
    EXPECT_EQ(pas_run.program, "fpc");

    const auto pas_dbg = ProjectToolClassifier::create_debug_command(pas_target, temp_dir);
    EXPECT_EQ(pas_dbg.program, "gdb");
}

TEST_F(ProjectToolClassifierTest, DetectsManifestProjectNames)
{
    // CMake project name
    write_file("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(MyAwesomeEngine LANGUAGES CXX)\n");
    EXPECT_EQ(ProjectToolClassifier::detect_cmake_project_name(temp_dir), "MyAwesomeEngine");

    // Cargo TOML project name
    write_file("Cargo.toml", "[package]\nname = \"hyper_server\"\nversion = \"0.1.0\"\n");
    EXPECT_EQ(ProjectToolClassifier::detect_cargo_package_name(temp_dir), "hyper_server");

    // Java Maven artifact name
    write_file("pom.xml", "<project><modelVersion>4.0.0</modelVersion><artifactId>spring-cloud-api</artifactId></project>");
    EXPECT_EQ(ProjectToolClassifier::detect_java_project_name(temp_dir), "spring-cloud-api");

    // Java Gradle settings project name
    std::filesystem::remove(temp_dir / "pom.xml");
    write_file("settings.gradle", "rootProject.name = 'gradle-backend'\n");
    EXPECT_EQ(ProjectToolClassifier::detect_java_project_name(temp_dir), "gradle-backend");

    // Python pyproject.toml name
    write_file("pyproject.toml", "[project]\nname = \"neural-classifier\"\nversion = \"0.1.0\"\n");
    EXPECT_EQ(ProjectToolClassifier::detect_python_project_name(temp_dir), "neural-classifier");

    // Pascal program name
    write_file("calc.pas", "program MathCalculator;\nbegin\nend.\n");
    EXPECT_EQ(ProjectToolClassifier::detect_pascal_project_name(temp_dir), "MathCalculator");
}

TEST_F(ProjectToolClassifierTest, DetectConfigurationsUsesParsedProjectNames)
{
    write_file("CMakeLists.txt", "project(VoxelSandbox CXX)\n");
    write_file("Cargo.toml", "[package]\nname = \"voxel_core\"\n");
    write_file("pom.xml", "<project><artifactId>voxel-auth</artifactId></project>");

    const auto targets = ProjectToolClassifier::detect_configurations(temp_dir);
    ASSERT_FALSE(targets.empty());

    bool found_voxel_sandbox = false;
    bool found_voxel_core = false;
    bool found_voxel_auth = false;

    for (const auto& t : targets)
    {
        if (t.classification == ToolClassification::CMake && t.name == "VoxelSandbox")
        {
            found_voxel_sandbox = true;
        }
        if (t.classification == ToolClassification::TomlCargo && t.name == "voxel_core")
        {
            found_voxel_core = true;
        }
        if (t.classification == ToolClassification::Java && t.name == "voxel-auth")
        {
            found_voxel_auth = true;
        }
    }

    EXPECT_TRUE(found_voxel_sandbox);
    EXPECT_TRUE(found_voxel_core);
    EXPECT_TRUE(found_voxel_auth);
}

TEST_F(ProjectToolClassifierTest, StudioViewModelConfiguresFromWorkspaceWithoutHardcodedZDE)
{
    write_file("CMakeLists.txt", "project(GameEngine CXX)\n");

    Zenvra::Application::ViewModels::StudioViewModel vm(
        Zenvra::Application::ViewModels::StudioActions{},
        temp_dir);

    EXPECT_EQ(vm.get_active_target(), "GameEngine");
    EXPECT_EQ(vm.get_active_classification(), ToolClassification::CMake);
    EXPECT_NE(vm.get_active_target(), "ZDE");
}
