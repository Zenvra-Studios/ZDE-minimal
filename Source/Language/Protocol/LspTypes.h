#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Language::Protocol
{

struct Position
{
    std::size_t line = 0;
    std::size_t character = 0;

    auto operator<=>(const Position&) const = default;
};

struct Range
{
    Position start;
    Position end;

    auto operator<=>(const Range&) const = default;
};

struct Location
{
    std::string uri;
    Range range;

    bool operator==(const Location&) const = default;
    auto operator<=>(const Location& other) const {
        if (const auto cmp = uri.compare(other.uri); cmp != 0) {
            return cmp <=> 0;
        }
        return range <=> other.range;
    }
};

enum class DiagnosticSeverity : int
{
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

enum class DiagnosticTag : int
{
    Unnecessary = 1,
    Deprecated = 2,
};

struct Diagnostic
{
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string source;
    std::string code;
    std::vector<DiagnosticTag> tags;

    [[nodiscard]] bool is_unnecessary() const noexcept
    {
        for (auto tag : tags)
        {
            if (tag == DiagnosticTag::Unnecessary) return true;
        }
        if (code == "unused-includes" || code == "unused" || code == "unused-variable" ||
            code == "unused-function" || code == "unused-parameter" || code == "dead_code" ||
            code == "unused-macro" || code == "unused-macros" || code == "unused_macro" ||
            code.find("unused") != std::string::npos || code.find("Unused") != std::string::npos ||
            code.find("redundant") != std::string::npos)
        {
            return true;
        }
        if (message.find("not used") != std::string::npos ||
            message.find("unused") != std::string::npos ||
            message.find("Unused") != std::string::npos ||
            message.find("never used") != std::string::npos ||
            message.find("never read") != std::string::npos ||
            message.find("not referenced") != std::string::npos ||
            message.find("is not needed") != std::string::npos ||
            message.find("is redundant") != std::string::npos ||
            message.find("is never read") != std::string::npos ||
            message.find("declared but its value is never read") != std::string::npos ||
            message.find("is not accessed") != std::string::npos ||
            message.find("defined but not used") != std::string::npos ||
            message.find("macro not used") != std::string::npos ||
            message.find("Macro not used") != std::string::npos ||
            message.find("statement macro") != std::string::npos ||
            (message.find("Included header") != std::string::npos && message.find("not used") != std::string::npos) ||
            (message.find("macro") != std::string::npos && message.find("not used") != std::string::npos) ||
            (message.find("Macro") != std::string::npos && message.find("not used") != std::string::npos))
        {
            return true;
        }
        return false;
    }
};

enum class CompletionItemKind : int
{
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25,
};

struct CompletionItem
{
    std::string label = {};
    CompletionItemKind kind = CompletionItemKind::Text;
    std::string detail = {};
    std::string documentation = {};
    std::string insert_text = {};
    std::string sort_text = {};
    std::string filter_text = {};
};

struct Hover
{
    std::string contents;
    std::optional<Range> range;
};

struct ParameterInformation
{
    std::string label;
    std::string documentation;
};

struct SignatureInformation
{
    std::string label;
    std::string documentation;
    std::vector<ParameterInformation> parameters;
    std::size_t active_parameter = 0;
};

struct SignatureHelp
{
    std::vector<SignatureInformation> signatures;
    std::size_t active_signature = 0;
    std::size_t active_parameter = 0;
};

struct SemanticTokens
{
    std::string result_id;
    std::vector<uint32_t> data;
};

} // namespace Zenvra::Language::Protocol
