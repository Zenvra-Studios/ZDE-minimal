#pragma once

#include "Settings/SettingsValue.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Settings
{

enum class SettingsScope
{
    Default,
    User,
    Workspace
};

class SettingsStore
{
public:
    virtual ~SettingsStore() = default;

    virtual bool load() = 0;
    virtual bool save() = 0;

    [[nodiscard]] virtual bool contains(std::string_view key) const = 0;
    [[nodiscard]] virtual std::optional<SettingsValue> get(std::string_view key) const = 0;
    virtual void set(std::string_view key, SettingsValue value) = 0;
    virtual void remove(std::string_view key) = 0;
    virtual void clear() = 0;
    [[nodiscard]] virtual std::vector<std::string> get_keys() const = 0;
};

} // namespace Zenvra::Settings
