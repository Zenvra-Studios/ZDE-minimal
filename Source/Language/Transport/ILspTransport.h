#pragma once

#include <functional>
#include <string_view>

namespace Zenvra::Language::Transport
{

class ILspTransport
{
public:
    virtual ~ILspTransport() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool is_running() const noexcept = 0;
    virtual bool send(std::string_view payload) = 0;

    virtual void set_message_handler(std::function<void(std::string_view)> handler) = 0;
    virtual void set_error_handler(std::function<void(std::string_view)> handler) = 0;
};

} // namespace Zenvra::Language::Transport
