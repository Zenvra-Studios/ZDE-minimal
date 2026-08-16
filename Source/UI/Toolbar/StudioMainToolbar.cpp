#include "UI/Toolbar/StudioMainToolbar.h"

namespace Zenvra::UI::Toolbar
{

ToolbarLayoutResult StudioMainToolbar::layout(float container_width, float content_top) const noexcept
{
    return ToolbarLayoutCalculator::compute_layout(container_width, content_top, m_dpi_scale);
}

} // namespace Zenvra::UI::Toolbar
