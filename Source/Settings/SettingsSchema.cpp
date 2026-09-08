#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::Settings
{

namespace
{

std::string to_lower(std::string_view str)
{
    std::string lower;
    lower.reserve(str.size());
    for (char ch : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower;
}

} // namespace

SettingsSchema::SettingsSchema() = default;
SettingsSchema::~SettingsSchema() = default;

void SettingsSchema::register_setting(SettingDefinition definition)
{
    const auto it = m_id_to_index.find(definition.id);
    if (it != m_id_to_index.end()) {
        m_definitions[it->second] = std::move(definition);
        return;
    }

    if (!definition.category.empty()) {
        if (std::find(m_categories.begin(), m_categories.end(), definition.category) == m_categories.end()) {
            m_categories.push_back(definition.category);
        }
    }

    const std::size_t index = m_definitions.size();
    m_id_to_index[definition.id] = index;
    m_definitions.push_back(std::move(definition));
}

bool SettingsSchema::has_setting(std::string_view id) const noexcept
{
    return m_id_to_index.find(std::string(id)) != m_id_to_index.end();
}

const SettingDefinition* SettingsSchema::get_setting(std::string_view id) const noexcept
{
    const auto it = m_id_to_index.find(std::string(id));
    if (it == m_id_to_index.end()) {
        return nullptr;
    }
    return &m_definitions[it->second];
}

std::vector<std::string> SettingsSchema::get_categories() const
{
    return m_categories;
}

std::vector<const SettingDefinition*> SettingsSchema::get_settings_by_category(std::string_view category) const
{
    std::vector<const SettingDefinition*> result;
    for (const auto& def : m_definitions) {
        if (!def.is_hidden && (category == "All" || def.category == category)) {
            result.push_back(&def);
        }
    }
    return result;
}

std::vector<const SettingDefinition*> SettingsSchema::search(std::string_view query) const
{
    if (query.empty()) {
        return get_settings_by_category("All");
    }

    const std::string lower_query = to_lower(query);
    std::vector<const SettingDefinition*> result;

    for (const auto& def : m_definitions) {
        if (def.is_hidden) continue;

        if (to_lower(def.id).find(lower_query) != std::string::npos ||
            to_lower(def.title).find(lower_query) != std::string::npos ||
            to_lower(def.description).find(lower_query) != std::string::npos ||
            to_lower(def.category).find(lower_query) != std::string::npos ||
            to_lower(def.subcategory).find(lower_query) != std::string::npos)
        {
            result.push_back(&def);
            continue;
        }

        bool matched_tag = false;
        for (const auto& tag : def.tags) {
            if (to_lower(tag).find(lower_query) != std::string::npos) {
                matched_tag = true;
                break;
            }
        }
        if (matched_tag) {
            result.push_back(&def);
        }
    }

    return result;
}

std::vector<const SettingDefinition*> SettingsSchema::get_all_settings() const
{
    std::vector<const SettingDefinition*> result;
    result.reserve(m_definitions.size());
    for (const auto& def : m_definitions) {
        result.push_back(&def);
    }
    return result;
}

bool SettingsSchema::validate(
    std::string_view id,
    const SettingsValue& value,
    SettingsValue* out_clamped) const
{
    const auto* def = get_setting(id);
    if (!def) {
        if (out_clamped) *out_clamped = value;
        return true;
    }

    if (def->type == SettingType::Integer) {
        int64_t val = value.as_int();
        bool clamped = false;
        if (def->minimum.has_value() && static_cast<double>(val) < *def->minimum) {
            val = static_cast<int64_t>(*def->minimum);
            clamped = true;
        }
        if (def->maximum.has_value() && static_cast<double>(val) > *def->maximum) {
            val = static_cast<int64_t>(*def->maximum);
            clamped = true;
        }
        if (out_clamped) *out_clamped = SettingsValue(val);
        return !clamped;
    }

    if (def->type == SettingType::Float) {
        double val = value.as_float();
        bool clamped = false;
        if (def->minimum.has_value() && val < *def->minimum) {
            val = *def->minimum;
            clamped = true;
        }
        if (def->maximum.has_value() && val > *def->maximum) {
            val = *def->maximum;
            clamped = true;
        }
        if (out_clamped) *out_clamped = SettingsValue(val);
        return !clamped;
    }

    if (def->type == SettingType::Enum && !def->enum_values.empty()) {
        const std::string val_str = value.as_string();
        for (const auto& opt : def->enum_values) {
            if (opt.value == val_str) {
                if (out_clamped) *out_clamped = value;
                return true;
            }
        }
        // Fallback to default
        if (out_clamped) *out_clamped = def->defaultValue;
        return false;
    }

    if (out_clamped) *out_clamped = value;
    return true;
}

} // namespace Zenvra::Settings
