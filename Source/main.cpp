#include "Application/Application.h"
#include "Bootstrapper/NativeUI.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <string_view>
#include <utility>
#include <filesystem>
#include <fstream>
#include <sstream>

static std::string ReadFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static bool has_argument(int argument_count, char** argument_values, std::string_view expected_argument)
{
    return std::any_of(
        argument_values + 1,
        argument_values + argument_count,
        [expected_argument](const char* argument) { return argument == expected_argument; });
}

int main(int argument_count, char** argument_values)
{
#if defined(_WIN32) && (defined(NDEBUG) || defined(ZDE_RELEASE))
    if (!has_argument(argument_count, argument_values, "--diagnose")) {
        FreeConsole();
    }
#endif
    bool simulate_missing_dep = false;
    std::string missing_dep_target = "";

    for (int i = 1; i < argument_count; ++i) {
        if (std::string(argument_values[i]) == "--simulate-missing-dependency" && i + 1 < argument_count) {
            simulate_missing_dep = true;
            missing_dep_target = argument_values[++i];
        }
    }

    if (simulate_missing_dep) {
        ShowNativeErrorDialog("ZDE Startup Error", "Required runtime component is missing:\n\n" + missing_dep_target + "\n\nThe installation may be incomplete or corrupted.");
        return 1;
    }

    // Locate installation directory
    std::filesystem::path exe_path;
#if defined(__linux__) || defined(__unix__)
    std::error_code ec_proc;
    if (std::filesystem::exists("/proc/self/exe", ec_proc)) {
        exe_path = std::filesystem::canonical("/proc/self/exe", ec_proc).parent_path();
    } else {
        exe_path = std::filesystem::path(argument_values[0]).parent_path();
    }
#else
    exe_path = std::filesystem::path(argument_values[0]).parent_path();
#endif
    std::filesystem::path manifest_path = exe_path / "manifest" / "runtime.json";

    std::string manifest_content = ReadFile(manifest_path.string());
    if (manifest_content.empty()) {
        ShowNativeErrorDialog("ZDE Startup Error", "Failed to read runtime manifest at:\n" + manifest_path.string());
        return 1;
    }

    RuntimeManifest manifest = ParseManifest(manifest_content);
    
    // In merged mode, we assume dependencies are in the same dir
    std::filesystem::path runtime_dir = exe_path; 
    
    if (has_argument(argument_count, argument_values, "--diagnose")) {
        PrintDiagnostic("ZDE Runtime Diagnostics");
        PrintDiagnostic("────────────────────────────");
        PrintDiagnostic("Exe Path:      " + exe_path.string());
        PrintDiagnostic("Dependencies:");
        for (const auto& dep : manifest.dependencies) {
            std::filesystem::path dep_path = runtime_dir / dep.file;
            bool exists = std::filesystem::exists(dep_path);
            PrintDiagnostic("  " + dep.file + (exists ? " [OK]" : " [MISSING]"));
        }
        return 0;
    }

    ValidationResult val_result = ValidateDependencies(runtime_dir.string(), manifest.dependencies);
    if (!val_result.success) {
        ShowNativeErrorDialog("ZDE Startup Error", "Required runtime component is missing:\n\n" + val_result.missing_file + "\n\n" + val_result.error_message);
        return 1;
    }

    Zenvra::Application::ApplicationSpecification specification;

    const bool safe_ui = has_argument(argument_count, argument_values, "--safe-ui");
    const bool native_titlebar = has_argument(argument_count, argument_values, "--native-titlebar");
    const bool smoke_test = has_argument(argument_count, argument_values, "--smoke-test");
    specification.custom_titlebar = !safe_ui && !native_titlebar;
    specification.enable_docking = !safe_ui;
    specification.enable_viewports = false;
    specification.smoke_test = smoke_test;

    for (int i = 1; i < argument_count; ++i) {
        std::string_view arg = argument_values[i];
        if (arg.starts_with("--") || arg.starts_with("-")) {
            if (arg == "--simulate-missing-dependency" && i + 1 < argument_count) {
                ++i;
            }
            continue;
        }
        if (!specification.initial_path) {
            std::error_code ec;
            std::filesystem::path p = std::filesystem::absolute(argument_values[i], ec);
            if (!ec && std::filesystem::exists(p, ec)) {
                specification.initial_path = std::filesystem::weakly_canonical(p, ec);
            }
        }
    }

    Zenvra::Application::Application application(std::move(specification));
    return application.run();
}
