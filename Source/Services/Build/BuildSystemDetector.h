#pragma once

#include "Services/Build/BuildSystem.h"
#include "Services/Build/CMakeBuildSystem.h"
#include "Services/Build/CargoBuildSystem.h"
#include "Services/Build/GradleBuildSystem.h"
#include "Services/Build/MavenBuildSystem.h"
#include "Services/Build/MakeBuildSystem.h"
#include "Services/Build/FreePascalBuildSystem.h"

#include <memory>
#include <vector>

namespace Zenvra::Services::Build
{

class BuildSystemDetector
{
public:
    [[nodiscard]] static std::unique_ptr<IBuildSystem> detect_build_system(const std::filesystem::path& root)
    {
        // 1. CMake
        auto cmake = std::make_unique<CMakeBuildSystem>();
        if (cmake->detect(root))
        {
            return cmake;
        }

        // 2. Cargo
        auto cargo = std::make_unique<CargoBuildSystem>();
        if (cargo->detect(root))
        {
            return cargo;
        }

        // 3. Gradle
        auto gradle = std::make_unique<GradleBuildSystem>();
        if (gradle->detect(root))
        {
            return gradle;
        }

        // 4. Maven
        auto maven = std::make_unique<MavenBuildSystem>();
        if (maven->detect(root))
        {
            return maven;
        }

        // 5. Make
        auto make = std::make_unique<MakeBuildSystem>();
        if (make->detect(root))
        {
            return make;
        }

        // 6. FreePascal
        auto fpc = std::make_unique<FreePascalBuildSystem>();
        if (fpc->detect(root))
        {
            return fpc;
        }

        return nullptr;
    }

    [[nodiscard]] static std::vector<std::unique_ptr<IBuildSystem>> detect_all(const std::filesystem::path& root)
    {
        std::vector<std::unique_ptr<IBuildSystem>> systems;

        auto cmake = std::make_unique<CMakeBuildSystem>();
        if (cmake->detect(root)) systems.push_back(std::move(cmake));

        auto cargo = std::make_unique<CargoBuildSystem>();
        if (cargo->detect(root)) systems.push_back(std::move(cargo));

        auto gradle = std::make_unique<GradleBuildSystem>();
        if (gradle->detect(root)) systems.push_back(std::move(gradle));

        auto maven = std::make_unique<MavenBuildSystem>();
        if (maven->detect(root)) systems.push_back(std::move(maven));

        auto make = std::make_unique<MakeBuildSystem>();
        if (make->detect(root)) systems.push_back(std::move(make));

        auto fpc = std::make_unique<FreePascalBuildSystem>();
        if (fpc->detect(root)) systems.push_back(std::move(fpc));

        return systems;
    }
};

} // namespace Zenvra::Services::Build
