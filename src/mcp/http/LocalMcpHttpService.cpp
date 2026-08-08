#include "mcp/http/LocalMcpHttpService.hpp"

#include "mcp/v2026_07_28/ProtocolHandler.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include <nlohmann/json.hpp>

namespace tggate::mcp::http {
namespace {

constexpr std::string_view kMcpPath = "/mcp";

[[nodiscard]] bool ascii_equal_ignore_case(const std::string_view lhs, const std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto left = static_cast<unsigned char>(lhs[index]);
        const auto right = static_cast<unsigned char>(rhs[index]);
        if (std::tolower(left) != std::tolower(right)) return false;
    }
    return true;
}

[[nodiscard]] std::string_view header(const application::http::HttpRequest& request, const std::string_view name) noexcept {
    const auto found = std::find_if(request.headers.begin(), request.headers.end(), [name](const auto& value) {
        return ascii_equal_ignore_case(value.first, name);
    });
    return found == request.headers.end() ? std::string_view{} : std::string_view(found->second);
}

[[nodiscard]] bool matches_bearer_token(
    const std::string_view authorization,
    const application::security::SecretBuffer& expected) noexcept {
    constexpr std::string_view prefix = "Bearer ";
    if (authorization.size() < prefix.size() || !ascii_equal_ignore_case(authorization.substr(0, prefix.size()), prefix)) {
        return false;
    }
    const auto supplied = authorization.substr(prefix.size());
    if (supplied.size() != expected.size()) return false;
    unsigned char difference = 0;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        difference = static_cast<unsigned char>(difference | (static_cast<unsigned char>(supplied[index]) ^ expected.data()[index]));
    }
    return difference == 0;
}

[[nodiscard]] application::http::HttpResponse response(
    const int status,
    nlohmann::json body = nlohmann::json()) {
    application::http::HttpResponse result{.status = status};
    if (!body.is_null()) {
        result.headers.emplace("Content-Type", "application/json");
        result.body = body.dump();
    }
    return result;
}

[[nodiscard]] application::http::HttpResponse rpc_error(
    const int status,
    const nlohmann::json& id,
    const int code,
    const std::string_view message,
    nlohmann::json data = nullptr) {
    nlohmann::json error{{"code", code}, {"message", message}};
    if (!data.is_null()) error["data"] = std::move(data);
    return response(status, {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}});
}

[[nodiscard]] bool valid_request_id(const nlohmann::json& value) {
    return value.is_null() || value.is_string() || value.is_number();
}

[[nodiscard]] bool has_required_client_metadata(const nlohmann::json& metadata) {
    const auto client_info = metadata.find("io.modelcontextprotocol/clientInfo");
    const auto capabilities = metadata.find("io.modelcontextprotocol/clientCapabilities");
    return client_info != metadata.end() && client_info->is_object() &&
        client_info->contains("name") && client_info->at("name").is_string() && !client_info->at("name").get_ref<const std::string&>().empty() &&
        client_info->contains("version") && client_info->at("version").is_string() && !client_info->at("version").get_ref<const std::string&>().empty() &&
        capabilities != metadata.end() && capabilities->is_object();
}

[[nodiscard]] std::string_view trim_ascii_whitespace(std::string_view value) noexcept {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
    return value;
}

[[nodiscard]] bool content_type_is_json(const std::string_view content_type) noexcept {
    const auto separator = content_type.find(';');
    return ascii_equal_ignore_case(trim_ascii_whitespace(content_type.substr(0, separator)), "application/json");
}

[[nodiscard]] bool accepts_media_type(const std::string_view accept, const std::string_view expected) noexcept {
    std::size_t offset = 0;
    while (offset <= accept.size()) {
        const auto delimiter = accept.find(',', offset);
        auto item = trim_ascii_whitespace(accept.substr(offset, delimiter == std::string_view::npos ? delimiter : delimiter - offset));
        const auto parameters = item.find(';');
        item = trim_ascii_whitespace(item.substr(0, parameters));
        if (ascii_equal_ignore_case(item, expected)) return true;
        if (delimiter == std::string_view::npos) break;
        offset = delimiter + 1;
    }
    return false;
}

} // namespace

LocalMcpHttpService::LocalMcpHttpService(
    application::http::IHttpHost& host,
    const core::ToolRegistry& tools,
    application::TelegramApplicationService& telegram_service)
    : host_(host), tools_(tools), telegram_service_(telegram_service) {}

LocalMcpHttpService::~LocalMcpHttpService() {
    stop();
}

bool LocalMcpHttpService::start(
    const std::filesystem::path& configuration_path,
    std::vector<domain::policy::McpClientProfile> profiles,
    std::string& error) {
    stop();
    application::config::McpHostConfiguration configuration;
    if (!application::config::McpHostConfigurationRepository::load(configuration_path, configuration, error)) {
        status_ = error;
        return false;
    }
    if (!configuration.enabled) {
        status_ = "MCP host is disabled by runtime configuration";
        return true;
    }
    if (profiles.empty()) {
        error = "MCP host requires at least one configured client profile";
        status_ = error;
        return false;
    }

    const auto credential_count = configuration.client_credentials.size();
    if (!application::config::McpHostConfigurationRepository::ensure_client_credentials(configuration, profiles, error)) {
        status_ = error;
        return false;
    }
    if (configuration.client_credentials.size() != credential_count &&
        !application::config::McpHostConfigurationRepository::save(configuration_path, configuration, error)) {
        status_ = error;
        return false;
    }

    std::vector<AuthenticatedClient> clients;
    clients.reserve(profiles.size());
    for (auto& profile : profiles) {
        const auto credential = application::config::McpHostConfigurationRepository::find_client_credential(configuration, profile.id);
        if (!credential) {
            error = "MCP client profile has no bearer credential";
            status_ = error;
            return false;
        }
        auto token = application::config::McpHostConfigurationRepository::unprotect_bearer_token(credential->bearer_token_dpapi);
        if (!token) {
            error = "MCP bearer token cannot be decrypted for this Windows user and computer";
            status_ = error;
            return false;
        }
        clients.push_back({.profile = std::move(profile), .bearer_token = std::move(*token)});
    }

    configuration_ = std::move(configuration);
    clients_ = std::move(clients);
    std::string host_error;
    if (!host_.start({
            .enabled = configuration_.enabled,
            .bind_address = configuration_.bind_address,
            .port = configuration_.port,
            .max_request_body_bytes = configuration_.max_request_body_bytes,
            .maximum_connections = configuration_.maximum_connections,
        },
        [this](const application::http::HttpRequest& request) { return handle(request); }, host_error)) {
        error = host_error;
        status_ = error;
        clients_.clear();
        return false;
    }

    running_ = true;
    status_ = "MCP host is listening on " + configuration_.bind_address + ":" + std::to_string(configuration_.port);
    return true;
}

void LocalMcpHttpService::stop() noexcept {
    host_.stop();
    clients_.clear();
    configuration_ = {};
    running_ = false;
    if (status_.rfind("MCP host is listening", 0) == 0) status_ = "MCP host is not started";
}

bool LocalMcpHttpService::running() const noexcept {
    return running_;
}

const std::string& LocalMcpHttpService::status() const noexcept {
    return status_;
}

application::http::HttpResponse LocalMcpHttpService::handle(const application::http::HttpRequest& request) {
    if (request.path != kMcpPath) return response(404);
    const auto origin = header(request, "Origin");
    if (request.method == "OPTIONS") {
        if (origin.empty() || std::find(configuration_.allowed_origins.begin(), configuration_.allowed_origins.end(), origin) == configuration_.allowed_origins.end()) {
            return response(403);
        }
        auto preflight = response(200);
        preflight.headers.emplace("Access-Control-Allow-Origin", std::string(origin));
        preflight.headers.emplace("Access-Control-Allow-Methods", "POST");
        preflight.headers.emplace("Access-Control-Allow-Headers", "Authorization, Content-Type, Accept, MCP-Protocol-Version, Mcp-Method, Mcp-Name");
        return preflight;
    }
    if (request.method != "POST") return response(405);
    if (!origin.empty() && std::find(configuration_.allowed_origins.begin(), configuration_.allowed_origins.end(), origin) == configuration_.allowed_origins.end()) {
        return response(403);
    }

    const auto authorization = header(request, "Authorization");
    const auto client = std::find_if(clients_.begin(), clients_.end(), [authorization](const auto& value) {
        return value.profile.enabled && matches_bearer_token(authorization, value.bearer_token);
    });
    if (client == clients_.end()) return response(401);
    if (!content_type_is_json(header(request, "Content-Type"))) return response(415);
    const auto accept = header(request, "Accept");
    if (!accepts_media_type(accept, "application/json") || !accepts_media_type(accept, "text/event-stream")) return response(406);

    try {
        const auto request_json = nlohmann::json::parse(request.body);
        const nlohmann::json id = request_json.is_object() && request_json.contains("id") ? request_json.at("id") : nlohmann::json(nullptr);
        if (!request_json.is_object() || request_json.contains("id") && !valid_request_id(request_json.at("id")) ||
            request_json.value("jsonrpc", "") != "2.0" || !request_json.contains("method") || !request_json.at("method").is_string()) {
            return rpc_error(400, id, -32600, "Invalid JSON-RPC request");
        }
        const auto method = request_json.at("method").get<std::string>();
        const auto protocol_version = header(request, "MCP-Protocol-Version");
        if (protocol_version.empty()) {
            return rpc_error(400, id, -32020, "MCP-Protocol-Version is required");
        }
        if (protocol_version != v2026_07_28::kProtocolVersion) {
            return rpc_error(400, id, -32022, "Unsupported protocol version",
                {{"supported", {v2026_07_28::kProtocolVersion}}, {"requested", protocol_version}});
        }
        if (!request_json.contains("params") || !request_json.at("params").is_object() ||
            !request_json.at("params").contains("_meta") || !request_json.at("params").at("_meta").is_object() ||
            request_json.at("params").at("_meta").value("io.modelcontextprotocol/protocolVersion", "") != protocol_version) {
            return rpc_error(400, id, -32020, "MCP protocol metadata does not match the request");
        }
        if (!has_required_client_metadata(request_json.at("params").at("_meta"))) {
            return rpc_error(400, id, -32602, "MCP request metadata requires clientInfo and clientCapabilities");
        }
        if (header(request, "Mcp-Method") != method) {
            return rpc_error(400, id, -32020, "Mcp-Method does not match the JSON-RPC method");
        }
        if (v2026_07_28::ProtocolHandler::requires_name(method)) {
            const auto name = header(request, "Mcp-Name");
            if (name.empty() || !request_json.at("params").contains("name") || !request_json.at("params").at("name").is_string() ||
                name != request_json.at("params").at("name").get<std::string>()) {
                return rpc_error(400, id, -32020, "Mcp-Name does not match the JSON-RPC request");
            }
        }
        if (!v2026_07_28::ProtocolHandler::supports_method(method)) {
            return rpc_error(404, id, -32601, "Method not found");
        }

        v2026_07_28::ProtocolHandler handler(tools_, telegram_service_, client->profile);
        auto result = response(200, handler.handle(request_json));
        if (!origin.empty()) result.headers.emplace("Access-Control-Allow-Origin", std::string(origin));
        return result;
    } catch (const nlohmann::json::parse_error&) {
        return rpc_error(400, nullptr, -32700, "Parse error");
    } catch (const nlohmann::json::exception&) {
        return rpc_error(400, nullptr, -32600, "Invalid JSON-RPC request");
    }
}

} // namespace tggate::mcp::http
