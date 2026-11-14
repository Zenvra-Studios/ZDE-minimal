#pragma once

#include "Source/Services/Toolchain/ToolchainManager.h"
#include "Source/Services/Toolchain/ToolchainDetector.h"
#include "Source/Services/Toolchain/Toolchain.h"
#include "Source/Services/Toolchain/CppToolchain.h"
#include "Source/Services/Toolchain/RustToolchain.h"
#include "Source/Services/Toolchain/PythonToolchain.h"
#include "Source/Services/Toolchain/JavaToolchain.h"
#include "Source/Services/Toolchain/FreePascalToolchain.h"

namespace Zenvra::Services::Toolchains
{
    using ToolchainManager = Zenvra::Services::Toolchain::ToolchainManager;
    using ToolchainDetector = Zenvra::Services::Toolchain::ToolchainDetector;
    using IToolchain = Zenvra::Services::Toolchain::IToolchain;
    using ToolchainType = Zenvra::Services::Toolchain::ToolchainType;
}

