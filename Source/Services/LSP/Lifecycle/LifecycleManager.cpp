#include "Services/LSP/Lifecycle/LifecycleManager.h"

namespace Zenvra::Services::LSP::Lifecycle
{

LifecycleManager::LifecycleManager(std::string server_id)
    : m_server_id(std::move(server_id))
{
}

bool LifecycleManager::is_valid_transition(LifecycleState from, LifecycleState to) const noexcept
{
    if (from == to) return true;

    switch (from)
    {
    case LifecycleState::Uninitialized:
        return to == LifecycleState::Discovering || to == LifecycleState::Starting || to == LifecycleState::Missing;

    case LifecycleState::Discovering:
        return to == LifecycleState::Starting || to == LifecycleState::Missing || to == LifecycleState::Failed;

    case LifecycleState::Missing:
        return to == LifecycleState::Installing || to == LifecycleState::Discovering || to == LifecycleState::Failed;

    case LifecycleState::Installing:
        return to == LifecycleState::Starting || to == LifecycleState::Failed || to == LifecycleState::Missing;

    case LifecycleState::Starting:
        return to == LifecycleState::Initializing || to == LifecycleState::Crashed || to == LifecycleState::Failed || to == LifecycleState::Stopping;

    case LifecycleState::Initializing:
        return to == LifecycleState::Running || to == LifecycleState::Stopping || to == LifecycleState::Crashed || to == LifecycleState::Failed;

    case LifecycleState::Running:
        return to == LifecycleState::Stopping || to == LifecycleState::Crashed || to == LifecycleState::Failed;

    case LifecycleState::Stopping:
        return to == LifecycleState::Uninitialized || to == LifecycleState::Failed;

    case LifecycleState::Crashed:
        return to == LifecycleState::Starting || to == LifecycleState::Uninitialized || to == LifecycleState::Failed;

    case LifecycleState::Failed:
        return to == LifecycleState::Discovering || to == LifecycleState::Starting || to == LifecycleState::Uninitialized;
    }

    return false;
}

bool LifecycleManager::transition_to(LifecycleState new_state, std::string_view /*reason*/)
{
    LifecycleState old_state;
    StateChangedCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!is_valid_transition(m_state, new_state))
        {
            return false;
        }
        old_state = m_state;
        m_state = new_state;
        cb = m_listener;
    }

    if (cb && old_state != new_state)
    {
        cb(old_state, new_state);
    }
    return true;
}

void LifecycleManager::set_state_changed_listener(StateChangedCallback listener)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listener = std::move(listener);
}

} // namespace Zenvra::Services::LSP::Lifecycle

