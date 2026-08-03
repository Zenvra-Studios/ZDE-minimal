#pragma once

#include "Platform/IPlatformWindow.h"

#include <memory>

namespace Zenvra::Platform
{

[[nodiscard]] std::unique_ptr<IPlatformWindow> create_platform_window(
    const WindowSpecification& specification);

} // namespace Zenvra::Platform
