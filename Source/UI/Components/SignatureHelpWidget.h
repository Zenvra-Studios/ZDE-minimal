#pragma once

#include "Language/Protocol/LspTypes.h"
#include "UI/Geometry.h"

#include <string>

namespace Zenvra::UI::Components
{

class SignatureHelpWidget
{
public:
    SignatureHelpWidget() = default;

    void show(Language::Protocol::SignatureHelp help, float x, float y);
    void hide() noexcept;
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }

    [[nodiscard]] const Language::Protocol::SignatureHelp& get_help() const noexcept { return m_help; }
    [[nodiscard]] float get_x() const noexcept { return m_x; }
    [[nodiscard]] float get_y() const noexcept { return m_y; }

    [[nodiscard]] Rect calculate_bounds(float width = 340.0F, float height = 48.0F) const noexcept;

private:
    bool m_visible = false;
    float m_x = 0.0F;
    float m_y = 0.0F;
    Language::Protocol::SignatureHelp m_help;
};

} // namespace Zenvra::UI::Components
