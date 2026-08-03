#include "Commands/CommandRegistry.h"

#include <algorithm>
#include <utility>

namespace Zenvra::Commands
{

std::size_t CommandRegistry::TransparentStringHash::operator()(std::string_view value) const noexcept
{
    return std::hash<std::string_view>{}(value);
}

bool CommandRegistry::register_command(Command command)
{
    if (command.id.empty() || command.name.empty())
    {
        return false;
    }

    auto [iterator, inserted] = m_commands.emplace(command.id, std::move(command));
    if (!inserted)
    {
        return false;
    }

    m_registration_order.push_back(iterator->first);
    return true;
}

bool CommandRegistry::unregister_command(std::string_view command_id)
{
    const auto iterator = m_commands.find(command_id);
    if (iterator == m_commands.end())
    {
        return false;
    }

    m_commands.erase(iterator);
    std::erase(m_registration_order, command_id);
    return true;
}

const Command* CommandRegistry::find_command(std::string_view command_id) const
{
    const auto iterator = m_commands.find(command_id);
    return iterator == m_commands.end() ? nullptr : &iterator->second;
}

CommandExecutionResult CommandRegistry::execute_command(std::string_view command_id) const
{
    const Command* command = find_command(command_id);
    if (command == nullptr)
    {
        return CommandExecutionResult::NotFound;
    }

    if (command->is_enabled && !command->is_enabled())
    {
        return CommandExecutionResult::Disabled;
    }

    if (!command->execute)
    {
        return CommandExecutionResult::MissingHandler;
    }

    command->execute();
    return CommandExecutionResult::Executed;
}

bool CommandRegistry::is_command_enabled(std::string_view command_id) const
{
    const Command* command = find_command(command_id);
    return command != nullptr && (!command->is_enabled || command->is_enabled());
}

bool CommandRegistry::is_command_checked(std::string_view command_id) const
{
    const Command* command = find_command(command_id);
    return command != nullptr && command->is_checked && command->is_checked();
}

std::vector<std::reference_wrapper<const Command>> CommandRegistry::get_commands() const
{
    std::vector<std::reference_wrapper<const Command>> commands;
    commands.reserve(m_registration_order.size());

    for (const CommandID& command_id : m_registration_order)
    {
        const Command* command = find_command(command_id);
        if (command != nullptr)
        {
            commands.emplace_back(*command);
        }
    }

    return commands;
}

std::size_t CommandRegistry::size() const noexcept
{
    return m_commands.size();
}

} // namespace Zenvra::Commands
