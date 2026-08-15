#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Zenvra::Language::Protocol
{

struct JsonRpcRequest
{
    int64_t id = 0;
    std::string method;
    nlohmann::json params;
};

struct JsonRpcResponse
{
    int64_t id = 0;
    nlohmann::json result;
    std::optional<nlohmann::json> error;
    bool is_error() const noexcept { return error.has_value(); }
};

struct JsonRpcNotification
{
    std::string method;
    nlohmann::json params;
};

} // namespace Zenvra::Language::Protocol
