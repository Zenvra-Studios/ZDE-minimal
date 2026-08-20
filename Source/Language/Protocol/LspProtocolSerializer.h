#pragma once

#include "Language/Protocol/LspMessage.h"
#include "Language/Protocol/LspTypes.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Language::Protocol
{

class LspProtocolSerializer
{
public:
    // URI Helpers
    [[nodiscard]] static std::string path_to_uri(const std::filesystem::path& path);
    [[nodiscard]] static std::filesystem::path uri_to_path(std::string_view uri);

    // Framing
    [[nodiscard]] static std::string frame_payload(std::string_view json_payload);
    [[nodiscard]] static std::string serialize_request(const JsonRpcRequest& req);
    [[nodiscard]] static std::string serialize_notification(const JsonRpcNotification& notif);
    [[nodiscard]] static std::string serialize_response(const JsonRpcResponse& res);

    // Types conversion
    [[nodiscard]] static Position parse_position(const nlohmann::json& j);
    [[nodiscard]] static nlohmann::json serialize_position(const Position& pos);

    [[nodiscard]] static Range parse_range(const nlohmann::json& j);
    [[nodiscard]] static nlohmann::json serialize_range(const Range& range);

    [[nodiscard]] static Location parse_location(const nlohmann::json& j);
    [[nodiscard]] static std::vector<Location> parse_locations(const nlohmann::json& j);

    [[nodiscard]] static Diagnostic parse_diagnostic(const nlohmann::json& j);
    [[nodiscard]] static std::vector<Diagnostic> parse_diagnostics(const nlohmann::json& j);

    [[nodiscard]] static CompletionItem parse_completion_item(const nlohmann::json& j);
    [[nodiscard]] static std::vector<CompletionItem> parse_completion_list(const nlohmann::json& j);

    [[nodiscard]] static Hover parse_hover(const nlohmann::json& j);
    [[nodiscard]] static SignatureHelp parse_signature_help(const nlohmann::json& j);
    [[nodiscard]] static SemanticTokens parse_semantic_tokens(const nlohmann::json& j);
};

} // namespace Zenvra::Language::Protocol
