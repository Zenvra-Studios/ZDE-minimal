#pragma once

#include "Language/Registry/ServerProfile.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace Zenvra::Language::Registry
{

class ServerRegistry
{
public:
    static ServerRegistry& instance() noexcept;

    void register_profile(ServerProfile profile);

    [[nodiscard]] const ServerProfile* find_profile_for_filename(std::string_view filename) const noexcept;
    [[nodiscard]] const ServerProfile* find_profile_for_extension(std::string_view extension) const noexcept;
    [[nodiscard]] const ServerProfile* find_profile_for_language(std::string_view language_id) const noexcept;
    [[nodiscard]] std::vector<const ServerProfile*> get_all_profiles() const;

    [[nodiscard]] std::filesystem::path find_executable_in_system(std::string_view executable_name) const;

    void initialize_default_profiles();
    void clear_cache() noexcept;

private:
    ServerRegistry();
    std::unordered_map<std::string, ServerProfile> m_profiles_by_language;
    std::unordered_map<std::string, std::string> m_language_by_extension;
    mutable std::mutex m_cache_mutex;
    mutable std::unordered_map<std::string, std::filesystem::path> m_executable_cache;
};

} // namespace Zenvra::Language::Registry
