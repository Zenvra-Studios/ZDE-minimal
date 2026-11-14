#include "Services/LSP/Client/LspClient.h"
#include "Services/LSP/Protocol/LspSerializer.h"

#include <iostream>

namespace Zenvra::Services::LSP::Client
{

LspClient::LspClient(
    std::string language_id,
    std::unique_ptr<Transport::ILspTransport> transport,
    std::filesystem::path workspace_root)
    : m_language_id(std::move(language_id))
    , m_transport(std::move(transport))
    , m_workspace_root(std::move(workspace_root))
    , m_lifecycle(m_language_id)
{
    if (m_transport)
    {
        m_transport->set_message_handler([this](std::string_view payload) {
            handle_incoming_message(payload);
        });
    }
}

LspClient::~LspClient()
{
    shutdown();
}

Lifecycle::LifecycleState LspClient::get_state() const noexcept
{
    return m_lifecycle.get_state();
}

bool LspClient::is_running() const noexcept
{
    return m_lifecycle.is_running();
}

bool LspClient::start()
{
    if (!m_transport) return false;

    m_lifecycle.transition_to(Lifecycle::LifecycleState::Starting);
    if (!m_transport->start())
    {
        m_lifecycle.transition_to(Lifecycle::LifecycleState::Failed, "Transport start failed");
        return false;
    }

    m_lifecycle.transition_to(Lifecycle::LifecycleState::Initializing);
    send_initialize();
    return true;
}

void LspClient::send_initialize()
{
    nlohmann::json params;
    params["processId"] = nullptr;
    if (!m_workspace_root.empty())
    {
        params["rootPath"] = m_workspace_root.string();
        params["rootUri"] = "file:///" + m_workspace_root.generic_string();
    }
    else
    {
        params["rootPath"] = nullptr;
        params["rootUri"] = nullptr;
    }

    params["capabilities"] = nlohmann::json::object({
        {"textDocument", {
            {"synchronization", {
                {"dynamicRegistration", false},
                {"willSave", false},
                {"willSaveWaitUntil", false},
                {"didSave", true}
            }},
            {"completion", {
                {"dynamicRegistration", false},
                {"completionItem", {
                    {"snippetSupport", true},
                    {"commitCharactersSupport", true},
                    {"documentationFormat", {"markdown", "plaintext"}},
                    {"deprecatedSupport", true},
                    {"preselectSupport", true}
                }}
            }},
            {"hover", {
                {"dynamicRegistration", false},
                {"contentFormat", {"markdown", "plaintext"}}
            }},
            {"signatureHelp", {
                {"dynamicRegistration", false},
                {"signatureInformation", {
                    {"documentationFormat", {"markdown", "plaintext"}}
                }}
            }},
            {"definition", {{"dynamicRegistration", false}}},
            {"declaration", {{"dynamicRegistration", false}}},
            {"typeDefinition", {{"dynamicRegistration", false}}},
            {"implementation", {{"dynamicRegistration", false}}},
            {"references", {{"dynamicRegistration", false}}},
            {"documentSymbol", {{"dynamicRegistration", false}}},
            {"formatting", {{"dynamicRegistration", false}}},
            {"rangeFormatting", {{"dynamicRegistration", false}}},
            {"rename", {{"dynamicRegistration", false}}},
            {"publishDiagnostics", {
                {"relatedInformation", true},
                {"versionSupport", true}
            }}
        }}
    });

    send_request("initialize", params, [this](const Protocol::JsonRpcResponse& resp) {
        if (!resp.is_error() && resp.result.is_object() && resp.result.contains("capabilities"))
        {
            m_capabilities = Capabilities::CapabilityMatrix::from_server_capabilities(resp.result["capabilities"]);
        }

        // Send 'initialized' notification
        send_notification("initialized", nlohmann::json::object());
        m_lifecycle.transition_to(Lifecycle::LifecycleState::Running);
    });
}

std::int64_t LspClient::send_request(std::string method, nlohmann::json params, ResponseCallback callback)
{
    const std::int64_t id = m_next_request_id.fetch_add(1);

    if (callback)
    {
        std::lock_guard<std::mutex> lock(m_callbacks_mutex);
        m_pending_callbacks[id] = std::move(callback);
    }

    Protocol::JsonRpcRequest req{
        .id = id,
        .method = std::move(method),
        .params = std::move(params)
    };

    if (m_transport)
    {
        m_transport->send(Protocol::LspSerializer::serialize_request(req));
    }
    return id;
}

void LspClient::send_notification(std::string method, nlohmann::json params)
{
    Protocol::JsonRpcNotification notif{
        .method = std::move(method),
        .params = std::move(params)
    };
    if (m_transport)
    {
        m_transport->send(Protocol::LspSerializer::serialize_notification(notif));
    }
}

void LspClient::handle_incoming_message(std::string_view payload)
{
    // Try parse response
    if (auto resp = Protocol::LspSerializer::parse_response(payload))
    {
        ResponseCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_callbacks_mutex);
            auto it = m_pending_callbacks.find(resp->id);
            if (it != m_pending_callbacks.end())
            {
                cb = std::move(it->second);
                m_pending_callbacks.erase(it);
            }
        }
        if (cb)
        {
            cb(*resp);
        }
        return;
    }

    // Try parse notification
    if (auto notif = Protocol::LspSerializer::parse_notification(payload))
    {
        if (notif->method == "textDocument/publishDiagnostics")
        {
            if (notif->params.contains("uri") && notif->params.contains("diagnostics"))
            {
                const std::string uri = notif->params["uri"].get<std::string>();
                if (m_diagnostics_callback)
                {
                    m_diagnostics_callback(uri, notif->params["diagnostics"]);
                }
            }
        }
    }
}

void LspClient::did_open(const std::string& uri, std::string_view language_id, std::int32_t version, std::string_view text)
{
    nlohmann::json params;
    params["textDocument"] = {
        {"uri", uri},
        {"languageId", language_id},
        {"version", version},
        {"text", text}
    };
    send_notification("textDocument/didOpen", params);
}

void LspClient::did_change(const std::string& uri, std::int32_t version, std::string_view text)
{
    nlohmann::json params;
    params["textDocument"] = {
        {"uri", uri},
        {"version", version}
    };
    params["contentChanges"] = nlohmann::json::array({{{"text", text}}});
    send_notification("textDocument/didChange", params);
}

void LspClient::did_save(const std::string& uri)
{
    nlohmann::json params;
    params["textDocument"] = {{"uri", uri}};
    send_notification("textDocument/didSave", params);
}

void LspClient::did_close(const std::string& uri)
{
    nlohmann::json params;
    params["textDocument"] = {{"uri", uri}};
    send_notification("textDocument/didClose", params);
}

void LspClient::set_diagnostics_callback(DiagnosticsCallback cb)
{
    m_diagnostics_callback = std::move(cb);
}

void LspClient::shutdown()
{
    if (m_lifecycle.is_running())
    {
        m_lifecycle.transition_to(Lifecycle::LifecycleState::Stopping);
        send_request("shutdown", nlohmann::json::object(), [this](const Protocol::JsonRpcResponse&) {
            exit();
        });
    }
}

void LspClient::exit()
{
    send_notification("exit", nlohmann::json::object());
    if (m_transport)
    {
        m_transport->stop();
    }
    m_lifecycle.transition_to(Lifecycle::LifecycleState::Uninitialized);
}

} // namespace Zenvra::Services::LSP::Client

