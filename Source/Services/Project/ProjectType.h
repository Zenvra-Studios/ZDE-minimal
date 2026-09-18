#pragma once

#include <cstdint>
#include <string_view>

namespace Zenvra::Services::Project
{

enum class ProjectType : std::uint8_t
{
    Unknown,
    CMake,
    Cargo,
    Maven,
    Gradle,
    Make,
    Python,
    Pascal,
    PlainCpp,
    PlainJava,
    PlainRust,
    JavaScript,
    TypeScript,
    PHP,
    Web
};

enum class ExecutionModel : std::uint8_t
{
    CompiledBinary,
    InterpreterScript,
    CustomCommand,
    WebInterpreter
};

[[nodiscard]] constexpr std::string_view to_string(ProjectType type) noexcept
{
    switch (type)
    {
    case ProjectType::CMake: return "CMake";
    case ProjectType::Cargo: return "Cargo";
    case ProjectType::Maven: return "Maven";
    case ProjectType::Gradle: return "Gradle";
    case ProjectType::Make: return "Make";
    case ProjectType::Python: return "Python";
    case ProjectType::Pascal: return "Pascal";
    case ProjectType::PlainCpp: return "C/C++";
    case ProjectType::PlainJava: return "Java";
    case ProjectType::PlainRust: return "Rust";
    case ProjectType::JavaScript: return "JavaScript";
    case ProjectType::TypeScript: return "TypeScript";
    case ProjectType::PHP: return "PHP";
    case ProjectType::Web: return "Web";
    case ProjectType::Unknown: return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(ExecutionModel model) noexcept
{
    switch (model)
    {
    case ExecutionModel::CompiledBinary: return "CompiledBinary";
    case ExecutionModel::InterpreterScript: return "InterpreterScript";
    case ExecutionModel::CustomCommand: return "CustomCommand";
    case ExecutionModel::WebInterpreter: return "WebInterpreter";
    }
    return "CustomCommand";
}

} // namespace Zenvra::Services::Project
