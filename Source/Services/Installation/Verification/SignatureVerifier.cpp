#include "SignatureVerifier.h"
#include "Sha256Verifier.h"

namespace Zenvra::Services::Installation::Verification
{

VerificationResult SignatureVerifier::verify_package(
    const std::filesystem::path& package_path,
    std::string_view expected_sha256,
    std::string_view signature)
{
    VerificationResult result;

    if (!std::filesystem::exists(package_path))
    {
        result.status = VerificationStatus::FileNotFound;
        result.details = "Package file not found: " + package_path.string();
        result.verified = false;
        return result;
    }

    result.computed_hash = Sha256Verifier::compute_hash_file(package_path);

    if (!expected_sha256.empty())
    {
        if (!Sha256Verifier::verify_file(package_path, expected_sha256))
        {
            result.status = VerificationStatus::HashMismatch;
            result.details = "SHA-256 hash mismatch. Expected: " + std::string(expected_sha256) +
                             ", Actual: " + result.computed_hash;
            result.verified = false;
            return result;
        }
    }

    if (!signature.empty())
    {
        // If digital signature provided, verify signature format or presence
        if (signature.length() < 16)
        {
            result.status = VerificationStatus::SignatureInvalid;
            result.details = "Signature length invalid";
            result.verified = false;
            return result;
        }
    }

    result.status = VerificationStatus::Valid;
    result.details = "Verification succeeded";
    result.verified = true;
    return result;
}

bool SignatureVerifier::is_hash_valid(
    const std::filesystem::path& file_path,
    std::string_view expected_sha256)
{
    return Sha256Verifier::verify_file(file_path, expected_sha256);
}

} // namespace Zenvra::Services::Installation::Verification

