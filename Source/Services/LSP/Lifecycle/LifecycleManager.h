#pragma once

#include "Services/LSP/Lifecycle/LifecycleState.h"

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::LSP::Lifecycle
{

class LifecycleManager
{
public:
    using StateChangedCallback = std::function<void(LifecycleState old_state, LifecycleState new_state)>;

    LifecycleManager(std::string server_id = {});

    [[nodiscard]] LifecycleState get_state() const noexcept { return m_state; }
    [[nodiscard]] bool is_running() const noexcept { return m_state == LifecycleState::Running; }
    [[nodiscard]] bool is_active() const noexcept
    {
        return m_state == LifecycleState::Starting ||
               m_state == LifecycleState::Initializing ||
               m_state == LifecycleState::Running;
    }

    bool transition_to(LifecycleState new_state, std::string_view reason = {});
    void set_state_changed_listener(StateChangedCallback listener);

    [[nodiscard]] const std::string& get_server_id() const noexcept { return m_server_id; }
    [[nodiscard]] std::size_t get_restart_count() const noexcept { return m_restart_count; }
    void increment_restart_count() noexcept { ++m_restart_count; }
    void reset_restart_count() noexcept { m_restart_count = 0; }

private:
    [[nodiscard]] bool is_valid_transition(LifecycleState from, LifecycleState to) const noexcept;

    std::string m_server_id;
    LifecycleState m_state{LifecycleState::Uninitialized};
    std::size_t m_restart_count{0};
    mutable std::mutex m_mutex;
    StateChangedCallback m_listener;
};

} // namespace Zenvra::Services::LSP::Lifecycle

