#pragma once

#include "Commands/Command.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Commands
{

class CommandRegistry
{
public:
    [[nodiscard]] bool register_command(Command command);
    [[nodiscard]] bool unregister_command(std::string_view command_id);

    [[nodiscard]] const Command* find_command(std::string_view command_id) const;
    [[nodiscard]] CommandExecutionResult execute_command(std::string_view command_id) const;
    [[nodiscard]] bool is_command_enabled(std::string_view command_id) const;
    [[nodiscard]] bool is_command_checked(std::string_view command_id) const;
    [[nodiscard]] std::vector<std::reference_wrapper<const Command>> get_commands() const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct TransparentStringHash
    {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
    };

    using CommandMap = std::unordered_map<CommandID, Command, TransparentStringHash, std::equal_to<>>;

    CommandMap m_commands;
    std::vector<CommandID> m_registration_order;
};

} // namespace Zenvra::Commands
