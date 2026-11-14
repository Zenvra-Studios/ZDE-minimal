#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::Services::Installation::Verification
{

class Sha256Verifier
{
public:
    [[nodiscard]] static std::string compute_hash_file(const std::filesystem::path& file_path);
    [[nodiscard]] static std::string compute_hash_bytes(const void* data, std::size_t size);
    [[nodiscard]] static bool verify_file(const std::filesystem::path& file_path, std::string_view expected_sha256);
};

} // namespace Zenvra::Services::Installation::Verification

