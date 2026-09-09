#include <gtest/gtest.h>

#include "Editors/Interaction/EditorInputRouter.h"
#include "Editors/Vim/VimEditorMode.h"
#include "Editors/Vim/VimMotions.h"
#include "Editors/Vim/VimOperators.h"
#include "UI/Editor/EditorController.h"
#include "UI/Editor/TextDocumentModel.h"
#include "Settings/SettingsService.h"

using namespace Zenvra;
using namespace Zenvra::Editors;
using namespace Zenvra::UI::Editor;

class VimModeTest : public ::testing::Test
{
protected:
    TextDocumentModel doc;
    EditorController controller;
    EditorInputRouter router;

    void SetUp() override
    {
        doc = TextDocumentModel();
        router.set_interaction_mode(InteractionMode::Vim);
    }

    void type_keys(std::string_view keys)
    {
        for (char c : keys)
        {
            std::string s(1, c);
            if (!router.handle_text_input(s, doc, controller))
            {
                doc.insert_text(s);
            }
        }
    }

    void press_escape()
    {
        EditorKeyEvent ev;
        ev.key_code = 27;
        ev.character = 27;
        router.handle_key(ev, doc, controller);
    }

    void press_ctrl_r()
    {
        EditorKeyEvent ev;
        ev.key_code = 'R';
        ev.ctrl = true;
        router.handle_key(ev, doc, controller);
    }
};

TEST_F(VimModeTest, RouterModeSwitching)
{
    EditorInputRouter test_router;
    EXPECT_EQ(test_router.get_interaction_mode(), InteractionMode::Default);
    EXPECT_FALSE(test_router.is_vim_active());
    EXPECT_EQ(test_router.get_mode_name(), "");
    EXPECT_EQ(test_router.get_cursor_shape(), "Line");

    test_router.set_interaction_mode(InteractionMode::Vim);
    EXPECT_TRUE(test_router.is_vim_active());
    EXPECT_EQ(test_router.get_mode_name(), "-- NORMAL --");
    EXPECT_EQ(test_router.get_cursor_shape(), "Block");

    test_router.set_interaction_mode("default");
    EXPECT_FALSE(test_router.is_vim_active());
}

TEST_F(VimModeTest, BasicMotionsHJKL)
{
    doc.insert_text("line 01\nline 02\nline 03\nline 04");
    doc.set_caret(0, 0);

    // 'l' moves right
    type_keys("l");
    EXPECT_EQ(doc.get_caret_column(), 1u);
    EXPECT_EQ(doc.get_caret_line(), 0u);

    // '3l' moves 3 right
    type_keys("3l");
    EXPECT_EQ(doc.get_caret_column(), 4u);

    // 'h' moves left
    type_keys("h");
    EXPECT_EQ(doc.get_caret_column(), 3u);

    // 'j' moves down
    type_keys("j");
    EXPECT_EQ(doc.get_caret_line(), 1u);

    // '2j' moves 2 lines down
    type_keys("2j");
    EXPECT_EQ(doc.get_caret_line(), 3u);

    // 'k' moves up
    type_keys("k");
    EXPECT_EQ(doc.get_caret_line(), 2u);
}

TEST_F(VimModeTest, WordMotionsWBE)
{
    doc.insert_text("hello world foo bar");
    doc.set_caret(0, 0);

    // 'w' moves to next word start ("world" at col 6)
    type_keys("w");
    EXPECT_EQ(doc.get_caret_column(), 6u);

    // 'w' to "foo" at col 12
    type_keys("w");
    EXPECT_EQ(doc.get_caret_column(), 12u);

    // 'b' moves backward to previous word start ("world" at col 6)
    type_keys("b");
    EXPECT_EQ(doc.get_caret_column(), 6u);

    // 'e' moves to word end ("world" end at col 10)
    type_keys("e");
    EXPECT_EQ(doc.get_caret_column(), 10u);
}

TEST_F(VimModeTest, LineMotionsZeroCaretDollar)
{
    doc.insert_text("   indented text here");
    doc.set_caret(0, 10);

    // '^' moves to first non-blank character (col 3)
    type_keys("^");
    EXPECT_EQ(doc.get_caret_column(), 3u);

    // '$' moves to line end (col 20)
    type_keys("$");
    EXPECT_EQ(doc.get_caret_column(), 20u);

    // '0' moves to col 0
    type_keys("0");
    EXPECT_EQ(doc.get_caret_column(), 0u);
}

TEST_F(VimModeTest, DocumentMotionsGAndGG)
{
    doc.insert_text("first\nsecond\nthird\nfourth");
    doc.set_caret(0, 0);

    // 'G' moves to last line (line 3)
    type_keys("G");
    EXPECT_EQ(doc.get_caret_line(), 3u);

    // 'gg' moves to first line (line 0)
    type_keys("gg");
    EXPECT_EQ(doc.get_caret_line(), 0u);

    // '2G' moves to line 2 (1-indexed => line index 1)
    type_keys("2G");
    EXPECT_EQ(doc.get_caret_line(), 1u);
}

TEST_F(VimModeTest, InsertModeTransitions)
{
    doc.insert_text("sample");
    doc.set_caret(0, 2);

    // 'i' enters insert mode
    type_keys("i");
    EXPECT_TRUE(router.is_insert_mode());
    EXPECT_EQ(router.get_mode_name(), "-- INSERT --");
    EXPECT_EQ(router.get_cursor_shape(), "Line");

    // Typing in insert mode inserts text
    doc.insert_text("X");
    EXPECT_EQ(doc.get_line(0), "saXmple");

    // Escape returns to normal mode
    press_escape();
    EXPECT_FALSE(router.is_insert_mode());
    EXPECT_EQ(router.get_mode_name(), "-- NORMAL --");
    EXPECT_EQ(router.get_cursor_shape(), "Block");
}

TEST_F(VimModeTest, AppendCommands)
{
    doc.insert_text("text");
    doc.set_caret(0, 1); // on 'e'

    // 'a' appends after cursor (col 2)
    type_keys("a");
    EXPECT_TRUE(router.is_insert_mode());
    EXPECT_EQ(doc.get_caret_column(), 2u);
    press_escape();

    // 'A' appends at end of line (col 4)
    type_keys("A");
    EXPECT_TRUE(router.is_insert_mode());
    EXPECT_EQ(doc.get_caret_column(), 4u);
    press_escape();

    // 'o' opens new line below
    type_keys("o");
    EXPECT_TRUE(router.is_insert_mode());
    EXPECT_EQ(doc.get_line_count(), 2u);
    EXPECT_EQ(doc.get_caret_line(), 1u);
    press_escape();
}

TEST_F(VimModeTest, DeleteCharacterUnderCursor)
{
    doc.insert_text("abcdef");
    doc.set_caret(0, 2); // on 'c'

    // 'x' deletes 'c'
    type_keys("x");
    EXPECT_EQ(doc.get_line(0), "abdef");

    // '2x' deletes 'd' and 'e'
    type_keys("2x");
    EXPECT_EQ(doc.get_line(0), "abf");
}

TEST_F(VimModeTest, DeleteLineAndCount)
{
    doc.insert_text("line 1\nline 2\nline 3\nline 4");
    doc.set_caret(1, 0);

    // 'dd' deletes "line 2"
    type_keys("dd");
    EXPECT_EQ(doc.get_line_count(), 3u);
    EXPECT_EQ(doc.get_line(1), "line 3");

    // '2dd' deletes "line 3" and "line 4"
    type_keys("2dd");
    EXPECT_EQ(doc.get_line_count(), 1u);
    EXPECT_EQ(doc.get_line(0), "line 1");
}

TEST_F(VimModeTest, YankAndPut)
{
    doc.insert_text("yank me\nsecond line");
    doc.set_caret(0, 0);

    // 'yy' yanks line 0
    type_keys("yy");

    // 'p' puts line below
    type_keys("p");
    EXPECT_EQ(doc.get_line_count(), 3u);
    EXPECT_EQ(doc.get_line(1), "yank me");

    // Move to col 0 and delete word 'dw'
    doc.set_caret(0, 0);
    type_keys("dw");
    EXPECT_EQ(doc.get_line(0), "me");
}

TEST_F(VimModeTest, VisualModeOperations)
{
    doc.insert_text("select this text");
    doc.set_caret(0, 0);

    // 'v' enters visual mode
    type_keys("v");
    EXPECT_EQ(router.get_mode_name(), "-- VISUAL --");

    // 'w' extends selection
    type_keys("w");
    EXPECT_TRUE(doc.has_selection());

    // 'd' deletes selection and exits visual mode
    type_keys("d");
    EXPECT_EQ(router.get_mode_name(), "-- NORMAL --");
    EXPECT_EQ(doc.get_line(0), "this text");
}

TEST_F(VimModeTest, UndoRedo)
{
    doc.insert_text("initial text");
    doc.set_caret(0, 0);

    // 'dw' deletes "initial "
    type_keys("dw");
    EXPECT_EQ(doc.get_line(0), "text");

    // 'u' undoes the deletion
    type_keys("u");
    EXPECT_EQ(doc.get_line(0), "initial text");

    // Ctrl+R redoes the deletion
    press_ctrl_r();
    EXPECT_EQ(doc.get_line(0), "text");
}

TEST_F(VimModeTest, ForwardAndBackwardSearch)
{
    doc.insert_text("apple banana apple cherry apple");
    doc.set_caret(0, 0);

    // Search forward for "cherry" (starts at col 19)
    type_keys("/cherry\n");
    EXPECT_EQ(doc.get_caret_column(), 19u);

    // Search forward for "apple" (from col 19, finds third occurrence at 26)
    type_keys("/apple\n");
    EXPECT_EQ(doc.get_caret_column(), 26u);

    // 'n' wraps around to first occurrence at 0
    type_keys("n");
    EXPECT_EQ(doc.get_caret_column(), 0u);

    // 'n' moves to second occurrence at 13
    type_keys("n");
    EXPECT_EQ(doc.get_caret_column(), 13u);

    // 'N' moves back to first occurrence at 0
    type_keys("N");
    EXPECT_EQ(doc.get_caret_column(), 0u);
}

TEST_F(VimModeTest, SettingsIntegration)
{
    auto& settings = Settings::SettingsService::instance();
    settings.set("editor.interactionMode", "default");
    settings.set("vim.enabled", false);

    // 1. Default Mode settings verification
    EXPECT_EQ(settings.get<std::string>("editor.interactionMode"), "default");
    EXPECT_EQ(settings.get<std::string>("editor.lineNumbers"), "on");
    EXPECT_EQ(settings.get<std::string>("editor.cursorStyle"), "Line");

    settings.set("editor.lineNumbers", "relative");
    EXPECT_EQ(settings.get<std::string>("editor.lineNumbers"), "relative");
    settings.set("editor.lineNumbers", "on");
    EXPECT_EQ(settings.get<std::string>("editor.lineNumbers"), "on");

    settings.set("editor.cursorStyle", "Block");
    EXPECT_EQ(settings.get<std::string>("editor.cursorStyle"), "Block");
    settings.set("editor.cursorStyle", "Line");
    EXPECT_EQ(settings.get<std::string>("editor.cursorStyle"), "Line");

    // 2. Vim Mode settings verification
    EXPECT_FALSE(settings.get<bool>("vim.enabled"));
    EXPECT_TRUE(settings.get<bool>("vim.showModeIndicator"));
    EXPECT_FALSE(settings.get<bool>("vim.relativeLineNumbers"));
    EXPECT_EQ(settings.get<int>("vim.timeout"), 1000);
    EXPECT_EQ(settings.get<std::string>("vim.startMode"), "normal");
    EXPECT_EQ(settings.get<std::string>("vim.leaderKey"), "\\");
    EXPECT_EQ(settings.get<std::string>("vim.escapeKey"), "Escape");

    // Test switching to Vim mode via setting
    settings.set("editor.interactionMode", "vim");
    EXPECT_EQ(settings.get<std::string>("editor.interactionMode"), "vim");

    // Test switching back to default mode
    settings.set("editor.interactionMode", "default");
    EXPECT_EQ(settings.get<std::string>("editor.interactionMode"), "default");

    // Test enabling vim via boolean
    settings.set("vim.enabled", true);
    EXPECT_TRUE(settings.get<bool>("vim.enabled"));
    settings.set("vim.enabled", false);
    EXPECT_FALSE(settings.get<bool>("vim.enabled"));

    // Test custom escape key and leader key configuration
    settings.set("vim.escapeKey", "jk");
    EXPECT_EQ(settings.get<std::string>("vim.escapeKey"), "jk");
    settings.set("vim.escapeKey", "Escape");

    settings.set("vim.leaderKey", " ");
    EXPECT_EQ(settings.get<std::string>("vim.leaderKey"), " ");
    settings.set("vim.leaderKey", "\\");
}

TEST_F(VimModeTest, FindCharMotions)
{
    doc.insert_text("const int value = 42;");
    doc.set_caret(0, 0);

    // 'f=' jumps to '=' at col 16
    type_keys("f=");
    EXPECT_EQ(doc.get_caret_column(), 16u);

    // 'Fv' jumps backward to 'v' at col 10
    type_keys("Fv");
    EXPECT_EQ(doc.get_caret_column(), 10u);

    // 't=' jumps forward till before '=' (col 15)
    type_keys("t=");
    EXPECT_EQ(doc.get_caret_column(), 15u);

    // ';' repeats last find
    type_keys("0");
    type_keys("fa");
    EXPECT_EQ(doc.get_caret_column(), 11u);
}

TEST_F(VimModeTest, BigWordMotions)
{
    doc.insert_text("foo.bar(baz) qux-corge");
    doc.set_caret(0, 0);

    // 'W' jumps whitespace-delimited word to "qux-corge" at col 13
    type_keys("W");
    EXPECT_EQ(doc.get_caret_column(), 13u);

    // 'B' jumps backward to "foo.bar(baz)" at col 0
    type_keys("B");
    EXPECT_EQ(doc.get_caret_column(), 0u);

    // 'E' jumps to end of WORD at col 11
    type_keys("E");
    EXPECT_EQ(doc.get_caret_column(), 11u);
}

TEST_F(VimModeTest, MatchingBracketAndParagraph)
{
    doc.insert_text("{\n    int x = (10 + 20);\n}\n\nnext paragraph");
    doc.set_caret(0, 0);

    // '%' jumps from opening { to matching } at line 2
    type_keys("%");
    EXPECT_EQ(doc.get_caret_line(), 2u);

    // '%' jumps back to line 0
    type_keys("%");
    EXPECT_EQ(doc.get_caret_line(), 0u);

    // '}' jumps to next blank line / paragraph boundary
    type_keys("}");
    EXPECT_EQ(doc.get_caret_line(), 3u);

    // '{' jumps back to start
    type_keys("{");
    EXPECT_EQ(doc.get_caret_line(), 0u);
}

TEST_F(VimModeTest, TextObjects)
{
    doc.insert_text("foo \"hello world\" bar (123 + 456)");
    doc.set_caret(0, 7); // inside "hello world"

    // 'di"' deletes inside quotes leaving ""
    type_keys("di\"");
    EXPECT_EQ(doc.get_line(0), "foo \"\" bar (123 + 456)");

    // 'da"' deletes around quotes including quotes
    doc.set_caret(0, 4); // on quote
    type_keys("da\"");
    EXPECT_EQ(doc.get_line(0), "foo bar (123 + 456)");

    // 'diw' on "bar" deletes word
    doc.set_caret(0, 4); // on 'b'
    type_keys("diw");
    EXPECT_EQ(doc.get_line(0), "foo  (123 + 456)");
}

TEST_F(VimModeTest, SingleCharReplaceAndSubstitute)
{
    doc.insert_text("hello world");
    doc.set_caret(0, 0);

    // 'rx' replaces 'h' with 'x'
    type_keys("rx");
    EXPECT_EQ(doc.get_line(0), "xello world");
    EXPECT_EQ(doc.get_caret_column(), 0u);

    // 's' substitutes character and enters Insert mode
    type_keys("sY");
    press_escape();
    EXPECT_EQ(doc.get_line(0), "Yello world");
}

TEST_F(VimModeTest, JoinLinesAndToggleCase)
{
    doc.insert_text("hello\n    world");
    doc.set_caret(0, 0);

    // 'J' joins lines with single space
    type_keys("J");
    EXPECT_EQ(doc.get_line(0), "hello world");
    EXPECT_EQ(doc.get_line_count(), 1u);

    // '~' toggles case
    doc.set_caret(0, 0);
    type_keys("~");
    EXPECT_EQ(doc.get_line(0), "Hello world");
}

TEST_F(VimModeTest, IndentAndUnindent)
{
    doc.insert_text("hello world");
    doc.set_caret(0, 0);

    // '>>' indents line by 4 spaces
    type_keys(">>");
    EXPECT_EQ(doc.get_line(0), "    hello world");

    // '<<' unindents line by 4 spaces
    type_keys("<<");
    EXPECT_EQ(doc.get_line(0), "hello world");
}

TEST_F(VimModeTest, CommandLineMode)
{
    doc.insert_text("line 1\nline 2\nline 3\nline 4\nline 5");
    doc.set_caret(0, 0);

    // Jump to line 4 via ':4' + Enter
    type_keys(":4\r");
    EXPECT_EQ(doc.get_caret_line(), 3u);

    // Mode indicator during typing shows :command
    type_keys(":w");
    EXPECT_EQ(router.get_mode_name(), ":w");
    type_keys("\r");
    EXPECT_EQ(router.get_mode_name(), "-- NORMAL --");

    // Substitution: :%s/line/row/g
    type_keys(":%s/line/row/g\r");
    EXPECT_EQ(doc.get_line(0), "row 1");
    EXPECT_EQ(doc.get_line(3), "row 4");
}
