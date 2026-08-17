#include "mcp/core/InputSchemaValidator.hpp"

#include <cstddef>
#include <string_view>

namespace tggate::mcp::core {
namespace {

std::size_t utf8_code_point_count(const std::string_view value) {
    std::size_t result = 0;
    for (const auto byte : value) {
        if ((static_cast<unsigned char>(byte) & 0xc0U) != 0x80U) ++result;
    }
    return result;
}

bool matches_type(const nlohmann::json& value, const std::string_view type) {
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "string") return value.is_string();
    if (type == "integer") return value.is_number_integer() || value.is_number_unsigned();
    if (type == "number") return value.is_number();
    if (type == "boolean") return value.is_boolean();
    if (type == "null") return value.is_null();
    return false;
}

std::optional<std::string> validate(const nlohmann::json& schema, const nlohmann::json& value, const std::string_view path) {
    if (!schema.is_object()) return std::string("Tool input schema is invalid");
    if (const auto type = schema.find("type"); type != schema.end()) {
        if (!type->is_string() || !matches_type(value, type->get<std::string>())) {
            return "Tool arguments do not match the schema at " + std::string(path);
        }
    }

    if (value.is_object()) {
        const auto properties = schema.value("properties", nlohmann::json::object());
        if (!properties.is_object()) return std::string("Tool input schema is invalid");
        const auto required = schema.value("required", nlohmann::json::array());
        if (!required.is_array()) return std::string("Tool input schema is invalid");
        for (const auto& name : required) {
            if (!name.is_string()) return std::string("Tool input schema is invalid");
            if (!value.contains(name.get<std::string>())) {
                return "Tool arguments do not match the schema at " + std::string(path);
            }
        }
        const auto additional_properties = schema.find("additionalProperties");
        if (additional_properties != schema.end() && !additional_properties->is_boolean()) {
            return std::string("Tool input schema is invalid");
        }
        if (additional_properties != schema.end() && !additional_properties->get<bool>()) {
            for (const auto& [name, item] : value.items()) {
                static_cast<void>(item);
                if (!properties.contains(name)) {
                    return "Tool arguments do not match the schema at " + std::string(path);
                }
            }
        }
        for (const auto& [name, property_schema] : properties.items()) {
            const auto argument = value.find(name);
            if (argument == value.end()) continue;
            if (const auto error = validate(property_schema, *argument, std::string(path) + "." + name)) return error;
        }
    }

    if (value.is_string()) {
        const auto length = utf8_code_point_count(value.get_ref<const nlohmann::json::string_t&>());
        if (const auto minimum = schema.find("minLength"); minimum != schema.end()) {
            if (!minimum->is_number_integer() && !minimum->is_number_unsigned()) return std::string("Tool input schema is invalid");
            const auto minimum_length = minimum->get<long double>();
            if (minimum_length < 0 || static_cast<long double>(length) < minimum_length) {
                return "Tool arguments do not match the schema at " + std::string(path);
            }
        }
        if (const auto maximum = schema.find("maxLength"); maximum != schema.end()) {
            if (!maximum->is_number_integer() && !maximum->is_number_unsigned()) return std::string("Tool input schema is invalid");
            const auto maximum_length = maximum->get<long double>();
            if (maximum_length < 0 || static_cast<long double>(length) > maximum_length) {
                return "Tool arguments do not match the schema at " + std::string(path);
            }
        }
    }

    if (value.is_number()) {
        const auto number = value.get<long double>();
        if (const auto minimum = schema.find("minimum"); minimum != schema.end() &&
            (!minimum->is_number() || number < minimum->get<long double>())) {
            return "Tool arguments do not match the schema at " + std::string(path);
        }
        if (const auto maximum = schema.find("maximum"); maximum != schema.end() &&
            (!maximum->is_number() || number > maximum->get<long double>())) {
            return "Tool arguments do not match the schema at " + std::string(path);
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> validate_tool_arguments(const nlohmann::json& input_schema, const nlohmann::json& arguments) {
    return validate(input_schema, arguments, "arguments");
}

} // namespace tggate::mcp::core
