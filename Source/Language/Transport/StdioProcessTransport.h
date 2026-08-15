#pragma once

#include "Language/Transport/ILspTransport.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Zenvra::Language::Transport
{

class StdioProcessTransport final : public ILspTransport
{
public:
    StdioProcessTransport(
        std::filesystem::path executable_path,
        std::vector<std::string> arguments = {},
        std::filesystem::path working_directory = {});
    ~StdioProcessTransport() override;

    bool start() override;
    void stop() override;
    [[nodiscard]] bool is_running() const noexcept override;
    bool send(std::string_view payload) override;

    void set_message_handler(std::function<void(std::string_view)> handler) override;
    void set_error_handler(std::function<void(std::string_view)> handler) override;

private:
    struct ProcessHandle;

    void reader_thread_loop();

    std::filesystem::path m_executable_path;
    std::vector<std::string> m_arguments;
    std::filesystem::path m_working_directory;

    std::unique_ptr<ProcessHandle> m_process;
    std::thread m_reader_thread;
    std::atomic<bool> m_running{false};
    std::mutex m_write_mutex;

    std::function<void(std::string_view)> m_message_handler;
    std::function<void(std::string_view)> m_error_handler;
};

} // namespace Zenvra::Language::Transport
