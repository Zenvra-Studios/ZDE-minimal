#include "Language/Client/LanguageClient.h"
#include "Language/Protocol/LspProtocolSerializer.h"

#include <iostream>

namespace Zenvra::Language::Client
{

LanguageClient::LanguageClient(
    std::string language_id,
    std::unique_ptr<Transport::ILspTransport> transport,
    std::filesystem::path workspace_root)
    : m_language_id(std::move(language_id))
    , m_transport(std::move(transport))
    , m_workspace_root(std::move(workspace_root))
{
}

LanguageClient::~LanguageClient()
{
    shutdown();
    exit();
}

void LanguageClient::set_initialization_options(nlohmann::json options)
{
    m_initialization_options = std::move(options);
}

bool LanguageClient::start()
{
    if (m_state.load() == ClientState::Active || m_state.load() == ClientState::Initializing)
    {
        return true;
    }

    if (!m_transport)
    {
        return false;
    }

    m_state.store(ClientState::Initializing);
    m_transport->set_message_handler([this](std::string_view msg) {
        on_message_received(msg);
    });

    if (!m_transport->start())
    {
        m_state.store(ClientState::Error);
        return false;
    }

    // Prepare Initialize Params
    std::string root_uri;
    if (!m_workspace_root.empty())
    {
        root_uri = Protocol::LspProtocolSerializer::path_to_uri(m_workspace_root);
        if (!root_uri.empty() && root_uri.back() == '/')
        {
            root_uri.pop_back();
        }
    }

    nlohmann::json init_params = {
        {"processId", nullptr},
        {"rootUri", root_uri.empty() ? nullptr : nlohmann::json(root_uri)},
        {"workspaceFolders", root_uri.empty() ? nlohmann::json::array() : nlohmann::json::array({{{"uri", root_uri}, {"name", "workspace"}}})},
        {"capabilities", {
            {"textDocument", {
                {"synchronization", {
                    {"dynamicRegistration", true},
                    {"willSave", true},
                    {"willSaveWaitUntil", false},
                    {"didSave", true}
                }},
                {"publishDiagnostics", {
                    {"relatedInformation", true},
                    {"versionSupport", true},
                    {"tagSupport", {
                        {"valueSet", nlohmann::json::array({1, 2})}
                    }},
                    {"codeDescriptionSupport", true},
                    {"dataSupport", true}
                }},
                {"completion", {
                    {"dynamicRegistration", true},
                    {"completionItem", {
                        {"snippetSupport", true},
                        {"documentationFormat", nlohmann::json::array({"markdown", "plaintext"})},
                        {"labelDetailsSupport", true},
                        {"insertReplaceSupport", true},
                        {"resolveSupport", {{"properties", nlohmann::json::array({"documentation", "detail", "additionalTextEdits"})}}}
                    }},
                    {"contextSupport", true}
                }},
                {"hover", {
                    {"contentFormat", nlohmann::json::array({"markdown", "plaintext"})}
                }},
                {"definition", {
                    {"dynamicRegistration", true},
                    {"linkSupport", true}
                }},
                {"signatureHelp", {
                    {"signatureInformation", {
                        {"documentationFormat", nlohmann::json::array({"markdown", "plaintext"})}
                    }}
                }},
                {"semanticTokens", {
                    {"dynamicRegistration", true},
                    {"requests", {{"full", true}, {"range", true}}},
                    {"tokenTypes", nlohmann::json::array({
                        "namespace", "type", "class", "enum", "interface", "struct", "typeParameter",
                        "parameter", "variable", "property", "enumMember", "event",
                        "function", "method", "macro", "keyword", "modifier",
                        "comment", "string", "number", "regexp", "operator", "custom"
                    })},
                    {"tokenModifiers", nlohmann::json::array({
                        "declaration", "definition", "readonly", "static", "deprecated",
                        "abstract", "async", "modification", "documentation", "defaultLibrary"
                    })},
                    {"formats", nlohmann::json::array({"relative"})}
                }}
            }}
        }}
    };

    if (!m_initialization_options.is_null())
    {
        init_params["initializationOptions"] = m_initialization_options;
    }

    send_request("initialize", init_params, [this](const nlohmann::json& result) {
        if (result.is_object() && result.contains("capabilities"))
        {
            const auto& caps = result["capabilities"];
            if (caps.contains("semanticTokensProvider") && caps["semanticTokensProvider"].is_object())
            {
                const auto& stp = caps["semanticTokensProvider"];
                if (stp.contains("legend") && stp["legend"].contains("tokenTypes") && stp["legend"]["tokenTypes"].is_array())
                {
                    std::vector<std::string> legend;
                    for (const auto& t : stp["legend"]["tokenTypes"])
                    {
                        if (t.is_string())
                        {
                            legend.push_back(t.get<std::string>());
                        }
                    }
                    m_semantic_token_legend = std::move(legend);
                }
            }
        }

        m_state.store(ClientState::Initialized);
        send_notification("initialized", nlohmann::json::object());
        m_state.store(ClientState::Active);

        std::vector<std::string> pending_packets;
        {
            std::lock_guard<std::mutex> lock(m_request_mutex);
            pending_packets.swap(m_queued_packets);
        }
        for (const auto& pkt : pending_packets)
        {
            if (m_transport)
            {
                m_transport->send(pkt);
            }
        }
    });

    return true;
}

void LanguageClient::shutdown()
{
    if (m_state.load() == ClientState::Active || m_state.load() == ClientState::Initialized)
    {
        send_request("shutdown", nlohmann::json::object(), [this](const nlohmann::json& /*res*/) {
            m_state.store(ClientState::Shutdown);
        });
    }
}

void LanguageClient::exit()
{
    if (m_state.load() != ClientState::Exit)
    {
        send_notification("exit", nlohmann::json::object());
        if (m_transport)
        {
            m_transport->stop();
        }
        m_state.store(ClientState::Exit);
    }
}

ClientState LanguageClient::get_state() const noexcept
{
    return m_state.load();
}

bool LanguageClient::is_active() const noexcept
{
    return m_state.load() == ClientState::Active;
}

bool LanguageClient::is_document_open(const std::string& uri) const noexcept
{
    return m_sync_manager.is_tracked(uri);
}

void LanguageClient::did_open(const std::string& uri, std::string_view language_id, int version, std::string_view text)
{
    if (m_sync_manager.is_tracked(uri))
    {
        return;
    }
    const auto notif = m_sync_manager.create_did_open(uri, language_id, version, text);
    send_notification(notif.method, notif.params);
}

void LanguageClient::did_change(const std::string& uri, int version, std::string_view text)
{
    const auto notif = m_sync_manager.create_did_change(uri, version, text);
    send_notification(notif.method, notif.params);
}

void LanguageClient::did_save(const std::string& uri)
{
    const auto notif = m_sync_manager.create_did_save(uri);
    send_notification(notif.method, notif.params);
}

void LanguageClient::did_close(const std::string& uri)
{
    const auto notif = m_sync_manager.create_did_close(uri);
    send_notification(notif.method, notif.params);
}

void LanguageClient::request_completion(
    const std::string& uri,
    const Protocol::Position& pos,
    std::function<void(std::vector<Protocol::CompletionItem>)> callback,
    std::optional<char> trigger_character)
{
    nlohmann::json context_json = nlohmann::json::object();
    if (trigger_character.has_value())
    {
        context_json["triggerKind"] = 2; // TriggerCharacter
        context_json["triggerCharacter"] = std::string(1, *trigger_character);
    }
    else
    {
        context_json["triggerKind"] = 1; // Invoked
    }

    nlohmann::json params = {
        {"textDocument", {{"uri", uri}}},
        {"position", Protocol::LspProtocolSerializer::serialize_position(pos)},
        {"context", context_json}
    };

    send_request("textDocument/completion", params, [callback = std::move(callback)](const nlohmann::json& res) {
        if (callback)
        {
            callback(Protocol::LspProtocolSerializer::parse_completion_list(res));
        }
    });
}

void LanguageClient::request_hover(
    const std::string& uri,
    const Protocol::Position& pos,
    std::function<void(std::optional<Protocol::Hover>)> callback)
{
    nlohmann::json params = {
        {"textDocument", {{"uri", uri}}},
        {"position", Protocol::LspProtocolSerializer::serialize_position(pos)}
    };

    send_request("textDocument/hover", params, [callback = std::move(callback)](const nlohmann::json& res) {
        if (callback)
        {
            if (res.is_null())
            {
                callback(std::nullopt);
            }
            else
            {
                callback(Protocol::LspProtocolSerializer::parse_hover(res));
            }
        }
    });
}

void LanguageClient::request_definition(
    const std::string& uri,
    const Protocol::Position& pos,
    std::function<void(std::vector<Protocol::Location>)> callback)
{
    nlohmann::json params = {
        {"textDocument", {{"uri", uri}}},
        {"position", Protocol::LspProtocolSerializer::serialize_position(pos)}
    };

    send_request("textDocument/definition", params, [callback = std::move(callback)](const nlohmann::json& res) {
        if (callback)
        {
            callback(Protocol::LspProtocolSerializer::parse_locations(res));
        }
    });
}

void LanguageClient::request_signature_help(
    const std::string& uri,
    const Protocol::Position& pos,
    std::function<void(std::optional<Protocol::SignatureHelp>)> callback)
{
    nlohmann::json params = {
        {"textDocument", {{"uri", uri}}},
        {"position", Protocol::LspProtocolSerializer::serialize_position(pos)}
    };

    send_request("textDocument/signatureHelp", params, [callback = std::move(callback)](const nlohmann::json& res) {
        if (callback)
        {
            if (res.is_null())
            {
                callback(std::nullopt);
            }
            else
            {
                callback(Protocol::LspProtocolSerializer::parse_signature_help(res));
            }
        }
    });
}

void LanguageClient::request_semantic_tokens(
    const std::string& uri,
    std::function<void(std::optional<Protocol::SemanticTokens>)> callback)
{
    nlohmann::json params = {
        {"textDocument", {{"uri", uri}}}
    };

    send_request("textDocument/semanticTokens/full", params, [callback = std::move(callback)](const nlohmann::json& res) {
        if (callback)
        {
            if (res.is_null())
            {
                callback(std::nullopt);
            }
            else
            {
                callback(Protocol::LspProtocolSerializer::parse_semantic_tokens(res));
            }
        }
    });
}

void LanguageClient::set_diagnostics_handler(
    std::function<void(const std::string&, const std::vector<Protocol::Diagnostic>&)> handler)
{
    m_diagnostics_handler = std::move(handler);
}

void LanguageClient::on_message_received(std::string_view json_string)
{
    try
    {
        const auto root = nlohmann::json::parse(json_string);

        // 1. Check if it's a notification
        if (root.contains("method") && root["method"].is_string())
        {
            const std::string method = root["method"].get<std::string>();
            if (method == "textDocument/publishDiagnostics" && root.contains("params"))
            {
                const auto& params = root["params"];
                if (params.contains("uri") && params["uri"].is_string() && params.contains("diagnostics"))
                {
                    const std::string uri = params["uri"].get<std::string>();
                    const auto diags = Protocol::LspProtocolSerializer::parse_diagnostics(params["diagnostics"]);
                    if (m_diagnostics_handler)
                    {
                        m_diagnostics_handler(uri, diags);
                    }
                }
            }
            return;
        }

        // 2. Check if it's a response to a pending request
        if (root.contains("id") && root["id"].is_number())
        {
            const int64_t req_id = root["id"].get<int64_t>();
            std::function<void(const nlohmann::json&)> cb;
            {
                std::lock_guard<std::mutex> lock(m_request_mutex);
                if (auto it = m_pending_requests.find(req_id); it != m_pending_requests.end())
                {
                    cb = std::move(it->second);
                    m_pending_requests.erase(it);
                }
            }

            if (cb)
            {
                if (root.contains("result"))
                {
                    cb(root["result"]);
                }
                else if (root.contains("error"))
                {
                    cb(nullptr);
                }
            }
        }
    }
    catch (...)
    {
        // JSON parsing error ignored for malformed packets
    }
}

int64_t LanguageClient::send_request(
    std::string_view method,
    nlohmann::json params,
    std::function<void(const nlohmann::json&)> callback)
{
    int64_t req_id = 0;
    {
        std::lock_guard<std::mutex> lock(m_request_mutex);
        req_id = m_next_request_id++;
        if (callback)
        {
            m_pending_requests[req_id] = std::move(callback);
        }
    }

    Protocol::JsonRpcRequest req{
        .id = req_id,
        .method = std::string(method),
        .params = std::move(params)
    };

    const std::string packet = Protocol::LspProtocolSerializer::serialize_request(req);
    if (m_state.load() == ClientState::Initializing && method != "initialize")
    {
        std::lock_guard<std::mutex> lock(m_request_mutex);
        m_queued_packets.push_back(packet);
    }
    else if (m_transport)
    {
        m_transport->send(packet);
    }

    return req_id;
}

void LanguageClient::send_notification(std::string_view method, nlohmann::json params)
{
    Protocol::JsonRpcNotification notif{
        .method = std::string(method),
        .params = std::move(params)
    };

    const std::string packet = Protocol::LspProtocolSerializer::serialize_notification(notif);
    if (m_state.load() == ClientState::Initializing && method != "initialized")
    {
        std::lock_guard<std::mutex> lock(m_request_mutex);
        m_queued_packets.push_back(packet);
    }
    else if (m_transport)
    {
        m_transport->send(packet);
    }
}

} // namespace Zenvra::Language::Client
