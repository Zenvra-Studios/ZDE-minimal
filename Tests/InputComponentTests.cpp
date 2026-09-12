#include "UI/Components/Input.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

using namespace Zenvra::UI;
using namespace Zenvra::UI::Components;

TEST(InputTests, InitialStateAndBounds) {
    Input input("Enter text here...", Rect{10.0F, 20.0F, 200.0F, 30.0F});

    EXPECT_EQ(input.get_text(), "");
    EXPECT_EQ(input.get_placeholder(), "Enter text here...");
    EXPECT_EQ(input.get_bounds().x, 10.0F);
    EXPECT_EQ(input.get_bounds().y, 20.0F);
    EXPECT_EQ(input.get_bounds().width, 200.0F);
    EXPECT_EQ(input.get_bounds().height, 30.0F);

    EXPECT_FALSE(input.is_focused());
    EXPECT_FALSE(input.is_caret_visible());
    EXPECT_EQ(input.get_cursor_position(), 0);
    EXPECT_FALSE(input.has_selection());
}

TEST(InputTests, PointerPressTriggerAndFocus) {
    Input input("Placeholder", Rect{50.0F, 50.0F, 150.0F, 30.0F});

    // 1. Click outside bounds -> unfocused
    EXPECT_FALSE(input.handle_pointer_press(10.0F, 10.0F));
    EXPECT_FALSE(input.is_focused());
    EXPECT_FALSE(input.is_caret_visible());

    // 2. Click inside bounds -> focused, caret becomes immediately visible
    EXPECT_TRUE(input.handle_pointer_press(80.0F, 60.0F));
    EXPECT_TRUE(input.is_focused());
    EXPECT_TRUE(input.is_caret_visible());

    // 3. Programmatic focus and blur
    input.blur();
    EXPECT_FALSE(input.is_focused());
    EXPECT_FALSE(input.is_caret_visible());

    input.focus();
    EXPECT_TRUE(input.is_focused());
    EXPECT_TRUE(input.is_caret_visible());
}

TEST(InputTests, TextInputAndCursorMovement) {
    Input input("Search", Rect{0.0F, 0.0F, 200.0F, 30.0F});

    // Unfocused typing is rejected
    EXPECT_FALSE(input.handle_text_input("hello"));
    EXPECT_EQ(input.get_text(), "");

    input.focus();
    EXPECT_TRUE(input.handle_text_input("hello"));
    EXPECT_EQ(input.get_text(), "hello");
    EXPECT_EQ(input.get_cursor_position(), 5);
    EXPECT_EQ(input.get_text_before_cursor(), "hello");
    EXPECT_EQ(input.get_text_after_cursor(), "");

    // Move cursor to position 2 and insert
    input.set_cursor_position(2);
    EXPECT_EQ(input.get_cursor_position(), 2);
    EXPECT_EQ(input.get_text_before_cursor(), "he");
    EXPECT_EQ(input.get_text_after_cursor(), "llo");

    EXPECT_TRUE(input.handle_text_input("XY"));
    EXPECT_EQ(input.get_text(), "heXYllo");
    EXPECT_EQ(input.get_cursor_position(), 4);
    EXPECT_EQ(input.get_text_before_cursor(), "heXY");
    EXPECT_EQ(input.get_text_after_cursor(), "llo");
}

TEST(InputTests, CharCodepointInput) {
    Input input("", Rect{0.0F, 0.0F, 200.0F, 30.0F});
    input.focus();

    // ASCII
    EXPECT_TRUE(input.handle_char('A'));
    // 2-byte UTF-8
    EXPECT_TRUE(input.handle_char(U'ñ'));
    // 3-byte UTF-8
    EXPECT_TRUE(input.handle_char(U'中'));
    // 4-byte UTF-8
    EXPECT_TRUE(input.handle_char(U'🚀'));

    EXPECT_EQ(input.get_text(), "Añ中🚀");
    // 'A' (1) + 'ñ' (2) + '中' (3) + '🚀' (4) = 10 bytes
    EXPECT_EQ(input.get_cursor_position(), 10);
    EXPECT_EQ(input.get_text().length(), 10);

    // Control characters rejected
    EXPECT_FALSE(input.handle_char(0));
    EXPECT_FALSE(input.handle_char(10)); // newline
    EXPECT_FALSE(input.handle_char(127)); // DEL
}

TEST(InputTests, BackspaceAndForwardDelete) {
    Input input("", Rect{0.0F, 0.0F, 200.0F, 30.0F});
    input.focus();
    input.set_text("Añ中🚀B");

    // Starts at end (11 bytes)
    EXPECT_EQ(input.get_cursor_position(), 11);

    // 1. Backspace 'B' (1 byte)
    EXPECT_TRUE(input.handle_backspace());
    EXPECT_EQ(input.get_text(), "Añ中🚀");
    EXPECT_EQ(input.get_cursor_position(), 10);

    // 2. Backspace '🚀' (4 bytes)
    EXPECT_TRUE(input.handle_backspace());
    EXPECT_EQ(input.get_text(), "Añ中");
    EXPECT_EQ(input.get_cursor_position(), 6);

    // 3. Backspace '中' (3 bytes)
    EXPECT_TRUE(input.handle_backspace());
    EXPECT_EQ(input.get_text(), "Añ");
    EXPECT_EQ(input.get_cursor_position(), 3);

    // 4. Backspace 'ñ' (2 bytes)
    EXPECT_TRUE(input.handle_backspace());
    EXPECT_EQ(input.get_text(), "A");
    EXPECT_EQ(input.get_cursor_position(), 1);

    // 5. Backspace 'A' (1 byte)
    EXPECT_TRUE(input.handle_backspace());
    EXPECT_EQ(input.get_text(), "");
    EXPECT_EQ(input.get_cursor_position(), 0);

    // 6. Backspace when empty
    EXPECT_FALSE(input.handle_backspace());

    // 7. Forward delete test
    input.set_text("Hello");
    input.set_cursor_position(1); // after 'H'
    EXPECT_TRUE(input.handle_delete()); // deletes 'e'
    EXPECT_EQ(input.get_text(), "Hllo");
    EXPECT_EQ(input.get_cursor_position(), 1);

    input.set_cursor_position(4); // at end
    EXPECT_FALSE(input.handle_delete());
}

TEST(InputTests, CursorNavigationLeftRightHomeEnd) {
    Input input("", Rect{0.0F, 0.0F, 200.0F, 30.0F});
    input.focus();
    input.set_text("AñB"); // 'A'=0..1, 'ñ'=1..3, 'B'=3..4

    EXPECT_EQ(input.get_cursor_position(), 4);

    // Left over 'B' (1 byte) -> 3
    EXPECT_TRUE(input.handle_left());
    EXPECT_EQ(input.get_cursor_position(), 3);

    // Left over 'ñ' (2 bytes) -> 1
    EXPECT_TRUE(input.handle_left());
    EXPECT_EQ(input.get_cursor_position(), 1);

    // Left over 'A' (1 byte) -> 0
    EXPECT_TRUE(input.handle_left());
    EXPECT_EQ(input.get_cursor_position(), 0);

    // Left at 0 returns false
    EXPECT_FALSE(input.handle_left());

    // Right over 'A' -> 1
    EXPECT_TRUE(input.handle_right());
    EXPECT_EQ(input.get_cursor_position(), 1);

    // End jumps to 4
    EXPECT_TRUE(input.handle_end());
    EXPECT_EQ(input.get_cursor_position(), 4);

    // Home jumps to 0
    EXPECT_TRUE(input.handle_home());
    EXPECT_EQ(input.get_cursor_position(), 0);
}

TEST(InputTests, TextSelectionAndReplace) {
    Input input("", Rect{0.0F, 0.0F, 200.0F, 30.0F});
    input.focus();
    input.set_text("Hello World");

    input.select_all();
    EXPECT_TRUE(input.has_selection());
    EXPECT_EQ(input.get_selected_text(), "Hello World");

    // Typing while selected replaces selection
    EXPECT_TRUE(input.handle_text_input("New"));
    EXPECT_EQ(input.get_text(), "New");
    EXPECT_FALSE(input.has_selection());
    EXPECT_EQ(input.get_cursor_position(), 3);

    // Backspace while selected deletes selection
    input.select_all();
    EXPECT_TRUE(input.handle_backspace());
    EXPECT_EQ(input.get_text(), "");
    EXPECT_FALSE(input.has_selection());
}

TEST(InputTests, CaretBlinkingLifecycle) {
    Input input("", Rect{0.0F, 0.0F, 200.0F, 30.0F});

    // Unfocused: tick returns false, caret invisible
    EXPECT_FALSE(input.tick());
    EXPECT_FALSE(input.is_caret_visible());

    input.focus();
    EXPECT_TRUE(input.is_caret_visible());

    // Immediately after focus, tick does not change state (interval not elapsed)
    EXPECT_FALSE(input.tick());
    EXPECT_TRUE(input.is_caret_visible());

    // Wait for caret blink interval (> 530ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    EXPECT_TRUE(input.tick());
    EXPECT_FALSE(input.is_caret_visible());

    // reset_blink forces caret to be visible again
    input.reset_blink();
    EXPECT_TRUE(input.is_caret_visible());
}

TEST(InputTests, TextChangedCallback) {
    Input input("", Rect{0.0F, 0.0F, 200.0F, 30.0F});
    input.focus();

    std::string last_val;
    input.set_on_text_changed([&last_val](const std::string& val) {
        last_val = val;
    });

    input.handle_text_input("foo");
    EXPECT_EQ(last_val, "foo");

    input.handle_backspace();
    EXPECT_EQ(last_val, "fo");

    input.clear();
    EXPECT_EQ(last_val, "");
}
