#pragma once

#include "Settings/SettingsDefinition.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Settings
{

class SettingsSchema
{
public:
    SettingsSchema();
    ~SettingsSchema();

    SettingsSchema(const SettingsSchema&) = delete;
    SettingsSchema& operator=(const SettingsSchema&) = delete;
    SettingsSchema(SettingsSchema&&) noexcept = default;
    SettingsSchema& operator=(SettingsSchema&&) noexcept = default;

    void register_setting(SettingDefinition definition);
    [[nodiscard]] bool has_setting(std::string_view id) const noexcept;
    [[nodiscard]] const SettingDefinition* get_setting(std::string_view id) const noexcept;

    [[nodiscard]] std::vector<std::string> get_categories() const;
    [[nodiscard]] std::vector<const SettingDefinition*> get_settings_by_category(std::string_view category) const;
    [[nodiscard]] std::vector<const SettingDefinition*> search(std::string_view query) const;
    [[nodiscard]] std::vector<const SettingDefinition*> get_all_settings() const;

    [[nodiscard]] bool validate(
        std::string_view id,
        const SettingsValue& value,
        SettingsValue* out_clamped = nullptr) const;

private:
    std::vector<SettingDefinition> m_definitions;
    std::unordered_map<std::string, std::size_t> m_id_to_index;
    std::vector<std::string> m_categories;
};

} // namespace Zenvra::Settings
