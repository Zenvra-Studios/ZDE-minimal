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

    auto operator<=>(const Location&) const = default;
};

enum class DiagnosticSeverity : int
{
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

struct Diagnostic
{
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string source;
    std::string code;
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
