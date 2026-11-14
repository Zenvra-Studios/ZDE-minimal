#include "Services/LSP/Transport/StdioTransport.h"

#include <cstdlib>
#include <sstream>

namespace Zenvra::Services::LSP::Transport
{

StdioTransport::StdioTransport(Process::ProcessSpec spec)
    : m_spec(std::move(spec))
{
    m_supervisor.set_stdout_listener([this](std::string_view chunk) {
        handle_raw_stdout_chunk(chunk);
    });
    m_supervisor.set_stderr_listener([this](std::string_view err_chunk) {
        std::function<void(std::string_view)> err_cb;
        {
            std::lock_guard<std::mutex> lock(m_buffer_mutex);
            err_cb = m_error_handler;
        }
        if (err_cb)
        {
            err_cb(err_chunk);
        }
    });
}

StdioTransport::~StdioTransport()
{
    stop();
}

bool StdioTransport::start()
{
    return m_supervisor.start(m_spec);
}

void StdioTransport::stop()
{
    m_supervisor.stop();
}

bool StdioTransport::is_running() const noexcept
{
    return m_supervisor.is_running();
}

bool StdioTransport::send(std::string_view payload)
{
    if (!is_running() || payload.empty()) return false;

    std::string framed = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
    framed.append(payload);
    return m_supervisor.write_input(framed);
}

void StdioTransport::set_message_handler(std::function<void(std::string_view)> handler)
{
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
    m_message_handler = std::move(handler);
}

void StdioTransport::set_error_handler(std::function<void(std::string_view)> handler)
{
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
    m_error_handler = std::move(handler);
}

void StdioTransport::handle_raw_stdout_chunk(std::string_view chunk)
{
    std::vector<std::string> completed_messages;
    {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);
        m_read_buffer.append(chunk);

        while (true)
        {
            const auto header_end = m_read_buffer.find("\r\n\r\n");
            if (header_end == std::string::npos) break;

            const std::string header = m_read_buffer.substr(0, header_end);
            const auto cl_pos = header.find("Content-Length: ");
            if (cl_pos == std::string::npos)
            {
                // Discard malformed header
                m_read_buffer.erase(0, header_end + 4);
                continue;
            }

            const std::size_t content_len = std::strtoul(header.c_str() + cl_pos + 16, nullptr, 10);
            const std::size_t body_start = header_end + 4;

            if (m_read_buffer.size() < body_start + content_len)
            {
                // Not enough bytes yet, wait for next chunk
                break;
            }

            completed_messages.push_back(m_read_buffer.substr(body_start, content_len));
            m_read_buffer.erase(0, body_start + content_len);
        }
    }

    if (m_message_handler)
    {
        for (const auto& msg : completed_messages)
        {
            m_message_handler(msg);
        }
    }
}

} // namespace Zenvra::Services::LSP::Transport

