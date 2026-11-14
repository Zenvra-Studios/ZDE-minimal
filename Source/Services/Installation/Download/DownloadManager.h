#pragma once

#include "DownloadTask.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <future>

namespace Zenvra::Services::Installation::Download
{

class DownloadManager
{
public:
    DownloadManager();
    ~DownloadManager();

    [[nodiscard]] std::string queue_download(
        const std::string& url,
        const std::filesystem::path& destination,
        const std::string& expected_sha256 = {},
        DownloadProgressCallback progress_callback = nullptr);

    bool cancel_download(const std::string& task_id);

    [[nodiscard]] DownloadStatus get_status(const std::string& task_id) const;

    [[nodiscard]] bool download_sync(
        const std::string& url,
        const std::filesystem::path& destination,
        const std::string& expected_sha256 = {},
        DownloadProgressCallback progress_callback = nullptr);

private:
    bool execute_download(DownloadTask& task);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<DownloadTask>> m_tasks;
};

} // namespace Zenvra::Services::Installation::Download

