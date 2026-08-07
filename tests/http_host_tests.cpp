#include "application/AuditService.hpp"
#include "application/TelegramApplicationService.hpp"
#include "application/TelegramPort.hpp"
#include "application/config/McpHostConfigurationRepository.hpp"
#include "domain/approval/ApprovalService.hpp"
#include "domain/policy/PolicyEngine.hpp"
#include "infrastructure/http/SimpleWebHttpHost.hpp"
#include "mcp/core/ToolRegistry.hpp"
#include "mcp/http/LocalMcpHttpService.hpp"

#include <client_http.hpp>

#include <cassert>
#include <filesystem>
#include <iostream>

#include <nlohmann/json.hpp>

namespace {

constexpr auto kProtocolVersion = "2026-07-28";

[[nodiscard]] tggate::domain::policy::McpClientProfile reader_profile() {
    return {
        .id = "reader-client", .display_name = "Reader", .enabled = true,
        .allowed_accounts = {"work"}, .allowed_tools = {"telegram_list_chats"}, .allowed_chats = {},
        .write_policy = tggate::domain::policy::WritePolicy::require_approval,
    };
}

[[nodiscard]] tggate::domain::policy::McpClientProfile writer_profile() {
    return {
        .id = "writer-client", .display_name = "Writer", .enabled = true,
        .allowed_accounts = {"work"}, .allowed_tools = {"telegram_prepare_send_message"}, .allowed_chats = {42},
        .write_policy = tggate::domain::policy::WritePolicy::require_approval,
    };
}

[[nodiscard]] nlohmann::json request(const std::string_view method, nlohmann::json params = nlohmann::json::object()) {
    params["_meta"]["io.modelcontextprotocol/protocolVersion"] = kProtocolVersion;
    return {{"jsonrpc", "2.0"}, {"id", 7}, {"method", method}, {"params", std::move(params)}};
}

[[nodiscard]] SimpleWeb::CaseInsensitiveMultimap headers(
    const std::string& authorization, const std::string_view method, const std::string_view origin = {}) {
    SimpleWeb::CaseInsensitiveMultimap result{
        {"Authorization", authorization}, {"Content-Type", "application/json"},
        {"MCP-Protocol-Version", kProtocolVersion}, {"Mcp-Method", std::string(method)},
    };
    if (!origin.empty()) result.emplace("Origin", std::string(origin));
    return result;
}

[[nodiscard]] int rpc_error_code(const std::shared_ptr<SimpleWeb::Client<SimpleWeb::HTTP>::Response>& response) {
    return nlohmann::json::parse(response->content.string()).at("error").at("code").get<int>();
}

} // namespace

int main() {
    using namespace tggate;

    infrastructure::http::SimpleWebHttpHost host;
    std::string error;
    assert(!host.start({.enabled = false}, {}, error));

    const auto root = std::filesystem::temp_directory_path() / "tggate-loopback-mcp-http-test";
    std::error_code filesystem_error;
    std::filesystem::remove_all(root, filesystem_error);
    std::filesystem::create_directories(root, filesystem_error);
    assert(!filesystem_error);
    const auto configuration_path = root / "mcp-host.json";

    application::config::McpHostConfiguration configuration{
        .enabled = true, .bind_address = "127.0.0.1", .port = 18766,
        .max_request_body_bytes = 1024, .maximum_connections = 2,
        .allowed_origins = {"https://allowed.example"},
    };
    assert(application::config::McpHostConfigurationRepository::save(configuration_path, configuration, error));

    application::UnavailableTelegramService telegram;
    domain::policy::PolicyEngine policy;
    domain::approval::ApprovalService approvals;
    application::AuditService audit;
    application::TelegramApplicationService telegram_service(telegram, policy, approvals, audit);
    mcp::core::ToolRegistry tools;
    tools.add({"telegram_list_chats", "List chats", mcp::core::ToolSurface::read, nlohmann::json::object()});
    tools.add({"telegram_prepare_send_message", "Prepare send", mcp::core::ToolSurface::write, nlohmann::json::object()});
    mcp::http::LocalMcpHttpService mcp_host(host, tools, telegram_service);
    assert(mcp_host.start(configuration_path, {reader_profile(), writer_profile()}, error));
    assert(mcp_host.running());

    application::config::McpHostConfiguration persisted;
    assert(application::config::McpHostConfigurationRepository::load(configuration_path, persisted, error));
    const auto reader_credential = application::config::McpHostConfigurationRepository::find_client_credential(persisted, "reader-client");
    assert(reader_credential);
    application::config::McpHostConfiguration legacy_configuration = persisted;
    legacy_configuration.legacy_bearer_token_dpapi = reader_credential->bearer_token_dpapi;
    legacy_configuration.client_credentials.clear();
    assert(application::config::McpHostConfigurationRepository::ensure_client_credentials(
        legacy_configuration, {reader_profile()}, error));
    const auto migrated_credential = application::config::McpHostConfigurationRepository::find_client_credential(legacy_configuration, "reader-client");
    assert(migrated_credential && migrated_credential->bearer_token_dpapi == reader_credential->bearer_token_dpapi);
    const auto token = application::config::McpHostConfigurationRepository::unprotect_bearer_token(reader_credential->bearer_token_dpapi);
    assert(token);
    const std::string authorization = "Bearer " + std::string(token->view());

    SimpleWeb::Client<SimpleWeb::HTTP> client("127.0.0.1:18766");
    const auto discover = request("server/discover").dump();
    const auto native_response = client.request("POST", "/mcp", discover, headers(authorization, "server/discover"));
    assert(native_response && native_response->status_code == "200 OK");
    const auto native_body = nlohmann::json::parse(native_response->content.string());
    assert(native_body.at("result").at("supportedVersions").at(0) == kProtocolVersion);

    const auto origin_response = client.request("POST", "/mcp", discover, headers(authorization, "server/discover", "https://allowed.example"));
    assert(origin_response && origin_response->status_code == "200 OK");
    const auto allowed_origin = origin_response->header.find("Access-Control-Allow-Origin");
    assert(allowed_origin != origin_response->header.end() && allowed_origin->second == "https://allowed.example");

    const auto preflight = client.request("OPTIONS", "/mcp", "", {{"Origin", "https://allowed.example"}});
    assert(preflight && preflight->status_code == "200 OK");
    const auto allowed_headers = preflight->header.find("Access-Control-Allow-Headers");
    assert(allowed_headers != preflight->header.end() && allowed_headers->second.find("Mcp-Name") != std::string::npos);
    const auto forbidden_preflight = client.request("OPTIONS", "/mcp", "", {{"Origin", "https://denied.example"}});
    assert(forbidden_preflight && forbidden_preflight->status_code == "403 Forbidden");

    const auto list_response = client.request("POST", "/mcp", request("tools/list").dump(),
        {{"Authorization", authorization}, {"Content-Type", "application/json"}, {"MCP-Protocol-Version", kProtocolVersion},
         {"Mcp-Method", "tools/list"}, {"X-TgGate-Client", "writer-client"}});
    assert(list_response && list_response->status_code == "200 OK");
    const auto listed = nlohmann::json::parse(list_response->content.string()).at("result").at("tools");
    assert(listed.size() == 1 && listed.at(0).at("name") == "telegram_list_chats");

    const auto unauthorized = client.request("POST", "/mcp", discover, headers("Bearer wrong", "server/discover"));
    assert(unauthorized && unauthorized->status_code == "401 Unauthorized");
    const auto forbidden_origin = client.request("POST", "/mcp", discover, headers(authorization, "server/discover", "https://denied.example"));
    assert(forbidden_origin && forbidden_origin->status_code == "403 Forbidden");

    auto missing_protocol_headers = headers(authorization, "server/discover");
    missing_protocol_headers.erase("MCP-Protocol-Version");
    const auto missing_protocol = client.request("POST", "/mcp", discover, missing_protocol_headers);
    assert(missing_protocol && missing_protocol->status_code == "400 Bad Request" && rpc_error_code(missing_protocol) == -32022);
    auto unsupported_protocol_headers = headers(authorization, "server/discover");
    unsupported_protocol_headers.find("MCP-Protocol-Version")->second = "2025-11-25";
    const auto unsupported_protocol = client.request("POST", "/mcp", discover, unsupported_protocol_headers);
    assert(unsupported_protocol && unsupported_protocol->status_code == "400 Bad Request" && rpc_error_code(unsupported_protocol) == -32022);

    const auto mismatched_method = client.request("POST", "/mcp", discover, headers(authorization, "tools/list"));
    assert(mismatched_method && mismatched_method->status_code == "400 Bad Request" && rpc_error_code(mismatched_method) == -32020);
    const auto missing_name = client.request("POST", "/mcp", request("tools/call", {{"name", "telegram_list_chats"}}).dump(),
        headers(authorization, "tools/call"));
    assert(missing_name && missing_name->status_code == "400 Bad Request" && rpc_error_code(missing_name) == -32020);
    auto wrong_name_headers = headers(authorization, "tools/call");
    wrong_name_headers.emplace("Mcp-Name", "telegram_get_messages");
    const auto mismatched_name = client.request("POST", "/mcp", request("tools/call", {{"name", "telegram_list_chats"}}).dump(), wrong_name_headers);
    assert(mismatched_name && mismatched_name->status_code == "400 Bad Request" && rpc_error_code(mismatched_name) == -32020);

    auto write_headers = headers(authorization, "tools/call");
    write_headers.emplace("Mcp-Name", "telegram_prepare_send_message");
    const auto reader_write = client.request("POST", "/mcp",
        request("tools/call", {{"name", "telegram_prepare_send_message"}, {"arguments", {{"account_id", "work"}, {"chat_id", 42}, {"text", "blocked"}}}}).dump(),
        write_headers);
    assert(reader_write && reader_write->status_code == "200 OK" && rpc_error_code(reader_write) == -32602);

    const auto batch = client.request("POST", "/mcp", "[]", headers(authorization, "server/discover"));
    assert(batch && batch->status_code == "400 Bad Request" && rpc_error_code(batch) == -32600);
    const auto invalid_json = client.request("POST", "/mcp", "{", headers(authorization, "server/discover"));
    assert(invalid_json && invalid_json->status_code == "400 Bad Request" && rpc_error_code(invalid_json) == -32700);
    const auto unknown = client.request("POST", "/mcp", request("no/such/method").dump(), headers(authorization, "no/such/method"));
    assert(unknown && unknown->status_code == "404 Not Found" && rpc_error_code(unknown) == -32601);

    mcp_host.stop();
    auto corrupt_configuration = persisted;
    corrupt_configuration.client_credentials.front().bearer_token_dpapi = "00";
    assert(!application::config::McpHostConfigurationRepository::ensure_client_credentials(
        corrupt_configuration, {reader_profile(), writer_profile()}, error));

    assert(application::config::McpHostConfigurationRepository::regenerate_client_credential(persisted, "reader-client", error));
    assert(application::config::McpHostConfigurationRepository::save(configuration_path, persisted, error));
    assert(mcp_host.start(configuration_path, {reader_profile(), writer_profile()}, error));
    const auto old_token_rejected = client.request("POST", "/mcp", discover, headers(authorization, "server/discover"));
    assert(old_token_rejected && old_token_rejected->status_code == "401 Unauthorized");
    const auto rotated_credential = application::config::McpHostConfigurationRepository::find_client_credential(persisted, "reader-client");
    assert(rotated_credential);
    const auto rotated_token = application::config::McpHostConfigurationRepository::unprotect_bearer_token(rotated_credential->bearer_token_dpapi);
    assert(rotated_token);
    const auto rotated_response = client.request("POST", "/mcp", discover,
        headers("Bearer " + std::string(rotated_token->view()), "server/discover"));
    assert(rotated_response && rotated_response->status_code == "200 OK");

    mcp_host.stop();
    std::filesystem::remove_all(root, filesystem_error);
    std::cout << "TgGate loopback MCP HTTP integration tests passed\n";
}
