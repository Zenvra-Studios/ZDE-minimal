#pragma once

#include "Editors/Vim/VimMode.h"
#include "UI/Editor/TextDocumentModel.h"

#include <string>

namespace Zenvra::Editors
{

using UI::Editor::TextPosition;

struct VimState
{
    VimMode mode = VimMode::Normal;
    VimMode previous_mode = VimMode::Normal;

    int count = 0;
    char pending_operator = '\0'; // 'd', 'c', 'y'

    std::string key_sequence;
    double sequence_timer = 0.0;
    double timeout_seconds = 1.0;
    std::string leader_key = "\\";
    std::string escape_key = "Escape";

    std::string yank_register;
    bool yank_is_line = false;

    std::string search_query;
    bool search_forward = true;
    bool search_active = false;

    std::string command_buffer;

    char last_find_char = '\0';
    char last_find_type = '\0'; // 'f', 'F', 't', 'T'
    char pending_find = '\0';   // 'f', 'F', 't', 'T'
    bool pending_replace = false;
    char pending_text_object_modifier = '\0'; // 'i' or 'a'
    int tab_size = 4;

    TextPosition visual_anchor{0, 0};

    [[nodiscard]] int get_effective_count() const noexcept
    {
        return count > 0 ? count : 1;
    }

    void clear_count() noexcept
    {
        count = 0;
    }

    void reset_pending() noexcept
    {
        count = 0;
        pending_operator = '\0';
        pending_find = '\0';
        pending_replace = false;
        pending_text_object_modifier = '\0';
        key_sequence.clear();
        sequence_timer = 0.0;
        search_active = false;
        command_buffer.clear();
    }

    void set_mode(VimMode new_mode) noexcept
    {
        previous_mode = mode;
        mode = new_mode;
        reset_pending();
    }
};

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using VimState = Zenvra::Editors::VimState;
} // namespace Zenvra::UI::Editor
