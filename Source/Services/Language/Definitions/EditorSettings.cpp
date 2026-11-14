#include "Services/Language/Definitions/EditorSettings.h"

namespace Zenvra::Services::Language::Definitions
{

nlohmann::json EditorSettings::to_json() const
{
    nlohmann::json j;
    j["tabSize"] = tab_size;
    j["insertSpaces"] = insert_spaces;
    j["autoCloseBraces"] = auto_close_braces;
    j["autoCloseBrackets"] = auto_close_brackets;
    j["autoCloseParentheses"] = auto_close_parentheses;
    j["autoCloseQuotes"] = auto_close_quotes;
    j["autoContinueComments"] = auto_continue_comments;
    j["lineComment"] = line_comment;
    j["blockCommentStart"] = block_comment_start;
    j["blockCommentEnd"] = block_comment_end;
    j["wordPattern"] = word_pattern;
    return j;
}

EditorSettings EditorSettings::from_json(const nlohmann::json& j)
{
    EditorSettings settings;
    if (!j.is_object()) return settings;

    if (j.contains("tabSize") && j["tabSize"].is_number_unsigned())
        settings.tab_size = j["tabSize"].get<std::size_t>();
    if (j.contains("insertSpaces") && j["insertSpaces"].is_boolean())
        settings.insert_spaces = j["insertSpaces"].get<bool>();
    if (j.contains("autoCloseBraces") && j["autoCloseBraces"].is_boolean())
        settings.auto_close_braces = j["autoCloseBraces"].get<bool>();
    if (j.contains("autoCloseBrackets") && j["autoCloseBrackets"].is_boolean())
        settings.auto_close_brackets = j["autoCloseBrackets"].get<bool>();
    if (j.contains("autoCloseParentheses") && j["autoCloseParentheses"].is_boolean())
        settings.auto_close_parentheses = j["autoCloseParentheses"].get<bool>();
    if (j.contains("autoCloseQuotes") && j["autoCloseQuotes"].is_boolean())
        settings.auto_close_quotes = j["autoCloseQuotes"].get<bool>();
    if (j.contains("autoContinueComments") && j["autoContinueComments"].is_boolean())
        settings.auto_continue_comments = j["autoContinueComments"].get<bool>();
    if (j.contains("lineComment") && j["lineComment"].is_string())
        settings.line_comment = j["lineComment"].get<std::string>();
    if (j.contains("blockCommentStart") && j["blockCommentStart"].is_string())
        settings.block_comment_start = j["blockCommentStart"].get<std::string>();
    if (j.contains("blockCommentEnd") && j["blockCommentEnd"].is_string())
        settings.block_comment_end = j["blockCommentEnd"].get<std::string>();
    if (j.contains("wordPattern") && j["wordPattern"].is_string())
        settings.word_pattern = j["wordPattern"].get<std::string>();

    return settings;
}

} // namespace Zenvra::Services::Language::Definitions

