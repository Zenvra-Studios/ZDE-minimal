#pragma once

#include <cstdint>
#include <string_view>
#include <array>

namespace Zenvra::Language::Syntax
{

enum class SemanticTokenType : uint32_t
{
    Type = 0,
    Class = 1,
    Enum = 2,
    Interface = 3,
    Struct = 4,
    TypeParameter = 5,
    Parameter = 6,
    Variable = 7,
    Property = 8,
    EnumMember = 9,
    Event = 10,
    Function = 11,
    Method = 12,
    Macro = 13,
    Keyword = 14,
    Modifier = 15,
    Comment = 16,
    String = 17,
    Number = 18,
    Regexp = 19,
    Operator = 20,
    Custom = 21,
    Count = 22
};

#ifdef None
#pragma push_macro("None")
#undef None
#define ZDE_RESTORED_NONE 1
#endif

namespace SemanticTokenModifier
{
    inline constexpr uint32_t None           = 0;
    inline constexpr uint32_t Declaration    = 1U << 0;
    inline constexpr uint32_t Definition     = 1U << 1;
    inline constexpr uint32_t Readonly       = 1U << 2;
    inline constexpr uint32_t Static         = 1U << 3;
    inline constexpr uint32_t Deprecated     = 1U << 4;
    inline constexpr uint32_t Abstract       = 1U << 5;
    inline constexpr uint32_t Async          = 1U << 6;
    inline constexpr uint32_t Modification   = 1U << 7;
    inline constexpr uint32_t Documentation  = 1U << 8;
    inline constexpr uint32_t DefaultLibrary = 1U << 9;
}

[[nodiscard]] inline std::string_view semantic_token_type_to_string(SemanticTokenType type) noexcept
{
    constexpr std::array<std::string_view, static_cast<std::size_t>(SemanticTokenType::Count)> names = {
        "type", "class", "enum", "interface", "struct", "typeParameter",
        "parameter", "variable", "property", "enumMember", "event",
        "function", "method", "macro", "keyword", "modifier",
        "comment", "string", "number", "regexp", "operator", "custom"
    };
    const auto idx = static_cast<std::size_t>(type);
    return idx < names.size() ? names[idx] : "unknown";
}

[[nodiscard]] inline SemanticTokenType string_to_semantic_token_type(std::string_view name) noexcept
{
    if (name == "type") return SemanticTokenType::Type;
    if (name == "class") return SemanticTokenType::Class;
    if (name == "enum") return SemanticTokenType::Enum;
    if (name == "interface") return SemanticTokenType::Interface;
    if (name == "struct") return SemanticTokenType::Struct;
    if (name == "typeParameter") return SemanticTokenType::TypeParameter;
    if (name == "parameter") return SemanticTokenType::Parameter;
    if (name == "variable") return SemanticTokenType::Variable;
    if (name == "property") return SemanticTokenType::Property;
    if (name == "enumMember") return SemanticTokenType::EnumMember;
    if (name == "event") return SemanticTokenType::Event;
    if (name == "function") return SemanticTokenType::Function;
    if (name == "method") return SemanticTokenType::Method;
    if (name == "macro") return SemanticTokenType::Macro;
    if (name == "keyword") return SemanticTokenType::Keyword;
    if (name == "modifier") return SemanticTokenType::Modifier;
    if (name == "comment") return SemanticTokenType::Comment;
    if (name == "string") return SemanticTokenType::String;
    if (name == "number") return SemanticTokenType::Number;
    if (name == "regexp") return SemanticTokenType::Regexp;
    if (name == "operator") return SemanticTokenType::Operator;
    return SemanticTokenType::Custom;
}

} // namespace Zenvra::Language::Syntax

#ifdef ZDE_RESTORED_NONE
#pragma pop_macro("None")
#undef ZDE_RESTORED_NONE
#endif

