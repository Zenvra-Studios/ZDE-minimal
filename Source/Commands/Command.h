#pragma once

#include "Core/Types.h"

#include <functional>
#include <string>

namespace Zenvra::Commands
{

enum class KeyCode
{
    None,
    N,
    O,
    P,
    W,
    Z,
};

struct Shortcut
{
    KeyCode key = KeyCode::None;
    bool control = false;
    bool shift = false;
    bool alt = false;
};

struct Command
{
    CommandID id;
    std::string name;
    std::string description;
    std::string category;
    Shortcut shortcut_binding;
    std::function<void()> execute;
    std::function<bool()> is_enabled;
    std::function<bool()> is_checked;
};

enum class CommandExecutionResult
{
    Executed,
    NotFound,
    Disabled,
    MissingHandler,
};

} // namespace Zenvra::Commands
