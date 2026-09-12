#include "Tools/Classification/ProjectToolClassifier.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cstdint>
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

std::vector<std::string> ProjectToolClassifier::detect_cmake_executable_targets(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }

    std::vector<std::string> targets;
    std::vector<std::filesystem::path> scanned_paths;

    std::string proj_name = detect_cmake_project_name(workspace_root);
    if (proj_name.empty())
    {
        proj_name = workspace_root.filename().string();
    }

    const auto add_target_if_unique = [&](std::string name) {
        if (name.empty()) return;

        // Strip surrounding quotes if any
        while (!name.empty() && (name.front() == '"' || name.front() == '\'')) name.erase(0, 1);
        while (!name.empty() && (name.back() == '"' || name.back() == '\'')) name.pop_back();
        if (name.empty()) return;

        // Resolve ${PROJECT_NAME} or ${CMAKE_PROJECT_NAME} or variable references
        if (name == "${PROJECT_NAME}" || name == "$PROJECT_NAME" ||
            name == "${CMAKE_PROJECT_NAME}" || name == "$CMAKE_PROJECT_NAME")
        {
            name = proj_name;
        }
        else if (name.starts_with("${") && name.ends_with("}"))
        {
            name = proj_name;
        }

        if (name == "ALIAS" || name == "IMPORTED" || name == "GLOBAL" ||
            name == "WIN32" || name == "MACOSX_BUNDLE" || name == "EXCLUDE_FROM_ALL")
        {
            return;
        }
        if (std::find(targets.begin(), targets.end(), name) == targets.end())
        {
            targets.push_back(name);
        }
    };

    const std::regex exec_regex(R"(add_executable\s*\(\s*["']?(\$\{?[A-Za-z0-9_\-\.]+\}?|[A-Za-z0-9_\-\.]+)["']?)", std::regex_constants::icase);
    const std::regex sub_dir_regex(R"(add_subdirectory\s*\(\s*([A-Za-z0-9_\-\./\\]+))", std::regex_constants::icase);

    std::vector<std::filesystem::path> pending_files = { workspace_root / "CMakeLists.txt" };

    // Also prime common source/test subdirectories
    for (const auto& sub : {"Source", "src", "Tests", "tests", "Apps", "apps", "App", "Bin", "bin", "Examples", "examples"})
    {
        const auto sub_cmake = workspace_root / sub / "CMakeLists.txt";
        std::error_code ec;
        if (std::filesystem::exists(sub_cmake, ec))
        {
            pending_files.push_back(sub_cmake);
        }
    }

    while (!pending_files.empty())
    {
        auto cur_file = pending_files.back();
        pending_files.pop_back();

        std::error_code ec;
        const auto canonical_path = std::filesystem::weakly_canonical(cur_file, ec);
        const auto search_path = ec ? cur_file : canonical_path;

        if (std::find(scanned_paths.begin(), scanned_paths.end(), search_path) != scanned_paths.end())
        {
            continue;
        }
        scanned_paths.push_back(search_path);

        std::ifstream file(cur_file);
        if (!file.is_open()) continue;

        const auto cur_dir = cur_file.parent_path();
        std::string line;
        while (std::getline(file, line))
        {
            const auto comment_idx = line.find('#');
            if (comment_idx != std::string::npos)
            {
                line.erase(comment_idx);
            }
            std::smatch match;
            if (std::regex_search(line, match, exec_regex) && match.size() > 1)
            {
                add_target_if_unique(match[1].str());
            }
            if (std::regex_search(line, match, sub_dir_regex) && match.size() > 1)
            {
                std::string sub_rel = match[1].str();
                while (!sub_rel.empty() && (sub_rel.front() == '\"' || sub_rel.front() == '\'')) sub_rel.erase(0, 1);
                while (!sub_rel.empty() && (sub_rel.back() == '\"' || sub_rel.back() == '\'')) sub_rel.pop_back();

                const auto candidate_cmake = cur_dir / sub_rel / "CMakeLists.txt";
                if (std::filesystem::exists(candidate_cmake, ec))
                {
                    pending_files.push_back(candidate_cmake);
                }
            }
        }
    }

    return targets;
}

std::vector<std::string> ProjectToolClassifier::detect_cargo_binary_targets(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }

    std::vector<std::string> targets;
    const auto add_target_if_unique = [&](const std::string& name) {
        if (name.empty()) return;
        if (std::find(targets.begin(), targets.end(), name) == targets.end())
        {
            targets.push_back(name);
        }
    };

    // 1. Package name
    const std::string pkg_name = detect_cargo_package_name(workspace_root);
    if (!pkg_name.empty())
    {
        add_target_if_unique(pkg_name);
    }

    // 2. [[bin]] sections
    const auto cargo_path = workspace_root / "Cargo.toml";
    std::ifstream file(cargo_path);
    if (file.is_open())
    {
        std::string line;
        bool in_bin = false;
        const std::regex bin_sec_regex(R"(^\s*\[\[bin\]\])", std::regex_constants::icase);
        const std::regex other_sec_regex(R"(^\s*\[)");
        const std::regex name_regex(R"(^\s*name\s*=\s*["']([^"']+)["'])", std::regex_constants::icase);

        while (std::getline(file, line))
        {
            const auto comment_idx = line.find('#');
            if (comment_idx != std::string::npos)
            {
                line.erase(comment_idx);
            }
            if (std::regex_search(line, bin_sec_regex))
            {
                in_bin = true;
                continue;
            }
            if (in_bin && std::regex_search(line, other_sec_regex))
            {
                in_bin = false;
            }
            if (in_bin)
            {
                std::smatch match;
                if (std::regex_search(line, match, name_regex) && match.size() > 1)
                {
                    add_target_if_unique(match[1].str());
                }
            }
        }
    }

    // 3. src/bin/*.rs
    std::error_code ec;
    const auto src_bin_dir = workspace_root / "src" / "bin";
    if (std::filesystem::exists(src_bin_dir, ec) && std::filesystem::is_directory(src_bin_dir, ec))
    {
        for (const auto& entry : std::filesystem::directory_iterator(src_bin_dir, ec))
        {
            if (entry.is_regular_file(ec) && to_lower_ascii(entry.path().extension().string()) == ".rs")
            {
                add_target_if_unique(entry.path().stem().string());
            }
        }
    }

    return targets;
}

std::string ProjectToolClassifier::resolve_python_interpreter(
    const std::filesystem::path& workspace_root)
{
    if (!workspace_root.empty())
    {
        std::error_code ec;
        const std::vector<std::string> venv_dirs = {".venv", "venv", "env", ".env"};
        for (const auto& venv_name : venv_dirs)
        {
            const auto venv_path = workspace_root / venv_name;
#if defined(_WIN32)
            const auto py_exe = venv_path / "Scripts" / "python.exe";
#else
            const auto py_exe = venv_path / "bin" / "python";
#endif
            if (std::filesystem::exists(py_exe, ec))
            {
                return py_exe.string();
            }
        }
    }

#if defined(_WIN32)
    return "python";
#else
    return "python3";
#endif
}

std::vector<UI::Toolbar::BinaryTargetProfile> ProjectToolClassifier::detect_python_targets(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }

    std::vector<UI::Toolbar::BinaryTargetProfile> profiles;
    std::error_code ec;

    const std::string proj_name = detect_python_project_name(workspace_root);
    const std::string fallback_name = workspace_root.filename().empty() ? "Python Project" : workspace_root.filename().string();
    const std::string effective_proj = proj_name.empty() ? fallback_name : proj_name;
    const std::string icon = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::Python);

    const auto add_profile_if_unique = [&](std::string target_name, const std::string& script_file) {
        if (target_name.empty()) return;
        const bool exists = std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) {
            return p.name == target_name;
        });
        if (!exists)
        {
            profiles.push_back(UI::Toolbar::BinaryTargetProfile{
                .id = "py_" + to_lower_ascii(target_name),
                .name = std::move(target_name),
                .executable_path = script_file,
                .classification = UI::Toolbar::ToolClassification::Python,
                .icon_asset = icon,
                .is_default = false
            });
        }
    };

    // 1. Direct Alignment: does a script named <project_name>.py exist in workspace?
    if (!proj_name.empty())
    {
        const auto direct_script = workspace_root / (proj_name + ".py");
        if (std::filesystem::exists(direct_script, ec))
        {
            add_profile_if_unique(proj_name, direct_script.filename().string());
        }
        const auto direct_main = workspace_root / proj_name / "__main__.py";
        if (std::filesystem::exists(direct_main, ec))
        {
            add_profile_if_unique(proj_name, (proj_name + "/__main__.py"));
        }
    }

    // 2. Parse pyproject.toml [project.scripts] or [tool.poetry.scripts]
    const auto pyproject_path = workspace_root / "pyproject.toml";
    if (std::filesystem::exists(pyproject_path, ec))
    {
        std::ifstream file(pyproject_path);
        if (file.is_open())
        {
            std::string line;
            bool in_scripts = false;
            const std::regex scripts_sec_regex(R"(^\s*\[(project\.scripts|tool\.poetry\.scripts)\])", std::regex_constants::icase);
            const std::regex other_sec_regex(R"(^\s*\[)");
            const std::regex script_kv_regex(R"(^\s*([A-Za-z0-9_\-]+)\s*=\s*["']([^"']+)["'])");

            while (std::getline(file, line))
            {
                const auto comment_idx = line.find('#');
                if (comment_idx != std::string::npos)
                {
                    line.erase(comment_idx);
                }
                if (std::regex_search(line, scripts_sec_regex))
                {
                    in_scripts = true;
                    continue;
                }
                if (in_scripts && std::regex_search(line, other_sec_regex))
                {
                    in_scripts = false;
                }
                if (in_scripts)
                {
                    std::smatch match;
                    if (std::regex_search(line, match, script_kv_regex) && match.size() > 1)
                    {
                        const std::string script_cmd = match[1].str();
                        add_profile_if_unique(effective_proj + " (" + script_cmd + ")", script_cmd);
                    }
                }
            }
        }
    }

    // 3. Conventional entry scripts that actually exist in workspace root
    const std::vector<std::string> conventions = {
        "main.py", "app.py", "run.py", "cli.py", "__main__.py", "server.py", "manage.py", "index.py"
    };
    for (const auto& conv : conventions)
    {
        const auto p = workspace_root / conv;
        if (std::filesystem::exists(p, ec))
        {
            // If project name differs from file, label clearly: <project_name> (<file>)
            if (!proj_name.empty() && proj_name != conv && proj_name != p.stem().string())
            {
                add_profile_if_unique(proj_name + " (" + conv + ")", conv);
            }
            else
            {
                add_profile_if_unique(conv, conv);
            }
        }
    }

    // 4. Any other top-level .py files in root
    for (const auto& entry : std::filesystem::directory_iterator(workspace_root, ec))
    {
        if (entry.is_regular_file(ec))
        {
            const auto ext = to_lower_ascii(entry.path().extension().string());
            if (ext == ".py" || ext == ".pyw")
            {
                const std::string fname = entry.path().filename().string();
                if (std::find(conventions.begin(), conventions.end(), fname) == conventions.end() &&
                    fname != (proj_name + ".py"))
                {
                    if (!proj_name.empty())
                    {
                        add_profile_if_unique(proj_name + " (" + fname + ")", fname);
                    }
                    else
                    {
                        add_profile_if_unique(fname, fname);
                    }
                }
            }
        }
    }

    // 5. Fallback if pyproject.toml or requirements.txt exists without top-level py script
    if (profiles.empty())
    {
        if (std::filesystem::exists(workspace_root / "pyproject.toml", ec) ||
            std::filesystem::exists(workspace_root / "requirements.txt", ec))
        {
            add_profile_if_unique(effective_proj, "");
        }
    }

    return profiles;
}

std::string ProjectToolClassifier::detect_binary_architecture(const std::filesystem::path& binary_path)
{
    std::error_code ec;
    if (!std::filesystem::exists(binary_path, ec) || !std::filesystem::is_regular_file(binary_path, ec))
    {
        return {};
    }

    std::ifstream file(binary_path, std::ios::binary);
    if (!file.is_open())
    {
        return {};
    }

    std::array<uint8_t, 1024> header{};
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const std::streamsize bytes_read = file.gcount();
    if (bytes_read < 64)
    {
        return {};
    }

    // 1. Check Windows PE Header
    if (header[0] == 'M' && header[1] == 'Z')
    {
        uint32_t pe_offset = 0;
        std::memcpy(&pe_offset, &header[0x3C], sizeof(uint32_t));
        if (pe_offset >= 0x40 && pe_offset <= static_cast<uint32_t>(bytes_read) - 24)
        {
            if (header[pe_offset] == 'P' && header[pe_offset + 1] == 'E' &&
                header[pe_offset + 2] == '\0' && header[pe_offset + 3] == '\0')
            {
                uint16_t machine = 0;
                std::memcpy(&machine, &header[pe_offset + 4], sizeof(uint16_t));
                switch (machine)
                {
                case 0x8664: return "x86_64";
                case 0x014C: return "x86";
                case 0xAA64: return "arm64";
                case 0x01C0:
                case 0x01C4: return "arm32";
                default: break;
                }
            }
        }
        else if (pe_offset >= 0x40 && pe_offset < 1024 * 1024)
        {
            file.seekg(pe_offset, std::ios::beg);
            std::array<uint8_t, 24> pe_head{};
            file.read(reinterpret_cast<char*>(pe_head.data()), static_cast<std::streamsize>(pe_head.size()));
            if (file.gcount() >= 6 &&
                pe_head[0] == 'P' && pe_head[1] == 'E' && pe_head[2] == '\0' && pe_head[3] == '\0')
            {
                uint16_t machine = 0;
                std::memcpy(&machine, &pe_head[4], sizeof(uint16_t));
                switch (machine)
                {
                case 0x8664: return "x86_64";
                case 0x014C: return "x86";
                case 0xAA64: return "arm64";
                case 0x01C0:
                case 0x01C4: return "arm32";
                default: break;
                }
            }
        }
    }

    // 2. Check ELF Header (Linux)
    if (header[0] == 0x7F && header[1] == 'E' && header[2] == 'L' && header[3] == 'F')
    {
        if (bytes_read >= 20)
        {
            uint16_t e_machine = 0;
            std::memcpy(&e_machine, &header[18], sizeof(uint16_t));
            switch (e_machine)
            {
            case 62: return "x86_64";
            case 3:  return "x86";
            case 183: return "arm64";
            case 40:  return "arm32";
            default: break;
            }
        }
    }

    // 3. Check Mach-O (macOS)
    if (bytes_read >= 8)
    {
        uint32_t magic = 0;
        std::memcpy(&magic, &header[0], sizeof(uint32_t));
        if (magic == 0xFEEDFACF || magic == 0xCFFAEDFE || magic == 0xFEEDFACE || magic == 0xCEFAEDFE)
        {
            uint32_t cputype = 0;
            std::memcpy(&cputype, &header[4], sizeof(uint32_t));
            if (cputype == 0x01000007 || cputype == 0x07000001) return "x86_64";
            if (cputype == 0x0100000C || cputype == 0x0C000001) return "arm64";
            if (cputype == 0x00000007 || cputype == 0x07000000) return "x86";
            if (cputype == 0x0000000C || cputype == 0x0C000000) return "arm32";
        }
    }

    return {};
}

std::vector<ProjectToolClassifier::CompiledBinary> ProjectToolClassifier::detect_compiled_binaries(
    const std::filesystem::path& workspace_root)
{
    if (workspace_root.empty())
    {
        return {};
    }

    std::error_code ec;
    if (!std::filesystem::exists(workspace_root, ec) || !std::filesystem::is_directory(workspace_root, ec))
    {
        return {};
    }

    std::vector<CompiledBinary> results;

    const bool has_cmake = std::filesystem::exists(workspace_root / "CMakeLists.txt", ec) ||
                           std::filesystem::exists(workspace_root / "build" / "CMakeCache.txt", ec);
    const bool has_cargo = std::filesystem::exists(workspace_root / "Cargo.toml", ec);
    const UI::Toolbar::ToolClassification default_class =
        has_cmake ? UI::Toolbar::ToolClassification::CMake :
        (has_cargo ? UI::Toolbar::ToolClassification::TomlCargo : UI::Toolbar::ToolClassification::CustomExecutable);

    const std::vector<std::filesystem::path> search_dirs = {
        workspace_root / "build",
        workspace_root / "build_x86",
        workspace_root / "build_arm64",
        workspace_root / "build_arm32",
        workspace_root / "bin",
        workspace_root / "out",
        workspace_root / "target" / "debug",
        workspace_root / "target" / "release",
        workspace_root / "Debug",
        workspace_root / "Release",
    };

    const auto is_ignored_binary = [](std::string_view name) {
        if (name.empty()) return true;
        if (name.find("CompilerId") != std::string_view::npos) return true;
        if (name.find("CMakeSystemInfo") != std::string_view::npos) return true;
        if (name.find("feature_tests") != std::string_view::npos) return true;
        if (name.find("TestCFlags") != std::string_view::npos) return true;
        if (name.find("CMakeCXXCompilerABI") != std::string_view::npos) return true;
        if (name.find("CMakeCCompilerABI") != std::string_view::npos) return true;
        return false;
    };

    auto check_binary_candidate = [&](const std::filesystem::path& file_path) {
        const auto ext = file_path.extension().string();

        bool is_exe = false;
#if defined(_WIN32)
        std::string lower_ext;
        lower_ext.reserve(ext.size());
        for (char c : ext) lower_ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lower_ext == ".exe")
        {
            is_exe = true;
        }
#else
        if (ext.empty())
        {
            const auto perms = std::filesystem::status(file_path, ec).permissions();
            if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)
            {
                is_exe = true;
            }
        }
#endif

        if (is_exe)
        {
            const std::string stem = file_path.stem().string();
            if (!is_ignored_binary(stem))
            {
                bool exists_already = false;
                for (const auto& b : results)
                {
                    if (b.name == stem)
                    {
                        exists_already = true;
                        break;
                    }
                }
                if (!exists_already)
                {
                    std::string bin_arch = detect_binary_architecture(file_path);
                    if (bin_arch.empty())
                    {
                        const std::string path_lower = to_lower_ascii(file_path.string());
                        if (path_lower.find("x86") != std::string::npos || path_lower.find("win32") != std::string::npos || path_lower.find("i386") != std::string::npos)
                            bin_arch = "x86";
                        else if (path_lower.find("arm64") != std::string::npos || path_lower.find("aarch64") != std::string::npos)
                            bin_arch = "arm64";
                        else if (path_lower.find("arm") != std::string::npos)
                            bin_arch = "arm32";
                        else
                            bin_arch = "x86_64";
                    }

                    results.push_back(CompiledBinary{
                        .name = stem,
                        .path = file_path,
                        .classification = default_class,
                        .architecture = bin_arch
                    });
                }
            }
        }
    };

    for (const auto& dir : search_dirs)
    {
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
        {
            continue;
        }

        for (auto it = std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                break;
            }

            if (it->is_directory(ec))
            {
                if (it.depth() >= 3)
                {
                    it.disable_recursion_pending();
                    continue;
                }
                const auto dname = it->path().filename().string();
                if (dname == "CMakeFiles" || dname == ".git" || dname == "node_modules" || dname == ".vs" || dname == ".vscode")
                {
                    it.disable_recursion_pending();
                    continue;
                }
                continue;
            }

            if (!it->is_regular_file(ec))
            {
                continue;
            }

            check_binary_candidate(it->path());
        }
    }

    // Also check workspace root directly (non-recursively) for top-level binaries
    for (auto it = std::filesystem::directory_iterator(workspace_root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_regular_file(ec))
        {
            check_binary_candidate(it->path());
        }
    }

    return results;
}

std::vector<UI::Toolbar::BinaryTargetProfile> ProjectToolClassifier::detect_configurations(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& active_file)
{
    // When no folder is opened and no active file, return empty (clean like VS Code / JetBrains)
    if (workspace_root.empty() && active_file.empty())
    {
        return {};
    }

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

    // 2. Non-exclusive Polyglot scan of workspace root
    if (!workspace_root.empty() && std::filesystem::exists(workspace_root))
    {
        std::error_code ec;
        const auto compiled_bins = detect_compiled_binaries(workspace_root);

        // A. CMake detection & executable target discovery
        const auto cmake_file = workspace_root / "CMakeLists.txt";
        if (std::filesystem::exists(cmake_file, ec))
        {
            const std::string cmake_proj = detect_cmake_project_name(workspace_root);
            const std::string default_proj_name = cmake_proj.empty()
                ? (workspace_root.empty() ? "Project" : workspace_root.filename().string())
                : cmake_proj;

            auto exec_targets = detect_cmake_executable_targets(workspace_root);
            for (const auto& bin : compiled_bins)
            {
                if (std::find(exec_targets.begin(), exec_targets.end(), bin.name) == exec_targets.end())
                {
                    exec_targets.push_back(bin.name);
                }
            }

            if (!exec_targets.empty())
            {
                for (const auto& exec_name : exec_targets)
                {
                    UI::Toolbar::BinaryTargetProfile cmake_target;
                    cmake_target.id = to_lower_ascii(exec_name);
                    cmake_target.name = exec_name;
                    cmake_target.classification = UI::Toolbar::ToolClassification::CMake;
                    cmake_target.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::CMake);
                    cmake_target.is_default = targets.empty();

                    for (const auto& bin : compiled_bins)
                    {
                        if (bin.name == exec_name)
                        {
                            cmake_target.executable_path = bin.path.string();
                            break;
                        }
                    }

                    targets.push_back(cmake_target);
                }
            }
            else
            {
                UI::Toolbar::BinaryTargetProfile cmake_main;
                cmake_main.id = to_lower_ascii(default_proj_name);
                cmake_main.name = default_proj_name;
                cmake_main.classification = UI::Toolbar::ToolClassification::CMake;
                cmake_main.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::CMake);
                cmake_main.is_default = targets.empty();
                for (const auto& bin : compiled_bins)
                {
                    if (bin.name == default_proj_name)
                    {
                        cmake_main.executable_path = bin.path.string();
                        break;
                    }
                }
                targets.push_back(cmake_main);
            }
        }

        // B. Cargo / TOML detection & binary target discovery
        const auto cargo_file = workspace_root / "Cargo.toml";
        if (std::filesystem::exists(cargo_file, ec))
        {
            const std::string cargo_pkg = detect_cargo_package_name(workspace_root);
            const auto bin_targets = detect_cargo_binary_targets(workspace_root);

            if (!bin_targets.empty())
            {
                for (const auto& bin_name : bin_targets)
                {
                    UI::Toolbar::BinaryTargetProfile cargo_target;
                    cargo_target.id = "cargo_bin_" + to_lower_ascii(bin_name);
                    cargo_target.name = bin_name;
                    cargo_target.classification = UI::Toolbar::ToolClassification::TomlCargo;
                    cargo_target.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::TomlCargo);
                    cargo_target.is_default = targets.empty();
                    targets.push_back(cargo_target);
                }
            }
            else
            {
                const std::string cargo_name = cargo_pkg.empty() ? "Cargo Run" : cargo_pkg;
                UI::Toolbar::BinaryTargetProfile cargo_run;
                cargo_run.id = "cargo_run";
                cargo_run.name = cargo_name;
                cargo_run.classification = UI::Toolbar::ToolClassification::TomlCargo;
                cargo_run.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::TomlCargo);
                cargo_run.is_default = targets.empty();
                targets.push_back(cargo_run);
            }

            // Test target for Cargo
            UI::Toolbar::BinaryTargetProfile cargo_test;
            cargo_test.id = "cargo_test";
            cargo_test.name = cargo_pkg.empty() ? "Cargo Test" : (cargo_pkg + " (test)");
            cargo_test.classification = UI::Toolbar::ToolClassification::TomlCargo;
            cargo_test.icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::TomlCargo);
            cargo_test.is_default = false;
            targets.push_back(cargo_test);
        }

        // C. Python project & entry script alignment
        const auto py_targets = detect_python_targets(workspace_root);
        for (auto py_t : py_targets)
        {
            const bool already_has = std::any_of(targets.begin(), targets.end(), [&](const auto& t) {
                return t.classification == UI::Toolbar::ToolClassification::Python && t.name == py_t.name;
            });
            if (!already_has)
            {
                py_t.is_default = targets.empty();
                targets.push_back(std::move(py_t));
            }
        }

        // D. Java project detection (Maven / Gradle)
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

        // E. Pascal project detection
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

        // F. Auto-configure any pre-compiled binaries from build/ or bin/ not already added
        for (const auto& bin : compiled_bins)
        {
            bool already_added = false;
            for (const auto& t : targets)
            {
                if (t.name == bin.name)
                {
                    already_added = true;
                    break;
                }
            }
            if (!already_added)
            {
                UI::Toolbar::BinaryTargetProfile bin_target;
                bin_target.id = "bin_" + to_lower_ascii(bin.name);
                bin_target.name = bin.name;
                bin_target.executable_path = bin.path.string();
                bin_target.classification = bin.classification;
                bin_target.icon_asset = UI::Toolbar::get_classification_icon(bin.classification);
                bin_target.is_default = targets.empty();
                targets.push_back(bin_target);
            }
        }
    }

    // Default fallback ONLY if a folder was opened and truly nothing was found
    if (targets.empty() && !workspace_root.empty())
    {
        const std::string fallback_name = workspace_root.filename().string();
        targets.push_back({
            .id = "primary",
            .name = fallback_name.empty() ? "Project" : fallback_name,
            .executable_path = "",
            .classification = UI::Toolbar::ToolClassification::CustomExecutable,
            .icon_asset = UI::Toolbar::get_classification_icon(UI::Toolbar::ToolClassification::CustomExecutable),
            .is_default = true
        });
    }

    // Ensure at least one target is marked default
    if (std::none_of(targets.begin(), targets.end(), [](const auto& t) { return t.is_default; }))
    {
        if (!targets.empty())
        {
            targets.front().is_default = true;
        }
    }

    return targets;
}

ExecutionCommand ProjectToolClassifier::create_run_command(
    const UI::Toolbar::BinaryTargetProfile& profile,
    const std::filesystem::path& workspace_root,
    const std::string& preset,
    const std::string& config,
    const std::string& arch)
{
    ExecutionCommand cmd;
    cmd.working_directory = workspace_root.empty() ? std::filesystem::current_path() : workspace_root;

    switch (profile.classification)
    {
    case UI::Toolbar::ToolClassification::CMake:
    {
        std::filesystem::path exec_path;
        std::error_code ec;

        if (!profile.executable_path.empty())
        {
            // If user switched mode (e.g. Debug <-> Release), try to resolve the binary for the active mode
            std::string path_str = profile.executable_path;
            const std::string alt_mode = (config == "Release") ? "Debug" : "Release";
            auto mode_pos = path_str.find(alt_mode);
            if (mode_pos != std::string::npos)
            {
                std::string mode_switch = path_str;
                mode_switch.replace(mode_pos, alt_mode.length(), config);
                if (std::filesystem::exists(mode_switch, ec))
                {
                    exec_path = mode_switch;
                }
                else if (std::filesystem::exists(cmd.working_directory / mode_switch, ec))
                {
                    exec_path = cmd.working_directory / mode_switch;
                }
            }
            else
            {
                const std::filesystem::path prof_p(profile.executable_path);
                if (std::filesystem::exists(prof_p, ec))
                {
                    exec_path = prof_p;
                }
                else if (std::filesystem::exists(cmd.working_directory / prof_p, ec))
                {
                    exec_path = cmd.working_directory / prof_p;
                }
            }
        }

        if (exec_path.empty())
        {
            const std::string exe_name = profile.name +
#if defined(_WIN32)
                ".exe";
#else
                "";
#endif
            const std::vector<std::filesystem::path> candidates = {
                cmd.working_directory / config / exe_name,
                cmd.working_directory / "build" / config / exe_name,
                cmd.working_directory / "build" / arch / config / exe_name,
                cmd.working_directory / "build" / "x64" / config / exe_name,
                cmd.working_directory / "build" / "x86" / config / exe_name,
                cmd.working_directory / "build" / "Win32" / config / exe_name,
                cmd.working_directory / "build" / "ARM64" / config / exe_name,
                cmd.working_directory / ("build_" + arch) / config / exe_name,
                cmd.working_directory / "build_x86" / config / exe_name,
                cmd.working_directory / "build_arm64" / config / exe_name,
                cmd.working_directory / "build" / preset / "bin" / config / exe_name,
                cmd.working_directory / "build" / "bin" / config / exe_name,
                cmd.working_directory / "bin" / config / exe_name,
                cmd.working_directory / "build" / "bin" / exe_name,
                cmd.working_directory / "build" / exe_name,
                cmd.working_directory / "bin" / exe_name,
                cmd.working_directory / exe_name,
            };

            for (const auto& cand : candidates)
            {
                if (std::filesystem::exists(cand, ec))
                {
                    const std::string cand_arch = detect_binary_architecture(cand);
                    if (!cand_arch.empty() && !arch.empty())
                    {
                        const bool match_64 = (arch == "x86_64" || arch == "x64") && (cand_arch == "x86_64");
                        const bool match_32 = (arch == "x86" || arch == "Win32") && (cand_arch == "x86");
                        const bool match_arm64 = (arch == "arm64" || arch == "ARM64") && (cand_arch == "arm64");
                        const bool match_arm32 = (arch == "arm32" || arch == "ARM32") && (cand_arch == "arm32");
                        if (!match_64 && !match_32 && !match_arm64 && !match_arm32)
                        {
                            continue;
                        }
                    }
                    exec_path = cand;
                    break;
                }
            }

            if (exec_path.empty())
            {
                if (std::filesystem::exists(cmd.working_directory / config, ec) ||
                    std::filesystem::exists(cmd.working_directory / "Debug", ec) ||
                    std::filesystem::exists(cmd.working_directory / "Release", ec))
                {
                    exec_path = cmd.working_directory / config / exe_name;
                }
                else if (std::filesystem::exists(cmd.working_directory / "build", ec))
                {
                    exec_path = cmd.working_directory / "build" / config / exe_name;
                }
                else
                {
#if defined(_WIN32)
                    exec_path = cmd.working_directory / "build" / preset / "bin" / config / (profile.name + ".exe");
#else
                    exec_path = cmd.working_directory / "build" / preset / "bin" / config / profile.name;
#endif
                }
            }
        }
        cmd.program = exec_path.string();
        break;
    }

    case UI::Toolbar::ToolClassification::TomlCargo:
    {
        cmd.program = "cargo";
        if (profile.name.find("Test") != std::string::npos || profile.name.find("test") != std::string::npos)
        {
            cmd.arguments.push_back("test");
        }
        else if (profile.id.find("cargo_bin_") == 0)
        {
            cmd.arguments.push_back("run");
            cmd.arguments.push_back("--bin");
            cmd.arguments.push_back(profile.name);
        }
        else
        {
            cmd.arguments.push_back("run");
        }
        break;
    }

    case UI::Toolbar::ToolClassification::Python:
    {
        cmd.program = resolve_python_interpreter(workspace_root);
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
        std::filesystem::path exec_path;
        std::error_code ec;
        if (!profile.executable_path.empty())
        {
            std::string path_str = profile.executable_path;
            const std::string alt_mode = (config == "Release") ? "Debug" : "Release";
            auto mode_pos = path_str.find(alt_mode);
            if (mode_pos != std::string::npos)
            {
                std::string mode_switch = path_str;
                mode_switch.replace(mode_pos, alt_mode.length(), config);
                if (std::filesystem::exists(mode_switch, ec))
                {
                    exec_path = mode_switch;
                }
                else if (std::filesystem::exists(cmd.working_directory / mode_switch, ec))
                {
                    exec_path = cmd.working_directory / mode_switch;
                }
            }
            else
            {
                const std::filesystem::path prof_p(profile.executable_path);
                if (std::filesystem::exists(prof_p, ec))
                {
                    exec_path = prof_p;
                }
                else if (std::filesystem::exists(cmd.working_directory / prof_p, ec))
                {
                    exec_path = cmd.working_directory / prof_p;
                }
            }
        }
        if (exec_path.empty())
        {
            const std::string exe_name = profile.name +
#if defined(_WIN32)
                ".exe";
#else
                "";
#endif
            const std::vector<std::filesystem::path> candidates = {
                cmd.working_directory / config / exe_name,
                cmd.working_directory / "build" / config / exe_name,
                cmd.working_directory / "build" / arch / config / exe_name,
                cmd.working_directory / "build_x86" / config / exe_name,
                cmd.working_directory / "build_arm64" / config / exe_name,
                cmd.working_directory / "bin" / config / exe_name,
                cmd.working_directory / "bin" / exe_name,
                cmd.working_directory / exe_name,
            };
            for (const auto& cand : candidates)
            {
                if (std::filesystem::exists(cand, ec))
                {
                    exec_path = cand;
                    break;
                }
            }
            if (exec_path.empty())
            {
                exec_path = cmd.working_directory / config / exe_name;
            }
        }
        cmd.program = exec_path.string();
        break;
    }
    }

    return cmd;
}

ExecutionCommand ProjectToolClassifier::create_debug_command(
    const UI::Toolbar::BinaryTargetProfile& profile,
    const std::filesystem::path& workspace_root,
    const std::string& preset,
    const std::string& config,
    const std::string& arch)
{
    ExecutionCommand cmd;
    cmd.working_directory = workspace_root.empty() ? std::filesystem::current_path() : workspace_root;

    switch (profile.classification)
    {
    case UI::Toolbar::ToolClassification::CMake:
    {
        const auto run_cmd = create_run_command(profile, workspace_root, preset, config, arch);
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
        cmd.program = resolve_python_interpreter(workspace_root);
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
        const auto run_cmd = create_run_command(profile, workspace_root, preset, config);
        cmd.program = "lldb";
        cmd.arguments.push_back(run_cmd.program);
        break;
    }
    }

    return cmd;
}

} // namespace Zenvra::Tools::Classification
