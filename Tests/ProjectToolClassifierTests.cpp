#include <gtest/gtest.h>

#include "Application/ViewModels/MainToolbarViewModel.h"
#include "Application/ViewModels/StudioViewModel.h"
#include "Services/Project/ProjectDetector.h"
#include "Tools/Classification/ProjectToolClassifier.h"
#include "UI/Components/MenuModel.h"
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

TEST_F(ProjectToolClassifierTest, CMakeMultipleExecutableTargetsDiscovery)
{
    write_file("CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n"
               "project(MySuite LANGUAGES CXX)\n"
               "add_executable(SuiteServer src/server.cpp)\n"
               "add_executable(SuiteClient src/client.cpp)\n"
               "add_executable(SuiteTests tests/tests.cpp)\n");

    const auto exec_targets = ProjectToolClassifier::detect_cmake_executable_targets(temp_dir);
    ASSERT_EQ(exec_targets.size(), 3u);
    EXPECT_EQ(exec_targets[0], "SuiteServer");
    EXPECT_EQ(exec_targets[1], "SuiteClient");
    EXPECT_EQ(exec_targets[2], "SuiteTests");

    const auto configs = ProjectToolClassifier::detect_configurations(temp_dir);
    bool found_server = false;
    bool found_client = false;
    bool found_tests = false;
    for (const auto& c : configs)
    {
        if (c.name == "SuiteServer") found_server = true;
        if (c.name == "SuiteClient") found_client = true;
        if (c.name == "SuiteTests") found_tests = true;
    }
    EXPECT_TRUE(found_server);
    EXPECT_TRUE(found_client);
    EXPECT_TRUE(found_tests);
}

TEST_F(ProjectToolClassifierTest, PythonSameNameProjectScriptAlignment)
{
    write_file("pyproject.toml", "[project]\nname = \"task_runner\"\nversion = \"0.1.0\"\n");
    write_file("task_runner.py", "print('running task')\n");

    const auto py_targets = ProjectToolClassifier::detect_python_targets(temp_dir);
    ASSERT_FALSE(py_targets.empty());

    EXPECT_EQ(py_targets.front().name, "task_runner");
    EXPECT_EQ(py_targets.front().executable_path, "task_runner.py");
    EXPECT_EQ(py_targets.front().classification, ToolClassification::Python);
}

TEST_F(ProjectToolClassifierTest, PythonDifferingNameProjectScriptHandling)
{
    write_file("pyproject.toml", "[project]\nname = \"ecommerce_backend\"\nversion = \"1.0.0\"\n");
    write_file("server.py", "print('server starting')\n");
    write_file("worker.py", "print('worker starting')\n");

    const auto py_targets = ProjectToolClassifier::detect_python_targets(temp_dir);
    ASSERT_GE(py_targets.size(), 2u);

    bool found_server = false;
    bool found_worker = false;
    bool has_fake_main = false;

    for (const auto& t : py_targets)
    {
        if (t.name == "ecommerce_backend (server.py)" && t.executable_path == "server.py")
        {
            found_server = true;
        }
        if (t.name == "ecommerce_backend (worker.py)" && t.executable_path == "worker.py")
        {
            found_worker = true;
        }
        if (t.name == "main.py" || t.executable_path == "main.py")
        {
            has_fake_main = true;
        }
    }

    EXPECT_TRUE(found_server);
    EXPECT_TRUE(found_worker);
    EXPECT_FALSE(has_fake_main); // Must NOT hardcode non-existent main.py!
}

TEST_F(ProjectToolClassifierTest, PythonVirtualEnvironmentResolution)
{
    std::error_code ec;
#if defined(_WIN32)
    std::filesystem::create_directories(temp_dir / ".venv" / "Scripts", ec);
    write_file(".venv/Scripts/python.exe", "fake_binary");
#else
    std::filesystem::create_directories(temp_dir / ".venv" / "bin", ec);
    write_file(".venv/bin/python", "fake_binary");
#endif

    const std::string resolved = ProjectToolClassifier::resolve_python_interpreter(temp_dir);
    EXPECT_NE(resolved, "python");
    EXPECT_NE(resolved, "python3");
    EXPECT_NE(resolved.find(".venv"), std::string::npos);

    // Verify create_run_command uses resolved interpreter
    BinaryTargetProfile profile;
    profile.name = "app.py";
    profile.classification = ToolClassification::Python;
    profile.executable_path = "app.py";

    const auto cmd = ProjectToolClassifier::create_run_command(profile, temp_dir);
    EXPECT_EQ(cmd.program, resolved);
    ASSERT_FALSE(cmd.arguments.empty());
    EXPECT_EQ(cmd.arguments.front(), "app.py");
}

TEST_F(ProjectToolClassifierTest, PolyglotMultiTargetWorkspaceDetection)
{
    write_file("CMakeLists.txt", "project(CppEngine CXX)\nadd_executable(CppEngine main.cpp)\n");
    write_file("Cargo.toml", "[package]\nname = \"rust_cli\"\n");
    write_file("app.py", "print('polyglot python')\n");

    const auto targets = ProjectToolClassifier::detect_configurations(temp_dir);
    EXPECT_GE(targets.size(), 3u);

    bool found_cmake = false;
    bool found_cargo = false;
    bool found_python = false;

    for (const auto& t : targets)
    {
        if (t.classification == ToolClassification::CMake && t.name == "CppEngine") found_cmake = true;
        if (t.classification == ToolClassification::TomlCargo && t.name == "rust_cli") found_cargo = true;
        if (t.classification == ToolClassification::Python && t.name == "app.py") found_python = true;
    }

    EXPECT_TRUE(found_cmake);
    EXPECT_TRUE(found_cargo);
    EXPECT_TRUE(found_python);
}

TEST_F(ProjectToolClassifierTest, StudioViewModelTargetManagementAndSwitching)
{
    write_file("CMakeLists.txt", "project(CppEngine CXX)\nadd_executable(CppEngine main.cpp)\n");
    write_file("Cargo.toml", "[package]\nname = \"rust_cli\"\n");
    write_file("app.py", "print('polyglot python')\n");

    Zenvra::Application::ViewModels::StudioViewModel vm(
        Zenvra::Application::ViewModels::StudioActions{},
        temp_dir);

    const auto& available = vm.get_available_targets();
    ASSERT_GE(available.size(), 3u);

    // Initial primary target should be first discovered (CMake)
    EXPECT_EQ(vm.get_active_target(), "CppEngine");
    EXPECT_EQ(vm.get_active_classification(), ToolClassification::CMake);

    // Switch to Python via classification
    EXPECT_TRUE(vm.select_target_by_classification(ToolClassification::Python));
    EXPECT_EQ(vm.get_active_target(), "app.py");
    EXPECT_EQ(vm.get_active_classification(), ToolClassification::Python);
    EXPECT_EQ(vm.get_active_executable_path(), "app.py");

    // Switch to Cargo via name
    EXPECT_TRUE(vm.select_target_by_name("rust_cli"));
    EXPECT_EQ(vm.get_active_target(), "rust_cli");
    EXPECT_EQ(vm.get_active_classification(), ToolClassification::TomlCargo);

    // Active profile
    const auto* active_prof = vm.get_active_profile();
    ASSERT_NE(active_prof, nullptr);
    EXPECT_EQ(active_prof->name, "rust_cli");
}

TEST_F(ProjectToolClassifierTest, MainToolbarViewModelSyncWithStudio)
{
    write_file("CMakeLists.txt", "project(VoxelEditor CXX)\n");

    Zenvra::Application::ViewModels::StudioViewModel studio_vm(
        Zenvra::Application::ViewModels::StudioActions{},
        temp_dir);

    studio_vm.set_active_mode("Release");
    studio_vm.set_active_arch("x86_64");

    Zenvra::Application::ViewModels::MainToolbarViewModel toolbar_vm;
    toolbar_vm.sync_with_studio(studio_vm);

    EXPECT_EQ(toolbar_vm.get_toolbar().get_run_config_widget().get_state().active_target_name, "VoxelEditor");
    EXPECT_EQ(toolbar_vm.get_toolbar().get_run_config_widget().get_state().active_mode, BuildConfigurationMode::Release);
    EXPECT_EQ(toolbar_vm.get_toolbar().get_run_config_widget().get_state().active_architecture, TargetArchitecture::X86_64);
}

TEST_F(ProjectToolClassifierTest, ServicesProjectDetectorDelegatesAndIntegrates)
{
    write_file("CMakeLists.txt", "project(GameCore CXX)\nadd_executable(GameClient main.cpp)\n");
    write_file("app.py", "print('hello')\n");

    const auto project_info = Zenvra::Services::Project::ProjectDetector::detect(temp_dir);
    EXPECT_FALSE(project_info.targets.empty());
    EXPECT_TRUE(project_info.has_build_system);

    bool found_client = false;
    bool found_python = false;
    for (const auto& target : project_info.targets)
    {
        if (target.name == "GameClient")
        {
            found_client = true;
            EXPECT_EQ(target.execution_model, Zenvra::Services::Project::ExecutionModel::CompiledBinary);
        }
        if (target.name == "app.py")
        {
            found_python = true;
            EXPECT_EQ(target.execution_model, Zenvra::Services::Project::ExecutionModel::InterpreterScript);
        }
    }
    EXPECT_TRUE(found_client);
    EXPECT_TRUE(found_python);
}

TEST_F(ProjectToolClassifierTest, EmptyWorkspaceRunConfigurationMenuIsClean)
{
    // Empty workspace root should NOT scan CWD or leak internal IDE binaries
    Zenvra::Application::ViewModels::StudioViewModel vm(
        Zenvra::Application::ViewModels::StudioActions{},
        std::filesystem::path{});

    EXPECT_TRUE(vm.get_available_targets().empty());
    EXPECT_TRUE(vm.get_active_target().empty());

    const auto menus = Zenvra::UI::Components::get_window_menus();
    ASSERT_GT(menus.size(), 12u);
    const auto& binary_menu = menus[12];
    ASSERT_GE(binary_menu.items.size(), 3u);

    // Clean placeholder state
    EXPECT_EQ(binary_menu.items[0].label, "No Configurations");
    EXPECT_TRUE(binary_menu.items[0].command_id.empty());
    EXPECT_TRUE(binary_menu.items[1].separator);
    EXPECT_EQ(binary_menu.items[2].label, "Edit Configurations...");

    // Ensure no hardcoded or leaked IDE targets exist in the menu
    for (const auto& item : binary_menu.items)
    {
        EXPECT_NE(item.label, "ZDE");
        EXPECT_NE(item.label, "ZDEUnitTests");
        EXPECT_NE(item.label, "[Clang/CMake] ZDE");
        EXPECT_NE(item.label, "[Clang/CMake] ZDEUnitTests");
    }
}

TEST_F(ProjectToolClassifierTest, DynamicBinaryTargetsUpdatedOnFolderOpenAndResetOnClose)
{
    write_file("CMakeLists.txt", "project(SuperGame CXX)\nadd_executable(SuperGame main.cpp)\n");

    Zenvra::Application::ViewModels::StudioViewModel vm(
        Zenvra::Application::ViewModels::StudioActions{},
        std::filesystem::path{});

    EXPECT_TRUE(vm.get_available_targets().empty());

    // Open project folder
    vm.configure_for_workspace(temp_dir);
    EXPECT_FALSE(vm.get_available_targets().empty());
    EXPECT_EQ(vm.get_active_target(), "SuperGame");

    auto menus = Zenvra::UI::Components::get_window_menus();
    ASSERT_GT(menus.size(), 12u);
    bool found_target = false;
    for (const auto& item : menus[12].items)
    {
        if (item.label.find("SuperGame") != std::string::npos)
        {
            found_target = true;
            break;
        }
    }
    EXPECT_TRUE(found_target);

    // Close project folder
    vm.configure_for_workspace({});
    EXPECT_TRUE(vm.get_available_targets().empty());
    EXPECT_TRUE(vm.get_active_target().empty());

    menus = Zenvra::UI::Components::get_window_menus();
    ASSERT_GE(menus[12].items.size(), 3u);
    EXPECT_EQ(menus[12].items[0].label, "No Configurations");
    EXPECT_TRUE(menus[12].items[0].command_id.empty());
    EXPECT_EQ(menus[12].items[2].label, "Edit Configurations...");
}

TEST_F(ProjectToolClassifierTest, CMakeVariableTargetAndPrebuiltBinaryDetection)
{
    // Simulating user project: volumetric-vulkan-rendering
    write_file("CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(\"volumetric\" LANGUAGES CXX)\n"
        "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_SOURCE_DIR}/\")\n"
        "add_executable(${PROJECT_NAME} WIN32 main.cpp)\n");

    std::error_code ec;
    std::filesystem::create_directories(temp_dir / "build" / "Debug", ec);
#if defined(_WIN32)
    write_file("build/Debug/volumetric.exe", "MZ fake binary");
#else
    write_file("build/Debug/volumetric", "#!/bin/sh\necho fake");
    std::filesystem::permissions(temp_dir / "build" / "Debug" / "volumetric",
        std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
#endif

    const auto targets = ProjectToolClassifier::detect_configurations(temp_dir);
    ASSERT_FALSE(targets.empty());

    EXPECT_EQ(targets.front().name, "volumetric");
    EXPECT_EQ(targets.front().classification, ToolClassification::CMake);
    EXPECT_FALSE(targets.front().executable_path.empty());

    const auto cmd = ProjectToolClassifier::create_run_command(targets.front(), temp_dir, "windows-clang-ninja-debug", "Debug");
    EXPECT_FALSE(cmd.program.empty());
    EXPECT_NE(cmd.program.find("volumetric"), std::string::npos);
}

TEST_F(ProjectToolClassifierTest, PrecompiledPolyglotBinariesAutoConfiguration)
{
    std::error_code ec;
    std::filesystem::create_directories(temp_dir / "target" / "debug", ec);
    write_file("Cargo.toml", "[package]\nname = \"rusty\"\n[[bin]]\nname = \"rusty_tool\"\n");
#if defined(_WIN32)
    write_file("target/debug/rusty_tool.exe", "MZ fake binary");
    std::filesystem::create_directories(temp_dir / "bin", ec);
    write_file("bin/custom_zig.exe", "MZ fake binary");
#else
    write_file("target/debug/rusty_tool", "#!/bin/sh");
    std::filesystem::create_directories(temp_dir / "bin", ec);
    write_file("bin/custom_zig", "#!/bin/sh");
    std::filesystem::permissions(temp_dir / "target" / "debug" / "rusty_tool",
        std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
    std::filesystem::permissions(temp_dir / "bin" / "custom_zig",
        std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
#endif

    const auto targets = ProjectToolClassifier::detect_configurations(temp_dir);
    EXPECT_FALSE(targets.empty());

    bool found_rust = false;
    bool found_custom = false;
    for (const auto& t : targets)
    {
        if (t.name == "rusty_tool") found_rust = true;
        if (t.name == "custom_zig") found_custom = true;
    }
    EXPECT_TRUE(found_rust);
    EXPECT_TRUE(found_custom);
}

TEST_F(ProjectToolClassifierTest, DeepDirectoriesWorkspaceSwitchDoesNotCrash)
{
    std::error_code ec;
    // Create deep nested directories (previously would trigger it.pop() crash in recursive_directory_iterator)
    std::filesystem::create_directories(temp_dir / "Source" / "Level1" / "Level2" / "Level3" / "Level4" / "Level5", ec);
    std::filesystem::create_directories(temp_dir / ".git" / "objects" / "pack", ec);
    std::filesystem::create_directories(temp_dir / "node_modules" / "deep_pkg" / "nested" / "dir", ec);
    std::filesystem::create_directories(temp_dir / "build" / "nested" / "subdir" / "deep", ec);

    // Write a top level and nested binaries
#if defined(_WIN32)
    write_file("build/app.exe", "MZ fake app");
#else
    write_file("build/app", "#!/bin/sh");
    std::filesystem::permissions(temp_dir / "build" / "app",
        std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
#endif

    Zenvra::Application::ViewModels::StudioViewModel vm(
        Zenvra::Application::ViewModels::StudioActions{},
        std::filesystem::path{});
    // 1. Open project with deep structure
    EXPECT_NO_THROW(vm.configure_for_workspace(temp_dir));
    EXPECT_FALSE(vm.get_available_targets().empty());

    // 2. Close project
    EXPECT_NO_THROW(vm.configure_for_workspace({}));
    EXPECT_TRUE(vm.get_available_targets().empty());

    // 3. Open another project folder
    const auto second_dir = temp_dir / "SecondProject";
    std::filesystem::create_directories(second_dir / "build", ec);
#if defined(_WIN32)
    std::ofstream(second_dir / "build" / "other.exe") << "MZ fake other";
#else
    std::ofstream(second_dir / "build" / "other") << "#!/bin/sh";
    std::filesystem::permissions(second_dir / "build" / "other",
        std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
#endif
    EXPECT_NO_THROW(vm.configure_for_workspace(second_dir));
    EXPECT_FALSE(vm.get_available_targets().empty());
}

