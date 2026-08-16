#include "Tools/Debugger/DebuggerEngine.h"

#include <iostream>

namespace Zenvra::Tools::Debugger
{

bool DebuggerEngine::start_session(
    const DebugSessionOptions& /*options*/,
    std::function<void(std::string_view)> event_callback)
{
    m_active = true;
    if (event_callback)
    {
        event_callback("Debug session started.");
    }
    return true;
}

void DebuggerEngine::stop_session()
{
    m_active = false;
}

void DebuggerEngine::pause() {}
void DebuggerEngine::resume() {}
void DebuggerEngine::step_over() {}
void DebuggerEngine::step_into() {}
void DebuggerEngine::step_out() {}

} // namespace Zenvra::Tools::Debugger
