#include "mcp/v2025_11_25/ProtocolHandler.hpp"

namespace tggate::mcp::v2025_11_25 {
namespace {

constexpr auto kProtocolVersion = "2025-11-25";

} // namespace

ProtocolHandler::ProtocolHandler(
    const core::ToolRegistry& tools,
    application::TelegramApplicationService& telegram_service,
    const domain::policy::McpClientProfile& profile,
    const core::ToolSurface surface)
    : tools_(tools), telegram_service_(telegram_service), profile_(profile), surface_(surface) {}

nlohmann::json ProtocolHandler::result(const nlohmann::json& id, nlohmann::json value) const {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(value)}};
}

nlohmann::json ProtocolHandler::rpc_error(
    const nlohmann::json& id,
    const int code,
    const std::string_view message) const {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json ProtocolHandler::handle(const nlohmann::json& request) {
    const nlohmann::json id = request.value("id", nlohmann::json(nullptr));
    if (request.value("jsonrpc", "") != "2.0" || !request.contains("method")) {
        return rpc_error(id, -32600, "Invalid JSON-RPC request");
    }

    const auto method = request.at("method").get<std::string>();
    if (method == "initialize") {
        return result(id, {{"protocolVersion", kProtocolVersion}, {"serverInfo", {{"name", "TgGate"}, {"version", "0.1.0"}}},
            {"capabilities", {{"tools", nlohmann::json::object()}}}});
    }
    if (method == "tools/list") {
        return result(id, {{"tools", tools_.list(surface_)}});
    }
    if (method == "tools/call") {
        return handle_tool_call(request);
    }
    return rpc_error(id, -32601, "Method not found");
}

nlohmann::json ProtocolHandler::handle_tool_call(const nlohmann::json& request) {
    const nlohmann::json id = request.value("id", nlohmann::json(nullptr));
    const auto& params = request.value("params", nlohmann::json::object());
    if (!params.contains("name") || !params.at("name").is_string()) {
        return rpc_error(id, -32602, "tools/call requires a string name");
    }
    const auto name = params.at("name").get<std::string>();
    if (!tools_.find(name, surface_)) {
        // A read endpoint must not reveal write tools, and vice versa.
        return rpc_error(id, -32602, "Tool is unavailable on this endpoint");
    }
    const auto& arguments = params.value("arguments", nlohmann::json::object());
    nlohmann::json response;
    try {
        if (name == "telegram_list_chats") {
            response = telegram_service_.list_chats(profile_, arguments.at("account_id").get<std::string>());
        } else if (name == "telegram_get_messages") {
            response = telegram_service_.get_messages(profile_, arguments.at("account_id").get<std::string>(),
                arguments.at("chat_id").get<std::int64_t>(), arguments.value("limit", 50U));
        } else if (name == "telegram_prepare_send_message") {
            response = telegram_service_.prepare_send_message(profile_, arguments.at("account_id").get<std::string>(),
                arguments.at("chat_id").get<std::int64_t>(), arguments.at("text").get<std::string>());
        } else if (name == "telegram_execute_approved_action") {
            response = telegram_service_.execute_approved_action(profile_, arguments.at("action_id").get<std::string>());
        } else {
            return rpc_error(id, -32601, "Tool implementation is not available");
        }
    } catch (const nlohmann::json::exception&) {
        return rpc_error(id, -32602, "Tool arguments do not match the schema");
    }

    const auto serialized = response.dump();
    return result(id, {{"content", {{{"type", "text"}, {"text", serialized}}}}, {"isError", !response.value("ok", false)}});
}

} // namespace tggate::mcp::v2025_11_25
