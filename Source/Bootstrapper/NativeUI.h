#pragma once
#include <string>
#include <vector>
#include <filesystem> // Include for std::filesystem::path

struct Dependency {
    std::string file;
    bool required;
};

struct ValidationResult {
    bool success;
    std::string missing_file;
    std::string error_message;
};

// Runtime manifest structure
struct RuntimeManifest {
    std::string application_name;
    std::string version;
    std::string architecture;
    std::string executable;
    std::vector<Dependency> dependencies;
};

#if defined(_WIN32)
    #if defined(ZDE_BOOTSTRAPPER_BUILD)
        #define ZDE_BOOTSTRAPPER_API __declspec(dllexport)
    #else
        #define ZDE_BOOTSTRAPPER_API __declspec(dllimport)
    #endif
#else
    #define ZDE_BOOTSTRAPPER_API __attribute__((visibility("default")))
#endif

// C++ API
ZDE_BOOTSTRAPPER_API bool Bootstrap(const std::string& base_path, const std::string& command_line);
ZDE_BOOTSTRAPPER_API bool LaunchProcess(const std::string& executable_path, const std::string& command_line);
ZDE_BOOTSTRAPPER_API void ShowNativeErrorDialog(const std::string& title, const std::string& message);
ZDE_BOOTSTRAPPER_API void PrintDiagnostic(const std::string& message);

ZDE_BOOTSTRAPPER_API RuntimeManifest LoadRuntimeManifest(const std::filesystem::path& manifest_path);
ZDE_BOOTSTRAPPER_API ValidationResult ValidateDependencies(const std::filesystem::path& base_path, const std::vector<Dependency>& dependencies);
ZDE_BOOTSTRAPPER_API ValidationResult ValidateArchitecture(const std::filesystem::path& file_path);
ZDE_BOOTSTRAPPER_API RuntimeManifest ParseManifest(const std::string& json_content);
