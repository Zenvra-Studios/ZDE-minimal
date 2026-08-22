#include "../NativeUI.h"
#include "../NativeUI.h"
#include <filesystem>

ValidationResult ValidateDependencies(const std::filesystem::path& base_path, const std::vector<Dependency>& dependencies) {
    const std::vector<std::filesystem::path> search_dirs = {
        base_path,
        base_path / "lib",
        base_path / "lib" / "zde",
        base_path.parent_path() / "lib" / "zde",
        base_path.parent_path() / "lib64" / "zde",
        std::filesystem::path{"/usr/lib/zde"},
        std::filesystem::path{"/usr/lib64/zde"},
        std::filesystem::path{"/usr/local/lib/zde"},
    };
    for (const auto& dep : dependencies) {
        if (dep.required) {
            bool found = false;
            std::error_code ec;
            for (const auto& dir : search_dirs) {
                if (std::filesystem::is_regular_file(dir / dep.file, ec)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return {false, dep.file, "The installation may be incomplete or corrupted."};
            }
        }
    }
    return {true, "", ""};
}

ValidationResult ValidateArchitecture(const std::filesystem::path& file_path) {
    // Add elf header parsing here if needed
    return {true, "", ""};
}
