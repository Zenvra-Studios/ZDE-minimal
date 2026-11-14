#pragma once

#include "Services/LSP/Protocol/LspMessages.h"

#include <optional>
#include <string>
#include <string_view>

namespace Zenvra::Services::LSP::Protocol
{

class LspSerializer
{
public:
    [[nodiscard]] static std::string serialize_request(const JsonRpcRequest& req);
    [[nodiscard]] static std::string serialize_notification(const JsonRpcNotification& notif);
    [[nodiscard]] static std::string serialize_response(const JsonRpcResponse& resp);

    [[nodiscard]] static std::optional<JsonRpcResponse> parse_response(std::string_view json_str);
    [[nodiscard]] static std::optional<JsonRpcNotification> parse_notification(std::string_view json_str);
};

} // namespace Zenvra::Services::LSP::Protocol

