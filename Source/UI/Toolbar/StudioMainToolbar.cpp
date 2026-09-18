#include "UI/Toolbar/StudioMainToolbar.h"

namespace Zenvra::UI::Toolbar
{

ToolbarLayoutResult StudioMainToolbar::layout(float container_width, float content_top) const noexcept
{
    ToolbarLayoutMetrics metrics;
    metrics.target_label = m_run_config_widget.get_state().active_target_name;
    return ToolbarLayoutCalculator::compute_layout(container_width, content_top, m_dpi_scale, metrics);
}

} // namespace Zenvra::UI::Toolbar
