#include "mcp/v2026_07_28/ProtocolHandler.hpp"
#include "mcp/core/InputSchemaValidator.hpp"

namespace tggate::mcp::v2026_07_28 {

ProtocolHandler::ProtocolHandler(
    const core::ToolRegistry& tools,
    application::TelegramApplicationService& telegram_service,
    const domain::policy::McpClientProfile& profile)
    : tools_(tools), telegram_service_(telegram_service), profile_(profile) {}

bool ProtocolHandler::supports_method(const std::string_view method) noexcept {
    return method == "server/discover" || method == "tools/list" || method == "tools/call";
}

bool ProtocolHandler::requires_name(const std::string_view method) noexcept {
    return method == "tools/call";
}

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
    const auto method = request.at("method").get<std::string>();
    if (method == "server/discover") {
        return result(id, {{"resultType", "complete"}, {"supportedVersions", {kProtocolVersion}},
            {"capabilities", {{"tools", nlohmann::json::object()}}},
            {"_meta", {{"io.modelcontextprotocol/serverInfo", {{"name", "TgGate"}, {"version", "0.1.0"}}}}},
            {"instructions", "TgGate exposes policy-controlled local Telegram tools."},
            {"ttlMs", 3600000}, {"cacheScope", "public"}});
    }
    if (method == "tools/list") {
        auto tools = nlohmann::json::array();
        for (const auto& tool : tools_.list()) {
            if (tool.contains("name") && domain::policy::contains(profile_.allowed_tools, tool.at("name").get<std::string>())) {
                tools.push_back(tool);
            }
        }
        return result(id, {{"resultType", "complete"}, {"tools", std::move(tools)},
            {"ttlMs", 300000}, {"cacheScope", "private"}});
    }
    if (method == "tools/call") return handle_tool_call(request);
    return rpc_error(id, -32601, "Method not found");
}

nlohmann::json ProtocolHandler::handle_tool_call(const nlohmann::json& request) {
    const nlohmann::json id = request.value("id", nlohmann::json(nullptr));
    const auto& params = request.value("params", nlohmann::json::object());
    if (!params.contains("name") || !params.at("name").is_string()) {
        return rpc_error(id, -32602, "tools/call requires a string name");
    }
    const auto name = params.at("name").get<std::string>();
    const auto tool = tools_.find(name);
    if (!tool || !domain::policy::contains(profile_.allowed_tools, name)) {
        return rpc_error(id, -32602, "Tool is unavailable for this client");
    }
    const auto& arguments = params.value("arguments", nlohmann::json::object());
    if (const auto validation_error = core::validate_tool_arguments(tool->input_schema, arguments)) {
        return rpc_error(id, -32602, *validation_error);
    }
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
    return result(id, {{"resultType", "complete"}, {"content", {{{"type", "text"}, {"text", serialized}}}},
        {"isError", !response.value("ok", false)}});
}

} // namespace tggate::mcp::v2026_07_28
