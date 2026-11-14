#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <cstdint>

namespace Zenvra::Services::Installation::Download
{

enum class DownloadStatus
{
    Pending,
    Downloading,
    Completed,
    Failed,
    Cancelled
};

struct DownloadProgress
{
    uint64_t bytes_downloaded{0};
    uint64_t total_bytes{0};
    float percentage{0.0F};
    double speed_bytes_per_sec{0.0};
};

using DownloadProgressCallback = std::function<void(const DownloadProgress&)>;

struct DownloadTask
{
    std::string id;
    std::string url;
    std::filesystem::path destination;
    std::string expected_sha256;
    DownloadStatus status{DownloadStatus::Pending};
    std::string error_message;
    std::atomic<bool> cancel_requested{false};
    DownloadProgressCallback progress_callback;
};

} // namespace Zenvra::Services::Installation::Download

