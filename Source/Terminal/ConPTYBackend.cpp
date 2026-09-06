#include "Terminal/ConPTYBackend.h"

#if defined(_WIN32)
#include <algorithm>
#include <array>
#include <mutex>

namespace Zenvra::Terminal {

namespace {

using PFN_CreatePseudoConsole = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
using PFN_ResizePseudoConsole = HRESULT(WINAPI *)(HPCON, COORD);
using PFN_ClosePseudoConsole = VOID(WINAPI *)(HPCON);

static PFN_CreatePseudoConsole s_fn_CreatePseudoConsole = nullptr;
static PFN_ResizePseudoConsole s_fn_ResizePseudoConsole = nullptr;
static PFN_ClosePseudoConsole s_fn_ClosePseudoConsole = nullptr;

void load_conpty_api() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32 != nullptr) {
            s_fn_CreatePseudoConsole = reinterpret_cast<PFN_CreatePseudoConsole>(
                GetProcAddress(kernel32, "CreatePseudoConsole"));
            s_fn_ResizePseudoConsole = reinterpret_cast<PFN_ResizePseudoConsole>(
                GetProcAddress(kernel32, "ResizePseudoConsole"));
            s_fn_ClosePseudoConsole = reinterpret_cast<PFN_ClosePseudoConsole>(
                GetProcAddress(kernel32, "ClosePseudoConsole"));
        }
    });
}

std::wstring quote_arg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    if (arg.front() == L'"' && arg.back() == L'"') {
        return arg;
    }
    return L'"' + arg + L'"';
}

} // namespace

ConPTYBackend::ConPTYBackend() = default;

ConPTYBackend::~ConPTYBackend() {
    stop();
}

bool ConPTYBackend::is_supported() noexcept {
    load_conpty_api();
    return s_fn_CreatePseudoConsole != nullptr &&
           s_fn_ResizePseudoConsole != nullptr &&
           s_fn_ClosePseudoConsole != nullptr;
}

bool ConPTYBackend::start(
    const std::filesystem::path& executable,
    const std::wstring& arguments,
    const std::filesystem::path& working_directory,
    std::size_t columns,
    std::size_t rows) {
    stop();

    if (!is_supported()) {
        terminal_debug_log("[ConPTYBackend] ConPTY API is not available on this Windows build.");
        return false;
    }

    HANDLE input_read = nullptr;
    HANDLE output_write = nullptr;

    if (CreatePipe(&input_read, &m_input_write, nullptr, 0) == FALSE ||
        CreatePipe(&m_output_read, &output_write, nullptr, 0) == FALSE) {
        if (input_read != nullptr) CloseHandle(input_read);
        if (output_write != nullptr) CloseHandle(output_write);
        terminal_debug_log("[ConPTYBackend] Failed to create pipes for Pseudoconsole.");
        return false;
    }

    COORD console_size{
        static_cast<SHORT>(std::clamp<std::size_t>(columns, 1, 32767)),
        static_cast<SHORT>(std::clamp<std::size_t>(rows, 1, 32767))};

    HPCON hPC = nullptr;
    const HRESULT hr = s_fn_CreatePseudoConsole(console_size, input_read, output_write, 0, &hPC);
    if (FAILED(hr)) {
        CloseHandle(input_read);
        CloseHandle(output_write);
        stop();
        terminal_debug_log("[ConPTYBackend] CreatePseudoConsole failed (HRESULT 0x" + std::to_string(hr) + ")");
        return false;
    }

    m_pseudo_console = hPC;

    SIZE_T attr_list_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_list_size);
    if (attr_list_size == 0) {
        CloseHandle(input_read);
        CloseHandle(output_write);
        stop();
        return false;
    }

    m_attribute_buffer.resize(attr_list_size);
    m_attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attribute_buffer.data());
    if (InitializeProcThreadAttributeList(m_attribute_list, 1, 0, &attr_list_size) == FALSE) {
        CloseHandle(input_read);
        CloseHandle(output_write);
        stop();
        return false;
    }

    if (UpdateProcThreadAttribute(
            m_attribute_list,
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            m_pseudo_console,
            sizeof(HPCON),
            nullptr,
            nullptr) == FALSE) {
        CloseHandle(input_read);
        CloseHandle(output_write);
        stop();
        return false;
    }

    STARTUPINFOEXW startup_ex{};
    startup_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startup_ex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup_ex.StartupInfo.hStdInput = nullptr;
    startup_ex.StartupInfo.hStdOutput = nullptr;
    startup_ex.StartupInfo.hStdError = nullptr;
    startup_ex.lpAttributeList = m_attribute_list;

    std::wstring full_command = quote_arg(executable.wstring()) + arguments;
    std::vector<wchar_t> mutable_command(full_command.begin(), full_command.end());
    mutable_command.push_back(L'\0');

    std::wstring dir_str = working_directory.wstring();
    const wchar_t* dir_ptr = dir_str.empty() ? nullptr : dir_str.c_str();

    PROCESS_INFORMATION process_info{};
    BOOL created = CreateProcessW(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        dir_ptr,
        &startup_ex.StartupInfo,
        &process_info);

    if (created == FALSE && dir_ptr != nullptr) {
        created = CreateProcessW(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            nullptr,
            &startup_ex.StartupInfo,
            &process_info);
    }

    // Free the pseudo console pipe ends now that the child process has inherited them
    CloseHandle(input_read);
    CloseHandle(output_write);

    if (created == FALSE) {
        const DWORD err = GetLastError();
        terminal_debug_log("[ConPTYBackend] CreateProcessW failed for \"" + executable.string() +
                           "\" (Win32 error " + std::to_string(err) + ")");
        stop();
        return false;
    }

    CloseHandle(process_info.hThread);
    m_process = process_info.hProcess;
    m_process_id = process_info.dwProcessId;

    terminal_debug_log("[ConPTYBackend] CreateProcessW OK (PID " + std::to_string(m_process_id) + ")");

    m_start_time = std::chrono::steady_clock::now();
    m_running = true;
    return true;
}

void ConPTYBackend::stop() noexcept {
    if (m_pseudo_console != nullptr) {
        if (s_fn_ClosePseudoConsole != nullptr) {
            s_fn_ClosePseudoConsole(m_pseudo_console);
        }
        m_pseudo_console = nullptr;
    }
    if (m_attribute_list != nullptr) {
        DeleteProcThreadAttributeList(m_attribute_list);
        m_attribute_list = nullptr;
        m_attribute_buffer.clear();
    }
    if (m_input_write != nullptr) {
        CloseHandle(m_input_write);
        m_input_write = nullptr;
    }
    if (m_output_read != nullptr) {
        CloseHandle(m_output_read);
        m_output_read = nullptr;
    }
    if (m_process != nullptr) {
        if (WaitForSingleObject(m_process, 20) == WAIT_TIMEOUT) {
            TerminateProcess(m_process, 0);
            WaitForSingleObject(m_process, 100);
        }
        CloseHandle(m_process);
        m_process = nullptr;
    }
    m_process_id = 0;
    m_running = false;
}

bool ConPTYBackend::write_input(std::string_view text) {
    if (!m_running || m_input_write == nullptr || text.empty()) {
        return false;
    }
    DWORD bytes_written = 0;
    const BOOL ok = WriteFile(
        m_input_write,
        text.data(),
        static_cast<DWORD>(text.size()),
        &bytes_written,
        nullptr);
    return ok != FALSE && bytes_written == text.size();
}

std::size_t ConPTYBackend::read_output(std::span<char> buffer) {
    if (!m_running || m_output_read == nullptr || buffer.empty()) {
        return 0;
    }
    DWORD available = 0;
    if (PeekNamedPipe(m_output_read, nullptr, 0, nullptr, &available, nullptr) == FALSE || available == 0) {
        return 0;
    }
    DWORD bytes_read = 0;
    const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
    if (ReadFile(m_output_read, buffer.data(), requested, &bytes_read, nullptr) == FALSE || bytes_read == 0) {
        return 0;
    }
    return static_cast<std::size_t>(bytes_read);
}

void ConPTYBackend::resize(std::size_t columns, std::size_t rows) noexcept {
    if (m_pseudo_console != nullptr && s_fn_ResizePseudoConsole != nullptr) {
        COORD console_size{
            static_cast<SHORT>(std::clamp<std::size_t>(columns, 1, 32767)),
            static_cast<SHORT>(std::clamp<std::size_t>(rows, 1, 32767))};
        s_fn_ResizePseudoConsole(m_pseudo_console, console_size);
    }
}

bool ConPTYBackend::is_running() const noexcept {
    return m_running;
}

bool ConPTYBackend::check_exit(uint32_t& out_exit_code) {
    if (m_process == nullptr) {
        out_exit_code = 0;
        return true;
    }
    if (WaitForSingleObject(m_process, 0) == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        GetExitCodeProcess(m_process, &exit_code);
        out_exit_code = exit_code;
        m_running = false;
        return true;
    }
    return false;
}

} // namespace Zenvra::Terminal
#endif
