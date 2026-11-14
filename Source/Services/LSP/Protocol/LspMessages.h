#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Zenvra::Services::LSP::Protocol
{

struct JsonRpcRequest
{
    std::int64_t id{0};
    std::string method;
    nlohmann::json params = nlohmann::json::object();
};

struct JsonRpcResponse
{
    std::int64_t id{0};
    nlohmann::json result = nullptr;
    std::optional<nlohmann::json> error;

    [[nodiscard]] bool is_error() const noexcept { return error.has_value(); }
};

struct JsonRpcNotification
{
    std::string method;
    nlohmann::json params = nlohmann::json::object();
};

} // namespace Zenvra::Services::LSP::Protocol

