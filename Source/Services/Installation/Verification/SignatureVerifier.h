#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Installation::Verification
{

enum class VerificationStatus
{
    Valid,
    HashMismatch,
    SignatureInvalid,
    FileNotFound,
    UntrustedSigner,
    CorruptPackage
};

struct VerificationResult
{
    VerificationStatus status{VerificationStatus::FileNotFound};
    std::string details;
    std::string computed_hash;
    bool verified{false};
};

class SignatureVerifier
{
public:
    SignatureVerifier() = default;

    [[nodiscard]] static VerificationResult verify_package(
        const std::filesystem::path& package_path,
        std::string_view expected_sha256,
        std::string_view signature = {});

    [[nodiscard]] static bool is_hash_valid(
        const std::filesystem::path& file_path,
        std::string_view expected_sha256);
};

} // namespace Zenvra::Services::Installation::Verification

