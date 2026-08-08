#include "../NativeUI.h"
#include "../NativeUI.h"
#include <filesystem>
#include <windows.h>

ValidationResult ValidateDependencies(const std::filesystem::path& base_path, const std::vector<Dependency>& dependencies) {
    for (const auto& dep : dependencies) {
        if (dep.required) {
            std::filesystem::path dep_path = std::filesystem::path(base_path) / dep.file;
            if (!std::filesystem::exists(dep_path)) {
                return {false, dep.file, "The installation may be incomplete or corrupted."};
            }
        }
    }
    return {true, "", ""};
}

ValidationResult ValidateArchitecture(const std::filesystem::path& file_path) {
    std::wstring wfile_path = file_path.wstring();
    HMODULE hMod = LoadLibraryExW(wfile_path.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (hMod != NULL) {
        FreeLibrary(hMod);
        return {true, "", ""};
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_BAD_EXE_FORMAT) {
            return {false, file_path.string(), "Invalid architecture (e.g. 32-bit vs 64-bit mismatch)."};
        }
        return {false, file_path.string(), "Could not validate file format."};
    }
}
