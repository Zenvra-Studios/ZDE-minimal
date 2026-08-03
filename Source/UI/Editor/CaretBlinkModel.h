#pragma once

#include <chrono>

namespace Zenvra::UI::Editor
{

class CaretBlinkModel
{
public:
    void reset() noexcept;
    [[nodiscard]] bool tick() noexcept;
    [[nodiscard]] bool is_visible() const noexcept;

private:
    static constexpr std::chrono::milliseconds blink_interval{530};

    std::chrono::steady_clock::time_point m_last_toggle =
        std::chrono::steady_clock::now();
    bool m_visible = true;
};

} // namespace Zenvra::UI::Editor
