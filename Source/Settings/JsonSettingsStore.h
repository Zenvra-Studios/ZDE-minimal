#pragma once

#include "Settings/SettingsStore.h"

#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace Zenvra::Settings
{

class JsonSettingsStore : public SettingsStore
{
public:
    explicit JsonSettingsStore(std::filesystem::path file_path);
    ~JsonSettingsStore() override = default;

    bool load() override;
    bool save() override;

    [[nodiscard]] bool contains(std::string_view key) const override;
    [[nodiscard]] std::optional<SettingsValue> get(std::string_view key) const override;
    void set(std::string_view key, SettingsValue value) override;
    void remove(std::string_view key) override;
    void clear() override;
    [[nodiscard]] std::vector<std::string> get_keys() const override;

    [[nodiscard]] const std::filesystem::path& get_file_path() const noexcept {
        return m_file_path;
    }

private:
    std::filesystem::path m_file_path;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, SettingsValue> m_cache;
};

} // namespace Zenvra::Settings
