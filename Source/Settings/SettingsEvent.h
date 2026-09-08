#pragma once

#include "Settings/SettingsStore.h"
#include "Settings/SettingsValue.h"

#include <functional>
#include <string>

namespace Zenvra::Settings
{

struct SettingsChangedEvent
{
    std::string id;
    SettingsValue old_value;
    SettingsValue new_value;
    SettingsScope scope = SettingsScope::User;
};

using SettingsChangeCallback = std::function<void(const SettingsChangedEvent&)>;

} // namespace Zenvra::Settings
