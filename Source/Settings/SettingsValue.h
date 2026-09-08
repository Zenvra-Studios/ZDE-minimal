#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

namespace Zenvra::Settings
{

enum class SettingType
{
    Boolean,
    Integer,
    Float,
    String,
    Enum,
    Array
};

class SettingsValue
{
public:
    using ValueType = std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        std::vector<std::string>
    >;

    SettingsValue() : m_value(std::monostate{}) {}
    SettingsValue(bool val) : m_value(val) {}
    SettingsValue(int val) : m_value(static_cast<int64_t>(val)) {}
    SettingsValue(int64_t val) : m_value(val) {}
    SettingsValue(double val) : m_value(val) {}
    SettingsValue(float val) : m_value(static_cast<double>(val)) {}
    SettingsValue(const char* val) : m_value(std::string(val)) {}
    SettingsValue(std::string val) : m_value(std::move(val)) {}
    SettingsValue(std::string_view val) : m_value(std::string(val)) {}
    SettingsValue(std::vector<std::string> val) : m_value(std::move(val)) {}

    [[nodiscard]] bool is_null() const noexcept {
        return std::holds_alternative<std::monostate>(m_value);
    }

    [[nodiscard]] bool is_bool() const noexcept {
        return std::holds_alternative<bool>(m_value);
    }

    [[nodiscard]] bool is_int() const noexcept {
        return std::holds_alternative<int64_t>(m_value);
    }

    [[nodiscard]] bool is_float() const noexcept {
        return std::holds_alternative<double>(m_value) || std::holds_alternative<int64_t>(m_value);
    }

    [[nodiscard]] bool is_string() const noexcept {
        return std::holds_alternative<std::string>(m_value);
    }

    [[nodiscard]] bool is_array() const noexcept {
        return std::holds_alternative<std::vector<std::string>>(m_value);
    }

    [[nodiscard]] bool as_bool(bool fallback = false) const noexcept {
        if (const auto* val = std::get_if<bool>(&m_value)) return *val;
        if (const auto* val = std::get_if<int64_t>(&m_value)) return *val != 0;
        return fallback;
    }

    [[nodiscard]] int64_t as_int(int64_t fallback = 0) const noexcept {
        if (const auto* val = std::get_if<int64_t>(&m_value)) return *val;
        if (const auto* val = std::get_if<double>(&m_value)) return static_cast<int64_t>(*val);
        if (const auto* val = std::get_if<bool>(&m_value)) return *val ? 1 : 0;
        return fallback;
    }

    [[nodiscard]] double as_float(double fallback = 0.0) const noexcept {
        if (const auto* val = std::get_if<double>(&m_value)) return *val;
        if (const auto* val = std::get_if<int64_t>(&m_value)) return static_cast<double>(*val);
        return fallback;
    }

    [[nodiscard]] std::string as_string(std::string_view fallback = "") const {
        if (const auto* val = std::get_if<std::string>(&m_value)) return *val;
        return std::string(fallback);
    }

    [[nodiscard]] std::vector<std::string> as_array() const {
        if (const auto* val = std::get_if<std::vector<std::string>>(&m_value)) return *val;
        return {};
    }

    [[nodiscard]] std::string to_display_string() const {
        if (const auto* val = std::get_if<bool>(&m_value)) {
            return *val ? "true" : "false";
        }
        if (const auto* val = std::get_if<int64_t>(&m_value)) {
            return std::to_string(*val);
        }
        if (const auto* val = std::get_if<double>(&m_value)) {
            std::string str = std::to_string(*val);
            str.erase(str.find_last_not_of('0') + 1, std::string::npos);
            if (str.back() == '.') str.pop_back();
            return str;
        }
        if (const auto* val = std::get_if<std::string>(&m_value)) {
            return *val;
        }
        if (const auto* val = std::get_if<std::vector<std::string>>(&m_value)) {
            std::string result = "[";
            for (std::size_t i = 0; i < val->size(); ++i) {
                if (i > 0) result += ", ";
                result += (*val)[i];
            }
            result += "]";
            return result;
        }
        return "";
    }

    [[nodiscard]] SettingType get_type() const noexcept {
        if (std::holds_alternative<bool>(m_value)) return SettingType::Boolean;
        if (std::holds_alternative<int64_t>(m_value)) return SettingType::Integer;
        if (std::holds_alternative<double>(m_value)) return SettingType::Float;
        if (std::holds_alternative<std::vector<std::string>>(m_value)) return SettingType::Array;
        return SettingType::String;
    }

    [[nodiscard]] nlohmann::json to_json() const {
        if (const auto* val = std::get_if<bool>(&m_value)) {
            return *val;
        }
        if (const auto* val = std::get_if<int64_t>(&m_value)) {
            return *val;
        }
        if (const auto* val = std::get_if<double>(&m_value)) {
            return *val;
        }
        if (const auto* val = std::get_if<std::string>(&m_value)) {
            return *val;
        }
        if (const auto* val = std::get_if<std::vector<std::string>>(&m_value)) {
            return *val;
        }
        return nullptr;
    }

    [[nodiscard]] static SettingsValue from_json(const nlohmann::json& j) {
        if (j.is_boolean()) {
            return SettingsValue(j.get<bool>());
        }
        if (j.is_number_integer()) {
            return SettingsValue(j.get<int64_t>());
        }
        if (j.is_number_float()) {
            return SettingsValue(j.get<double>());
        }
        if (j.is_string()) {
            return SettingsValue(j.get<std::string>());
        }
        if (j.is_array()) {
            std::vector<std::string> arr;
            for (const auto& el : j) {
                if (el.is_string()) {
                    arr.push_back(el.get<std::string>());
                } else {
                    arr.push_back(el.dump());
                }
            }
            return SettingsValue(std::move(arr));
        }
        return SettingsValue();
    }

    bool operator==(const SettingsValue& other) const = default;

private:
    ValueType m_value;
};

} // namespace Zenvra::Settings
