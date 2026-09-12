#pragma once

#include <cstdint>

namespace Zenvra::Services::Process
{

struct ProcessHandle
{
    std::uint64_t id = 0;
    void* native_handle = nullptr;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return id != 0 && native_handle != nullptr;
    }
};

} // namespace Zenvra::Services::Process
