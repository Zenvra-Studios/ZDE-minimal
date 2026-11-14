#pragma once

#include "Services/LSP/Process/ProcessSupervisor.h"
#include "Services/LSP/Transport/ILspTransport.h"

#include <memory>
#include <mutex>
#include <string>

namespace Zenvra::Services::LSP::Transport
{

class StdioTransport final : public ILspTransport
{
public:
    explicit StdioTransport(Process::ProcessSpec spec);
    ~StdioTransport() override;

    bool start() override;
    void stop() override;
    [[nodiscard]] bool is_running() const noexcept override;
    bool send(std::string_view payload) override;

    void set_message_handler(std::function<void(std::string_view)> handler) override;
    void set_error_handler(std::function<void(std::string_view)> handler) override;

    [[nodiscard]] Process::ProcessSupervisor& get_supervisor() noexcept { return m_supervisor; }

private:
    void handle_raw_stdout_chunk(std::string_view chunk);

    Process::ProcessSpec m_spec;
    Process::ProcessSupervisor m_supervisor;

    std::mutex m_buffer_mutex;
    std::string m_read_buffer;

    std::function<void(std::string_view)> m_message_handler;
    std::function<void(std::string_view)> m_error_handler;
};

} // namespace Zenvra::Services::LSP::Transport

