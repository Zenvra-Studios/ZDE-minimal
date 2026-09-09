#pragma once

#include <string>
#include <string_view>

namespace Zenvra::Editors
{

class VimKeySequence
{
public:
    VimKeySequence() = default;

    void push(char c)
    {
        m_sequence.push_back(c);
        m_timer = 0.0;
    }

    void clear()
    {
        m_sequence.clear();
        m_timer = 0.0;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_sequence.empty();
    }

    [[nodiscard]] std::string_view get() const noexcept
    {
        return m_sequence;
    }

    [[nodiscard]] bool update_and_check_timeout(double delta_time, double timeout_sec)
    {
        if (m_sequence.empty())
        {
            return false;
        }
        m_timer += delta_time;
        if (m_timer >= timeout_sec)
        {
            clear();
            return true;
        }
        return false;
    }

private:
    std::string m_sequence;
    double m_timer = 0.0;
};

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using VimKeySequence = Zenvra::Editors::VimKeySequence;
} // namespace Zenvra::UI::Editor
