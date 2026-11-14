#include "Services/LSP/Protocol/LspSerializer.h"

namespace Zenvra::Services::LSP::Protocol
{

std::string LspSerializer::serialize_request(const JsonRpcRequest& req)
{
    nlohmann::json j;
    j["jsonrpc"] = "2.0";
    j["id"] = req.id;
    j["method"] = req.method;
    j["params"] = req.params;
    return j.dump();
}

std::string LspSerializer::serialize_notification(const JsonRpcNotification& notif)
{
    nlohmann::json j;
    j["jsonrpc"] = "2.0";
    j["method"] = notif.method;
    j["params"] = notif.params;
    return j.dump();
}

std::string LspSerializer::serialize_response(const JsonRpcResponse& resp)
{
    nlohmann::json j;
    j["jsonrpc"] = "2.0";
    j["id"] = resp.id;
    if (resp.error)
    {
        j["error"] = *resp.error;
    }
    else
    {
        j["result"] = resp.result;
    }
    return j.dump();
}

std::optional<JsonRpcResponse> LspSerializer::parse_response(std::string_view json_str)
{
    try
    {
        auto j = nlohmann::json::parse(json_str);
        if (j.contains("id") && (j.contains("result") || j.contains("error")))
        {
            JsonRpcResponse resp;
            if (j["id"].is_number())
            {
                resp.id = j["id"].get<std::int64_t>();
            }
            if (j.contains("result"))
            {
                resp.result = j["result"];
            }
            if (j.contains("error") && !j["error"].is_null())
            {
                resp.error = j["error"];
            }
            return resp;
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::optional<JsonRpcNotification> LspSerializer::parse_notification(std::string_view json_str)
{
    try
    {
        auto j = nlohmann::json::parse(json_str);
        if (!j.contains("id") && j.contains("method"))
        {
            JsonRpcNotification notif;
            notif.method = j["method"].get<std::string>();
            if (j.contains("params"))
            {
                notif.params = j["params"];
            }
            return notif;
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

} // namespace Zenvra::Services::LSP::Protocol

