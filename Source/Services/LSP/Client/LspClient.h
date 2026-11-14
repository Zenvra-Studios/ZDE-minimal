#pragma once

#include "Services/LSP/Capabilities/CapabilityMatrix.h"
#include "Services/LSP/Lifecycle/LifecycleManager.h"
#include "Services/LSP/Protocol/LspMessages.h"
#include "Services/LSP/Transport/ILspTransport.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::LSP::Client
{

class LspClient
{
public:
    LspClient(
        std::string language_id,
        std::unique_ptr<Transport::ILspTransport> transport,
        std::filesystem::path workspace_root = {});
    ~LspClient();

    using ResponseCallback = std::function<void(const Protocol::JsonRpcResponse&)>;
    using DiagnosticsCallback = std::function<void(const std::string& uri, const nlohmann::json& diagnostics)>;

    bool start();
    void shutdown();
    void exit();

    [[nodiscard]] Lifecycle::LifecycleState get_state() const noexcept;
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] const Capabilities::CapabilityMatrix& get_capabilities() const noexcept { return m_capabilities; }
    [[nodiscard]] const std::string& get_language_id() const noexcept { return m_language_id; }
    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept { return m_workspace_root; }

    // Core Request Sending
    std::int64_t send_request(std::string method, nlohmann::json params, ResponseCallback callback);
    void send_notification(std::string method, nlohmann::json params);

    // Document Synchronization
    void did_open(const std::string& uri, std::string_view language_id, std::int32_t version, std::string_view text);
    void did_change(const std::string& uri, std::int32_t version, std::string_view text);
    void did_save(const std::string& uri);
    void did_close(const std::string& uri);

    void set_diagnostics_callback(DiagnosticsCallback cb);

private:
    void handle_incoming_message(std::string_view payload);
    void send_initialize();

    std::string m_language_id;
    std::unique_ptr<Transport::ILspTransport> m_transport;
    std::filesystem::path m_workspace_root;
    Lifecycle::LifecycleManager m_lifecycle;
    Capabilities::CapabilityMatrix m_capabilities;

    std::atomic<std::int64_t> m_next_request_id{1};
    mutable std::mutex m_callbacks_mutex;
    std::unordered_map<std::int64_t, ResponseCallback> m_pending_callbacks;
    DiagnosticsCallback m_diagnostics_callback;
};

} // namespace Zenvra::Services::LSP::Client

