#include "Tools/Classification/ProjectToolClassifier.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>

namespace Zenvra::Tools::Classification
{

namespace
{

std::string to_lower_ascii(std::string str)
{
    for (char& c : str)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return str;
}

} // namespace

std::optional<UI::Toolbar::ToolClassification> ProjectToolClassifier::classify_file(
    const std::filesystem::path& file_path)
{
    if (file_path.empty())
    {
        return std::nullopt;
    }

    const std::string filename = to_lower_ascii(file_path.filename().string());
    const std::string ext = to_lower_ascii(file_path.extension().string());

    // 1. CMake & C/C++
    if (filename == "cmakelists.txt" || ext == ".cmake" ||
        ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".c" ||
        ext == ".hpp" || ext == ".hxx" || ext == ".h" || ext == ".inl")
    {
        return UI::Toolbar::ToolClassification::CMake;
    }

    // 2. TOML & Cargo / Rust
    if (filename == "cargo.toml" || filename == "cargo.lock" || ext == ".rs")
    {
        return UI::Toolbar::ToolClassification::TomlCargo;
    }

    // 3. Python
    if (ext == ".py" || ext == ".pyw" || ext == ".ipynb" ||
        filename == "pyproject.toml" || filename == "requirements.txt" ||
        filename == "setup.py" || filename == "pipfile")
    {
        return UI::Toolbar::ToolClassification::Python;
    }

    // 4. Java & JVM
    if (ext == ".java" || ext == ".kt" || ext == ".scala" ||
        filename == "pom.xml" || filename == "build.gradle" ||
        filename == "build.gradle.kts" || filename == "settings.gradle")
    {
        return UI::Toolbar::ToolClassification::Java;
    }

    // 5. Pascal & Delphi / FreePascal
    if (ext == ".pas" || ext == ".pp" || ext == ".lpr" || ext == ".lpi" ||
        ext == ".dpr" || ext == ".dpk" || filename == "fpc.cfg")
    {
        return UI::Toolbar::ToolClassification::Pascal;
    }

    // 6. Generic TOML configuration
    if (ext == ".toml")
    {
        return UI::Toolbar::ToolClassification::TomlCargo;
    }

    return std::nullopt;
}

UI::Toolbar::BinaryTargetProfile ProjectToolClassifier::create_profile_for_file(
    const std::filesystem::path& file_path)
{
    const auto classification = classify_file(file_path).value_or(UI::Toolbar::ToolClassification::CustomExecutable);
    const std::string filename = file_path.filename().string();
    const std::string icon = UI::Toolbar::get_classification_icon(classification);

    UI::Toolbar::BinaryTargetProfile profile;
    profile.id = to_lower_ascii(filename);
    profile.name = filename;
    profile.executable_path = file_path.string();
    profile.classification = classification;
    profile.icon_asset = icon;
    profile.is_default = false;
    return profile;
}

std::string ProjectToolClassifier::detect_cmake_project_name(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }
    const auto cmake_path = workspace_root / "CMakeLists.txt";
    std::ifstream file(cmake_path);
    if (!file.is_open())
    {
        return {};
    }

    std::string line;
    const std::regex project_regex(R"(project\s*\(\s*["']?([A-Za-z0-9_\-\.]+)["']?)", std::regex_constants::icase);
    while (std::getline(file, line))
    {
        const auto comment_idx = line.find('#');
        if (comment_idx != std::string::npos)
        {
            line.erase(comment_idx);
        }
        std::smatch match;
        if (std::regex_search(line, match, project_regex) && match.size() > 1)
        {
            return match[1].str();
        }
    }
    return {};
}

std::string ProjectToolClassifier::detect_cargo_package_name(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }
    const auto cargo_path = workspace_root / "Cargo.toml";
    std::ifstream file(cargo_path);
    if (!file.is_open())
    {
        return {};
    }

    std::string line;
    bool in_package = false;
    const std::regex package_sec_regex(R"(^\s*\[package\])", std::regex_constants::icase);
    const std::regex other_sec_regex(R"(^\s*\[)");
    const std::regex name_regex(R"(^\s*name\s*=\s*["']([^"']+)["'])", std::regex_constants::icase);

    while (std::getline(file, line))
    {
        const auto comment_idx = line.find('#');
        if (comment_idx != std::string::npos)
        {
            line.erase(comment_idx);
        }
        if (std::regex_search(line, package_sec_regex))
        {
            in_package = true;
            continue;
        }
        if (in_package && std::regex_search(line, other_sec_regex))
        {
            in_package = false;
        }
        if (in_package)
        {
            std::smatch match;
            if (std::regex_search(line, match, name_regex) && match.size() > 1)
            {
                return match[1].str();
            }
        }
    }
    return {};
}

std::string ProjectToolClassifier::detect_java_project_name(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }

    // 1. Maven: pom.xml
    const auto pom_path = workspace_root / "pom.xml";
    if (std::filesystem::exists(pom_path))
    {
        std::ifstream file(pom_path);
        if (file.is_open())
        {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const std::regex artifact_regex(R"(<artifactId>\s*([^<\s]+)\s*</artifactId>)", std::regex_constants::icase);
            std::smatch match;
            if (std::regex_search(content, match, artifact_regex) && match.size() > 1)
            {
                return match[1].str();
            }
        }
    }

    // 2. Gradle: settings.gradle or settings.gradle.kts
    for (const auto& settings_name : {"settings.gradle", "settings.gradle.kts"})
    {
        const auto settings_path = workspace_root / settings_name;
        std::ifstream file(settings_path);
        if (file.is_open())
        {
            std::string line;
            const std::regex root_name_regex(R"(rootProject\.name\s*=\s*['"]([^'"]+)['"])", std::regex_constants::icase);
            while (std::getline(file, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, root_name_regex) && match.size() > 1)
                {
                    return match[1].str();
                }
            }
        }
    }

    // 3. Gradle: build.gradle
    const auto build_path = workspace_root / "build.gradle";
    if (std::filesystem::exists(build_path))
    {
        std::ifstream file(build_path);
        if (file.is_open())
        {
            std::string line;
            const std::regex archive_regex(R"(archivesBaseName\s*=\s*['"]([^'"]+)['"])", std::regex_constants::icase);
            while (std::getline(file, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, archive_regex) && match.size() > 1)
                {
                    return match[1].str();
                }
            }
        }
    }

    return {};
}

std::string ProjectToolClassifier::detect_python_project_name(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }

    // 1. pyproject.toml
    const auto pyproject_path = workspace_root / "pyproject.toml";
    if (std::filesystem::exists(pyproject_path))
    {
        std::ifstream file(pyproject_path);
        if (file.is_open())
        {
            std::string line;
            const std::regex name_regex(R"(^\s*name\s*=\s*["']([^"']+)["'])", std::regex_constants::icase);
            while (std::getline(file, line))
            {
                const auto comment_idx = line.find('#');
                if (comment_idx != std::string::npos)
                {
                    line.erase(comment_idx);
                }
                std::smatch match;
                if (std::regex_search(line, match, name_regex) && match.size() > 1)
                {
                    return match[1].str();
                }
            }
        }
    }

    // 2. setup.py
    const auto setup_path = workspace_root / "setup.py";
    if (std::filesystem::exists(setup_path))
    {
        std::ifstream file(setup_path);
        if (file.is_open())
        {
            std::string line;
            const std::regex name_regex(R"(name\s*=\s*['"]([^'"]+)['"])", std::regex_constants::icase);
            while (std::getline(file, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, name_regex) && match.size() > 1)
                {
                    return match[1].str();
                }
            }
        }
    }

    return {};
}

std::string ProjectToolClassifier::detect_pascal_project_name(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }

    std::error_code ec;
    // 1. Check for .lpi files (Lazarus project)
    for (const auto& entry : std::filesystem::directory_iterator(workspace_root, ec))
    {
        if (entry.is_regular_file(ec))
        {
            const auto ext = to_lower_ascii(entry.path().extension().string());
            if (ext == ".lpi")
            {
                return entry.path().stem().string();
            }
        }
    }

    // 2. Check for program statement in .pas or .lpr or .dpr
    for (const auto& entry : std::filesystem::directory_iterator(workspace_root, ec))
    {
        if (entry.is_regular_file(ec))
        {
            const auto ext = to_lower_ascii(entry.path().extension().string());
            if (ext == ".pas" || ext == ".lpr" || ext == ".dpr")
            {
                std::ifstream file(entry.path());
                if (file.is_open())
                {
                    std::string line;
                    const std::regex prog_regex(R"(^\s*program\s+([A-Za-z0-9_]+)\s*;)", std::regex_constants::icase);
                    while (std::getline(file, line))
                    {
                        std::smatch match;
                        if (std::regex_search(line, match, prog_regex) && match.size() > 1)
                        {
                            return match[1].str();
                        }
                    }
                }
                return entry.path().stem().string();
            }
        }
    }

    return {};
}

std::string ProjectToolClassifier::extract_project_name(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return "Project";
    }

    std::string name = detect_cmake_project_name(workspace_root);
    if (!name.empty()) return name;

    name = detect_cargo_package_name(workspace_root);
    if (!name.empty()) return name;

    name = detect_java_project_name(workspace_root);
    if (!name.empty()) return name;

    name = detect_python_project_name(workspace_root);
    if (!name.empty()) return name;

    name = detect_pascal_project_name(workspace_root);
    if (!name.empty()) return name;

    const std::string folder_name = workspace_root.filename().string();
    return folder_name.empty() ? "Project" : folder_name;
}

std::vector<UI::Toolbar::BinaryTargetProfile> ProjectToolClassifier::detect_configurations(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& active_file)
{
    std::vector<UI::Toolbar::BinaryTargetProfile> targets;

    // 1. Contextual active file if classified
    if (!active_file.empty())
    {
        if (const auto classification = classify_file(active_file))
        {
            auto profile = create_profile_for_file(active_file);
            profile.is_default = true;
            targets.push_back(std::move(profile));
        }
    }

    // 2. Scan workspace root for project manifests
    if (!workspace_root.empty() && std::filesystem::exists(workspace_root))
    {
        std::error_code ec;

        // CMake detection
        const auto cmake_file = workspace_root / "CMakeLists.txt";
        if (std::filesystem::exists(cmake_file, ec))
        {
            const std::string cmake_proj = detect_cmake_project_name(workspace_root);
            const std::string main_name = cmake_proj.empty() ? (workspace_root.empty() ? "Project" : workspace_root.filename().string()) : cmake_proj;
            const std::string tests_name = (main_name == "ZDE") ? "ZDEUnitTests" : (main_name + "Tests");

            UI::Toolbar::BinaryTargetProfile cmake_main;
            cmake_main.id = to_lower_ascii(main_name);
            cmake_main.name = main_name;
            cmake_main.classification = UI::Toolbar::ToolClassification::CMake;
            cmake_main.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::CMake);
            cmake_main.is_default = targets.empty();
            targets.push_back(cmake_main);

            UI::Toolbar::BinaryTargetProfile cmake_tests;
            cmake_tests.id = to_lower_ascii(tests_name);
            cmake_tests.name = tests_name;
            cmake_tests.classification = UI::Toolbar::ToolClassification::CMake;
            cmake_tests.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::CMake);
            cmake_tests.is_default = false;
            targets.push_back(cmake_tests);
        }

        // Cargo / TOML detection
        const auto cargo_file = workspace_root / "Cargo.toml";
        if (std::filesystem::exists(cargo_file, ec))
        {
            const std::string cargo_pkg = detect_cargo_package_name(workspace_root);
            const std::string cargo_name = cargo_pkg.empty() ? "Cargo Run" : cargo_pkg;

            UI::Toolbar::BinaryTargetProfile cargo_run;
            cargo_run.id = "cargo_run";
            cargo_run.name = cargo_name;
            cargo_run.classification = UI::Toolbar::ToolClassification::TomlCargo;
            cargo_run.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::TomlCargo);
            cargo_run.is_default = targets.empty();
            targets.push_back(cargo_run);

            UI::Toolbar::BinaryTargetProfile cargo_test;
            cargo_test.id = "cargo_test";
            cargo_test.name = cargo_pkg.empty() ? "Cargo Test" : (cargo_pkg + " (test)");
            cargo_test.classification = UI::Toolbar::ToolClassification::TomlCargo;
            cargo_test.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::TomlCargo);
            cargo_test.is_default = false;
            targets.push_back(cargo_test);
        }

        // Python project detection
        const auto pyproject = workspace_root / "pyproject.toml";
        const auto req_txt = workspace_root / "requirements.txt";
        const auto main_py = workspace_root / "main.py";
        if (std::filesystem::exists(pyproject, ec) || std::filesystem::exists(req_txt, ec) || std::filesystem::exists(main_py, ec))
        {
            const std::string py_name = detect_python_project_name(workspace_root);
            const std::string py_target_name = py_name.empty() ? "main.py" : py_name;

            const bool already_has = std::any_of(targets.begin(), targets.end(), [&](const auto& t) {
                return t.classification == UI::Toolbar::ToolClassification::Python && t.name == py_target_name;
            });
            if (!already_has)
            {
                UI::Toolbar::BinaryTargetProfile py_target;
                py_target.id = "python_main";
                py_target.name = py_target_name;
                py_target.executable_path = (workspace_root / "main.py").string();
                py_target.classification = UI::Toolbar::ToolClassification::Python;
                py_target.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::Python);
                py_target.is_default = targets.empty();
                targets.push_back(py_target);
            }
        }

        // Java project detection (Maven / Gradle)
        const auto pom_xml = workspace_root / "pom.xml";
        const auto build_gradle = workspace_root / "build.gradle";
        const auto settings_gradle = workspace_root / "settings.gradle";
        if (std::filesystem::exists(pom_xml, ec) || std::filesystem::exists(build_gradle, ec) || std::filesystem::exists(settings_gradle, ec))
        {
            const std::string java_name = detect_java_project_name(workspace_root);
            const std::string java_target_name = java_name.empty() 
                ? (std::filesystem::exists(pom_xml, ec) ? "Maven Application" : "Gradle Application")
                : java_name;

            UI::Toolbar::BinaryTargetProfile java_target;
            java_target.id = "java_app";
            java_target.name = java_target_name;
            java_target.classification = UI::Toolbar::ToolClassification::Java;
            java_target.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::Java);
            java_target.is_default = targets.empty();
            targets.push_back(java_target);
        }

        // Pascal project detection
        const std::string pas_name = detect_pascal_project_name(workspace_root);
        if (!pas_name.empty())
        {
            UI::Toolbar::BinaryTargetProfile pas_target;
            pas_target.id = to_lower_ascii(pas_name);
            pas_target.name = pas_name;
            pas_target.classification = UI::Toolbar::ToolClassification::Pascal;
            pas_target.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::Pascal);
            pas_target.is_default = targets.empty();
            targets.push_back(pas_target);
        }
    }

    // Default fallback if nothing detected
    if (targets.empty())
    {
        const std::string fallback_name = workspace_root.empty() ? "Project" : workspace_root.filename().string();
        targets.push_back({
            .id = "primary",
            .name = fallback_name.empty() ? "Project" : fallback_name,
            .executable_path = "",
            .classification = UI::Toolbar::ToolClassification::CustomExecutable,
            .icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::CustomExecutable),
            .is_default = true
        });
    }

    return targets;
}

ExecutionCommand ProjectToolClassifier::create_run_command(
    const UI::Toolbar::BinaryTargetProfile& profile,
    const std::filesystem::path& workspace_root,
    const std::string& preset,
    const std::string& config)
{
    ExecutionCommand cmd;
    cmd.working_directory = workspace_root.empty() ? std::filesystem::current_path() : workspace_root;

    switch (profile.classification)
    {
    case UI::Toolbar::ToolClassification::CMake:
    {
        std::filesystem::path exec_path;
#if defined(__APPLE__)
        if (profile.name.find("Test") != std::string::npos || profile.name.find("test") != std::string::npos)
        {
            exec_path = cmd.working_directory / "build" / preset / "bin" / config / profile.name;
        }
        else
        {
            exec_path = cmd.working_directory / "build" / preset / "bin" / config / (profile.name + ".app") / "Contents" / "MacOS" / profile.name;
            if (!std::filesystem::exists(exec_path))
            {
                exec_path = cmd.working_directory / "build" / preset / "bin" / config / profile.name;
            }
        }
#elif defined(_WIN32)
        exec_path = cmd.working_directory / "build" / preset / "bin" / config / (profile.name + ".exe");
#else
        exec_path = cmd.working_directory / "build" / preset / "bin" / config / profile.name;
#endif
        cmd.program = exec_path.string();
        break;
    }

    case UI::Toolbar::ToolClassification::TomlCargo:
    {
        cmd.program = "cargo";
        if (profile.name.find("Test") != std::string::npos)
        {
            cmd.arguments.push_back("test");
        }
        else
        {
            cmd.arguments.push_back("run");
        }
        break;
    }

    case UI::Toolbar::ToolClassification::Python:
    {
#if defined(_WIN32)
        cmd.program = "python";
#else
        cmd.program = "python3";
#endif
        if (!profile.executable_path.empty())
        {
            cmd.arguments.push_back(profile.executable_path);
        }
        else
        {
            cmd.arguments.push_back(profile.name);
        }
        break;
    }

    case UI::Toolbar::ToolClassification::Java:
    {
        std::error_code ec;
        if (std::filesystem::exists(cmd.working_directory / "pom.xml", ec))
        {
            cmd.program = "mvn";
            cmd.arguments.push_back("compile");
            cmd.arguments.push_back("exec:java");
        }
        else if (std::filesystem::exists(cmd.working_directory / "build.gradle", ec))
        {
            cmd.program = "gradle";
            cmd.arguments.push_back("run");
        }
        else
        {
            cmd.program = "java";
            cmd.arguments.push_back(profile.executable_path.empty() ? profile.name : profile.executable_path);
        }
        break;
    }

    case UI::Toolbar::ToolClassification::Pascal:
    {
        cmd.program = "fpc";
        cmd.arguments.push_back("-B");
        cmd.arguments.push_back(profile.executable_path.empty() ? profile.name : profile.executable_path);
        break;
    }

    case UI::Toolbar::ToolClassification::CustomExecutable:
    {
        cmd.program = profile.executable_path.empty() ? profile.name : profile.executable_path;
        break;
    }
    }

    return cmd;
}

ExecutionCommand ProjectToolClassifier::create_debug_command(
    const UI::Toolbar::BinaryTargetProfile& profile,
    const std::filesystem::path& workspace_root,
    const std::string& preset,
    const std::string& config)
{
    ExecutionCommand cmd;
    cmd.working_directory = workspace_root.empty() ? std::filesystem::current_path() : workspace_root;

    switch (profile.classification)
    {
    case UI::Toolbar::ToolClassification::CMake:
    {
        const auto run_cmd = create_run_command(profile, workspace_root, preset, config);
        cmd.program = "lldb";
        cmd.arguments.push_back(run_cmd.program);
        break;
    }

    case UI::Toolbar::ToolClassification::TomlCargo:
    {
        cmd.program = "cargo";
        cmd.arguments.push_back("build");
        break;
    }

    case UI::Toolbar::ToolClassification::Python:
    {
#if defined(_WIN32)
        cmd.program = "python";
#else
        cmd.program = "python3";
#endif
        cmd.arguments.push_back("-m");
        cmd.arguments.push_back("pdb");
        if (!profile.executable_path.empty())
        {
            cmd.arguments.push_back(profile.executable_path);
        }
        else
        {
            cmd.arguments.push_back(profile.name);
        }
        break;
    }

    case UI::Toolbar::ToolClassification::Java:
    {
        cmd.program = "jdb";
        if (!profile.executable_path.empty())
        {
            cmd.arguments.push_back(profile.executable_path);
        }
        else
        {
            cmd.arguments.push_back(profile.name);
        }
        break;
    }

    case UI::Toolbar::ToolClassification::Pascal:
    {
        cmd.program = "gdb";
        cmd.arguments.push_back(profile.executable_path.empty() ? profile.name : profile.executable_path);
        break;
    }

    case UI::Toolbar::ToolClassification::CustomExecutable:
    {
        cmd.program = "gdb";
        cmd.arguments.push_back(profile.executable_path.empty() ? profile.name : profile.executable_path);
        break;
    }
    }

    return cmd;
}

} // namespace Zenvra::Tools::Classification
