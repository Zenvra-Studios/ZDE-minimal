#include "DownloadManager.h"
#include "Source/Services/LSP/Process/ProcessSupervisor.h"
#include "Source/Services/Installation/Verification/Sha256Verifier.h"

#include <chrono>
#include <random>
#include <sstream>

namespace Zenvra::Services::Installation::Download
{

namespace
{
std::string generate_task_id()
{
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << "dl-" << std::hex << dist(rng);
    return oss.str();
}
} // namespace

DownloadManager::DownloadManager() = default;
DownloadManager::~DownloadManager() = default;

std::string DownloadManager::queue_download(
    const std::string& url,
    const std::filesystem::path& destination,
    const std::string& expected_sha256,
    DownloadProgressCallback progress_callback)
{
    auto task = std::make_shared<DownloadTask>();
    task->id = generate_task_id();
    task->url = url;
    task->destination = destination;
    task->expected_sha256 = expected_sha256;
    task->progress_callback = std::move(progress_callback);
    task->status = DownloadStatus::Pending;

    std::string task_id = task->id;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks[task_id] = task;
    }

    std::thread([this, task]() {
        execute_download(*task);
    }).detach();

    return task_id;
}

bool DownloadManager::cancel_download(const std::string& task_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tasks.find(task_id);
    if (it != m_tasks.end())
    {
        it->second->cancel_requested = true;
        it->second->status = DownloadStatus::Cancelled;
        return true;
    }
    return false;
}

DownloadStatus DownloadManager::get_status(const std::string& task_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tasks.find(task_id);
    if (it != m_tasks.end())
        return it->second->status;
    return DownloadStatus::Failed;
}

bool DownloadManager::download_sync(
    const std::string& url,
    const std::filesystem::path& destination,
    const std::string& expected_sha256,
    DownloadProgressCallback progress_callback)
{
    DownloadTask task;
    task.id = generate_task_id();
    task.url = url;
    task.destination = destination;
    task.expected_sha256 = expected_sha256;
    task.progress_callback = std::move(progress_callback);

    return execute_download(task);
}

bool DownloadManager::execute_download(DownloadTask& task)
{
    task.status = DownloadStatus::Downloading;

    // Ensure parent directory exists
    std::error_code ec;
    std::filesystem::create_directories(task.destination.parent_path(), ec);

    LSP::Process::ProcessSupervisor supervisor;
    LSP::Process::ProcessSpec spec;

#if defined(_WIN32)
    spec.executable = "curl.exe";
#else
    spec.executable = "curl";
#endif
    spec.arguments = {"-L", "-s", "-f", "-o", task.destination.string(), task.url};

    if (!supervisor.start(spec))
    {
        task.status = DownloadStatus::Failed;
        task.error_message = "Failed to launch curl process";
        return false;
    }

    int exit_code = supervisor.wait_for_exit();
    if (task.cancel_requested)
    {
        task.status = DownloadStatus::Cancelled;
        std::filesystem::remove(task.destination, ec);
        return false;
    }

    if (exit_code != 0 || !std::filesystem::exists(task.destination))
    {
        task.status = DownloadStatus::Failed;
        task.error_message = "Download failed with exit code " + std::to_string(exit_code);
        return false;
    }

    // Verify SHA-256 if provided
    if (!task.expected_sha256.empty())
    {
        if (!Verification::Sha256Verifier::verify_file(task.destination, task.expected_sha256))
        {
            task.status = DownloadStatus::Failed;
            task.error_message = "SHA-256 checksum mismatch";
            std::filesystem::remove(task.destination, ec);
            return false;
        }
    }

    task.status = DownloadStatus::Completed;
    if (task.progress_callback)
    {
        DownloadProgress prog;
        prog.percentage = 100.0F;
        task.progress_callback(prog);
    }
    return true;
}

} // namespace Zenvra::Services::Installation::Download

