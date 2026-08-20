#include "Language/Protocol/LspProtocolSerializer.h"

namespace Zenvra::Language::Protocol
{

std::string LspProtocolSerializer::frame_payload(std::string_view json_payload)
{
    std::string header = "Content-Length: " + std::to_string(json_payload.size()) + "\r\n\r\n";
    header.append(json_payload);
    return header;
}

std::string LspProtocolSerializer::serialize_request(const JsonRpcRequest& req)
{
    nlohmann::json root = {
        {"jsonrpc", "2.0"},
        {"id", req.id},
        {"method", req.method}
    };
    if (!req.params.is_null())
    {
        root["params"] = req.params;
    }
    return frame_payload(root.dump());
}

std::string LspProtocolSerializer::serialize_notification(const JsonRpcNotification& notif)
{
    nlohmann::json root = {
        {"jsonrpc", "2.0"},
        {"method", notif.method}
    };
    if (!notif.params.is_null())
    {
        root["params"] = notif.params;
    }
    return frame_payload(root.dump());
}

std::string LspProtocolSerializer::serialize_response(const JsonRpcResponse& res)
{
    nlohmann::json root = {
        {"jsonrpc", "2.0"},
        {"id", res.id}
    };
    if (res.error.has_value())
    {
        root["error"] = *res.error;
    }
    else
    {
        root["result"] = res.result;
    }
    return frame_payload(root.dump());
}

Position LspProtocolSerializer::parse_position(const nlohmann::json& j)
{
    Position pos;
    if (j.is_object())
    {
        if (j.contains("line") && j["line"].is_number()) pos.line = j["line"].get<std::size_t>();
        if (j.contains("character") && j["character"].is_number()) pos.character = j["character"].get<std::size_t>();
    }
    return pos;
}

nlohmann::json LspProtocolSerializer::serialize_position(const Position& pos)
{
    return {
        {"line", pos.line},
        {"character", pos.character}
    };
}

Range LspProtocolSerializer::parse_range(const nlohmann::json& j)
{
    Range range;
    if (j.is_object())
    {
        if (j.contains("start")) range.start = parse_position(j["start"]);
        if (j.contains("end")) range.end = parse_position(j["end"]);
    }
    return range;
}

nlohmann::json LspProtocolSerializer::serialize_range(const Range& range)
{
    return {
        {"start", serialize_position(range.start)},
        {"end", serialize_position(range.end)}
    };
}

Location LspProtocolSerializer::parse_location(const nlohmann::json& j)
{
    Location loc;
    if (j.is_object())
    {
        if (j.contains("uri") && j["uri"].is_string()) loc.uri = j["uri"].get<std::string>();
        if (j.contains("range")) loc.range = parse_range(j["range"]);
    }
    return loc;
}

std::vector<Location> LspProtocolSerializer::parse_locations(const nlohmann::json& j)
{
    std::vector<Location> locations;
    if (j.is_array())
    {
        for (const auto& item : j)
        {
            locations.push_back(parse_location(item));
        }
    }
    else if (j.is_object())
    {
        locations.push_back(parse_location(j));
    }
    return locations;
}

Diagnostic LspProtocolSerializer::parse_diagnostic(const nlohmann::json& j)
{
    Diagnostic diag;
    if (j.is_object())
    {
        if (j.contains("range")) diag.range = parse_range(j["range"]);
        if (j.contains("severity") && j["severity"].is_number())
        {
            diag.severity = static_cast<DiagnosticSeverity>(j["severity"].get<int>());
        }
        if (j.contains("message") && j["message"].is_string()) diag.message = j["message"].get<std::string>();
        if (j.contains("source") && j["source"].is_string()) diag.source = j["source"].get<std::string>();
        if (j.contains("code"))
        {
            if (j["code"].is_string()) diag.code = j["code"].get<std::string>();
            else if (j["code"].is_number()) diag.code = std::to_string(j["code"].get<int64_t>());
        }
        if (j.contains("tags") && j["tags"].is_array())
        {
            for (const auto& t : j["tags"])
            {
                if (t.is_number())
                {
                    diag.tags.push_back(static_cast<DiagnosticTag>(t.get<int>()));
                }
            }
        }
    }
    return diag;
}

std::vector<Diagnostic> LspProtocolSerializer::parse_diagnostics(const nlohmann::json& j)
{
    std::vector<Diagnostic> diags;
    if (j.is_array())
    {
        for (const auto& item : j)
        {
            diags.push_back(parse_diagnostic(item));
        }
    }
    return diags;
}

CompletionItem LspProtocolSerializer::parse_completion_item(const nlohmann::json& j)
{
    CompletionItem item;
    if (j.is_object())
    {
        if (j.contains("label") && j["label"].is_string()) item.label = j["label"].get<std::string>();
        if (j.contains("kind") && j["kind"].is_number())
        {
            item.kind = static_cast<CompletionItemKind>(j["kind"].get<int>());
        }
        if (j.contains("detail") && j["detail"].is_string()) item.detail = j["detail"].get<std::string>();
        if (j.contains("documentation"))
        {
            if (j["documentation"].is_string()) item.documentation = j["documentation"].get<std::string>();
            else if (j["documentation"].is_object() && j["documentation"].contains("value") && j["documentation"]["value"].is_string())
            {
                item.documentation = j["documentation"]["value"].get<std::string>();
            }
        }
        if (j.contains("textEdit") && j["textEdit"].is_object() && j["textEdit"].contains("newText") && j["textEdit"]["newText"].is_string())
        {
            item.insert_text = j["textEdit"]["newText"].get<std::string>();
        }
        else if (j.contains("insertText") && j["insertText"].is_string())
        {
            item.insert_text = j["insertText"].get<std::string>();
        }
        else
        {
            item.insert_text = item.label;
        }

        if (j.contains("labelDetails") && j["labelDetails"].is_object())
        {
            const auto& ld = j["labelDetails"];
            if (ld.contains("detail") && ld["detail"].is_string() && item.detail.empty())
            {
                item.detail = ld["detail"].get<std::string>();
            }
        }

        // Clean up clangd header insertion decorator bullet prefix (• / \xE2\x80\xA2) and leading whitespace
        auto strip_leading_bullet = [](std::string& s) {
            while (!s.empty())
            {
                if (s.starts_with("•"))
                {
                    s.erase(0, std::string("•").length());
                }
                else if (s.starts_with("\xE2\x80\xA2"))
                {
                    s.erase(0, 3);
                }
                else if (s.front() == ' ' || s.front() == '\t')
                {
                    s.erase(0, 1);
                }
                else
                {
                    break;
                }
            }
        };
        strip_leading_bullet(item.label);
        strip_leading_bullet(item.insert_text);

        if (j.contains("sortText") && j["sortText"].is_string()) item.sort_text = j["sortText"].get<std::string>();
        if (j.contains("filterText") && j["filterText"].is_string())
        {
            item.filter_text = j["filterText"].get<std::string>();
            strip_leading_bullet(item.filter_text);
        }
    }
    return item;
}

std::vector<CompletionItem> LspProtocolSerializer::parse_completion_list(const nlohmann::json& j)
{
    std::vector<CompletionItem> items;
    if (j.is_array())
    {
        for (const auto& item : j)
        {
            items.push_back(parse_completion_item(item));
        }
    }
    else if (j.is_object())
    {
        if (j.contains("items") && j["items"].is_array())
        {
            for (const auto& item : j["items"])
            {
                items.push_back(parse_completion_item(item));
            }
        }
    }
    return items;
}

Hover LspProtocolSerializer::parse_hover(const nlohmann::json& j)
{
    Hover hover;
    if (j.is_object())
    {
        if (j.contains("range")) hover.range = parse_range(j["range"]);
        if (j.contains("contents"))
        {
            if (j["contents"].is_string())
            {
                hover.contents = j["contents"].get<std::string>();
            }
            else if (j["contents"].is_object() && j["contents"].contains("value") && j["contents"]["value"].is_string())
            {
                hover.contents = j["contents"]["value"].get<std::string>();
            }
            else if (j["contents"].is_array())
            {
                for (const auto& part : j["contents"])
                {
                    if (part.is_string())
                    {
                        if (!hover.contents.empty()) hover.contents += "\n\n";
                        hover.contents += part.get<std::string>();
                    }
                    else if (part.is_object() && part.contains("value") && part["value"].is_string())
                    {
                        if (!hover.contents.empty()) hover.contents += "\n\n";
                        hover.contents += part["value"].get<std::string>();
                    }
                }
            }
        }
    }
    return hover;
}

SignatureHelp LspProtocolSerializer::parse_signature_help(const nlohmann::json& j)
{
    SignatureHelp help;
    if (j.is_object())
    {
        if (j.contains("activeSignature") && j["activeSignature"].is_number())
        {
            help.active_signature = j["activeSignature"].get<std::size_t>();
        }
        if (j.contains("activeParameter") && j["activeParameter"].is_number())
        {
            help.active_parameter = j["activeParameter"].get<std::size_t>();
        }
        if (j.contains("signatures") && j["signatures"].is_array())
        {
            for (const auto& sig_json : j["signatures"])
            {
                SignatureInformation sig;
                if (sig_json.contains("label") && sig_json["label"].is_string())
                {
                    sig.label = sig_json["label"].get<std::string>();
                }
                if (sig_json.contains("documentation") && sig_json["documentation"].is_string())
                {
                    sig.documentation = sig_json["documentation"].get<std::string>();
                }
                if (sig_json.contains("activeParameter") && sig_json["activeParameter"].is_number())
                {
                    sig.active_parameter = sig_json["activeParameter"].get<std::size_t>();
                }
                if (sig_json.contains("parameters") && sig_json["parameters"].is_array())
                {
                    for (const auto& param_json : sig_json["parameters"])
                    {
                        ParameterInformation param;
                        if (param_json.contains("label") && param_json["label"].is_string())
                        {
                            param.label = param_json["label"].get<std::string>();
                        }
                        if (param_json.contains("documentation") && param_json["documentation"].is_string())
                        {
                            param.documentation = param_json["documentation"].get<std::string>();
                        }
                        sig.parameters.push_back(std::move(param));
                    }
                }
                help.signatures.push_back(std::move(sig));
            }
        }
    }
    return help;
}

SemanticTokens LspProtocolSerializer::parse_semantic_tokens(const nlohmann::json& j)
{
    SemanticTokens tokens;
    if (j.is_object())
    {
        if (j.contains("resultId") && j["resultId"].is_string())
        {
            tokens.result_id = j["resultId"].get<std::string>();
        }
        if (j.contains("data") && j["data"].is_array())
        {
            tokens.data.reserve(j["data"].size());
            for (const auto& item : j["data"])
            {
                if (item.is_number())
                {
                    tokens.data.push_back(item.get<uint32_t>());
                }
            }
        }
    }
    return tokens;
}

} // namespace Zenvra::Language::Protocol
