#pragma once

#include "Settings/SettingsValue.h"

#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Settings
{

struct SettingOption
{
    std::string label;
    std::string value;

    bool operator==(const SettingOption& other) const = default;
};

struct SettingDefinition
{
    std::string id;
    std::string title;
    std::string description;

    SettingType type = SettingType::String;
    SettingsValue defaultValue;

    std::string category = "General";
    std::string subcategory;

    std::vector<std::string> tags;

    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;

    std::vector<SettingOption> enum_values;

    bool requires_restart = false;
    bool is_hidden = false;
};

} // namespace Zenvra::Settings
