#include "TerminalSession.h"
#include "TerminalExitDecoder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>


#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Zenvra::Terminal {

namespace {

constexpr std::size_t maximum_scrollback_lines = 4000;

static std::mutex s_log_mutex;

void terminal_debug_log(std::string_view msg) {
  const char *disable_log = std::getenv("ZDE_TERMINAL_LOG");
  if (disable_log != nullptr &&
      (disable_log[0] == '0' || disable_log[0] == 'n' ||
       disable_log[0] == 'N')) {
    return;
  }

  std::error_code ec;
  std::filesystem::path log_path;
#if defined(_WIN32)
  char tmp_buf[512] = {};
  std::size_t tmp_len = 0;
  if (getenv_s(&tmp_len, tmp_buf, sizeof(tmp_buf), "TEMP") == 0 &&
      tmp_len > 0) {
    log_path = std::filesystem::path(tmp_buf);
  } else {
    log_path = std::filesystem::current_path(ec);
  }
#else
  const char *tmp = std::getenv("TMPDIR");
  log_path =
      tmp ? std::filesystem::path(tmp) : std::filesystem::current_path(ec);
#endif
  log_path /= "zde-terminal.log";

  std::lock_guard<std::mutex> lock(s_log_mutex);
  if (std::filesystem::exists(log_path, ec)) {
    const auto sz = std::filesystem::file_size(log_path, ec);
    if (!ec && sz > 1024 * 1024) {
      std::ofstream trunc_out(log_path, std::ios::trunc);
    }
  }

  std::ofstream out(log_path, std::ios::app);
  if (out) {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    out << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "."
        << std::setfill('0') << std::setw(3) << ms.count() << "] " << msg
        << '\n';
  }
}

bool is_printable_input(std::string_view text) noexcept {
  return !text.empty() && static_cast<unsigned char>(text.front()) >= 0x20U &&
         text.front() != '\x7F';
}

void remove_last_utf8_code_point(std::string &text) noexcept {
  if (text.empty()) {
    return;
  }
  text.pop_back();
  while (!text.empty() &&
         (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
    text.pop_back();
  }
}

#if defined(_WIN32)
static std::mutex s_shell_state_mutex;
static std::unordered_set<std::string> s_dead_shell_blacklist;
static bool s_conpty_permanently_failed = false;

static std::string normalize_shell_key(const std::filesystem::path &path) {
  std::string s = path.string();
  for (char &c : s) {
    if (c == '/')
      c = '\\';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

void mark_shell_dead(const std::filesystem::path &path) {
  std::lock_guard<std::mutex> lock(s_shell_state_mutex);
  s_dead_shell_blacklist.insert(normalize_shell_key(path));
}

bool is_shell_blacklisted(const std::filesystem::path &path) {
  std::lock_guard<std::mutex> lock(s_shell_state_mutex);
  return s_dead_shell_blacklist.find(normalize_shell_key(path)) !=
         s_dead_shell_blacklist.end();
}

std::filesystem::path get_last_good_shell_path() {
  std::error_code ec;
  std::filesystem::path state_path;
  std::array<wchar_t, 32768> local_app_data{};
  const DWORD local_length =
      GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(),
                              static_cast<DWORD>(local_app_data.size()));
  if (local_length > 0 && local_length < local_app_data.size()) {
    state_path = std::filesystem::path(local_app_data.data()) / L"ZDE";
    std::filesystem::create_directories(state_path, ec);
  } else {
    char tmp_buf[512] = {};
    std::size_t tmp_len = 0;
    if (getenv_s(&tmp_len, tmp_buf, sizeof(tmp_buf), "TEMP") == 0 &&
        tmp_len > 0) {
      state_path = std::filesystem::path(tmp_buf);
    }
  }
  state_path /= "terminal_last_good_shell.txt";
  return state_path;
}

std::filesystem::path load_last_good_shell() {
  const std::filesystem::path state_file = get_last_good_shell_path();
  std::ifstream in(state_file);
  if (!in)
    return {};
  std::string line;
  if (std::getline(in, line)) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!line.empty()) {
      std::string lower = line;
      for (char &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lower.find("syswow64") != std::string::npos ||
          lower.find("cmd.exe") != std::string::npos) {
        return {};
      }
      std::filesystem::path candidate(line);
      return candidate;
    }
  }
  return {};
}

void save_last_good_shell(const std::filesystem::path &path) {
  if (path.empty())
    return;
  std::string lower = path.string();
  for (char &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower.find("syswow64") != std::string::npos ||
      lower.find("cmd.exe") != std::string::npos)
    return;

  const std::filesystem::path state_file = get_last_good_shell_path();
  std::ofstream out(state_file, std::ios::trunc);
  if (out) {
    out << path.string() << "\n";
  }
}

std::wstring quote_windows_argument(const std::wstring &argument) {
  return L'"' + argument + L'"';
}

std::filesystem::path find_windows_executable(const wchar_t *executable) {
  std::array<wchar_t, 32768> resolved{};
  const DWORD length = SearchPathW(nullptr, executable, nullptr,
                                   static_cast<DWORD>(resolved.size()),
                                   resolved.data(), nullptr);
  return length > 0 && length < resolved.size()
             ? std::filesystem::path{resolved.data()}
             : std::filesystem::path{};
}

bool is_windows_shell(const std::filesystem::path &path,
                      const wchar_t *filename) {
  const std::wstring actual = path.filename().wstring();
  return CompareStringOrdinal(actual.c_str(), -1, filename, -1, TRUE) ==
         CSTR_EQUAL;
}


DWORD get_liveness_timeout_ms([[maybe_unused]] const std::filesystem::path &shell_path) {
  if (const char *env_timeout = std::getenv("ZDE_LIVENESS_MS");
      env_timeout != nullptr && env_timeout[0] != '\0') {
    const unsigned long custom_ms = std::stoul(env_timeout);
    if (custom_ms > 0 && custom_ms <= 30000) {
      return static_cast<DWORD>(custom_ms);
    }
  }
  return 2500;
}

std::wstring windows_shell_arguments(const std::filesystem::path &shell_path) {
  if (is_windows_shell(shell_path, L"bash.exe")) {
    return L" --noprofile --norc -i";
  }
  if (is_windows_shell(shell_path, L"cmd.exe")) {
    return L" /d /q";
  }
  return L" -NoLogo -NoExit";
}

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#ifndef HPCON
DECLARE_HANDLE(HPCON);
#endif

using PFN_CreatePseudoConsole = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD,
                                                  HPCON *);
using PFN_ResizePseudoConsole = HRESULT(WINAPI *)(HPCON, COORD);
using PFN_ClosePseudoConsole = VOID(WINAPI *)(HPCON);

static PFN_CreatePseudoConsole s_fn_CreatePseudoConsole = nullptr;
static PFN_ResizePseudoConsole s_fn_ResizePseudoConsole = nullptr;
static PFN_ClosePseudoConsole s_fn_ClosePseudoConsole = nullptr;

void load_conpty_api() {
  static std::once_flag flag;
  std::call_once(flag, []() {
    if (s_conpty_permanently_failed) {
      return;
    }

    if (const char *no_conpty = std::getenv("ZDE_NO_CONPTY");
        no_conpty != nullptr &&
        (no_conpty[0] == '1' || no_conpty[0] == 'y' || no_conpty[0] == 'Y')) {
      terminal_debug_log(
          "[ZDE Terminal] ConPTY disabled via ZDE_NO_CONPTY, using pipe mode");
      return;
    }

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

  if (s_conpty_permanently_failed) {
    s_fn_CreatePseudoConsole = nullptr;
    s_fn_ResizePseudoConsole = nullptr;
    s_fn_ClosePseudoConsole = nullptr;
  }
}
#endif

} // namespace

struct TerminalSession::Implementation {
#if defined(_WIN32)
  HANDLE process = nullptr;
  HANDLE input_write = nullptr;
  HANDLE output_read = nullptr;
  HPCON pseudo_console = nullptr;
  LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = nullptr;
  std::vector<BYTE> attribute_buffer;
  bool is_conpty = false;
  std::chrono::steady_clock::time_point start_time{};
  bool persisted_last_good = false;
  unsigned recovery_attempts = 0;
#else
  int master_fd = -1;
  pid_t process_id = -1;
#endif
};

TerminalSession::TerminalSession()
    : m_implementation(std::make_unique<Implementation>()) {}

TerminalSession::~TerminalSession() { stop(); }

#if defined(_WIN32)
bool is_valid_executable_file(const std::filesystem::path &path) {
  if (path.empty()) {
    return false;
  }
  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES ||
      (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return false;
  }
  return true;
}

std::vector<std::filesystem::path> resolve_host_shell_candidates() {
  std::vector<std::filesystem::path> candidates;

  auto add_candidate = [&](const std::filesystem::path &p) {
    if (!p.empty() && is_valid_executable_file(p)) {
      for (const auto &existing : candidates) {
        if (existing == p) {
          return;
        }
      }
      candidates.push_back(p);
    }
  };

  // 0. Explicit ZDE override ($ZDE_SHELL)
  if (const char *zde_shell = std::getenv("ZDE_SHELL");
      zde_shell != nullptr && zde_shell[0] != '\0') {
    add_candidate(zde_shell);
  }

  // 1. TOP PRIORITY: PowerShell 7+ (pwsh.exe) in standard installation
  // directories
  constexpr std::array<const wchar_t *, 3> standard_pwsh_paths{
      L"C:\\Program Files\\PowerShell\\7\\pwsh.exe",
      L"C:\\Program Files\\PowerShell\\7-preview\\pwsh.exe",
      L"C:\\Program Files (x86)\\PowerShell\\7\\pwsh.exe",
  };
  for (const wchar_t *path : standard_pwsh_paths) {
    add_candidate(path);
  }

  // Check %LOCALAPPDATA%\Programs\PowerShell\7\pwsh.exe (user install)
  std::array<wchar_t, 32768> local_app_data{};
  const DWORD local_length =
      GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(),
                              static_cast<DWORD>(local_app_data.size()));
  if (local_length > 0 && local_length < local_app_data.size()) {
    const std::filesystem::path user_pwsh =
        std::filesystem::path(local_app_data.data()) / L"Programs" /
        L"PowerShell" / L"7" / L"pwsh.exe";
    add_candidate(user_pwsh);
  }

  // Check pwsh.exe in PATH
  if (const std::filesystem::path pwsh = find_windows_executable(L"pwsh.exe");
      !pwsh.empty()) {
    add_candidate(pwsh);
  }

  // 2. Windows PowerShell 5.1 (System32 built-in 64-bit)
  add_candidate(
      L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");

  if (const std::filesystem::path ps =
          find_windows_executable(L"powershell.exe");
      !ps.empty()) {
    add_candidate(ps);
  }

  // Fallback: guarantee native System32 powershell
  if (candidates.empty()) {
    candidates.emplace_back(
        L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
  }

  return candidates;
}
#else
std::vector<std::filesystem::path> resolve_host_shell_candidates() {
  std::vector<std::filesystem::path> candidates;

  // 0. Check explicit ZDE override ($ZDE_SHELL)
  if (const char *zde_shell = std::getenv("ZDE_SHELL");
      zde_shell != nullptr && zde_shell[0] != '\0') {
    if (::access(zde_shell, X_OK) == 0) {
      candidates.emplace_back(zde_shell);
    }
  }

  // 1. Check environment variable $SHELL (host's active shell preference)
  if (const char *env_shell = std::getenv("SHELL");
      env_shell != nullptr && env_shell[0] != '\0') {
    if (::access(env_shell, X_OK) == 0) {
      candidates.emplace_back(env_shell);
    }
  }

  // 2. Check user's default login shell in passwd database
  const uid_t uid = ::getuid();
  if (const struct passwd *pw = ::getpwuid(uid);
      pw != nullptr && pw->pw_shell != nullptr && pw->pw_shell[0] != '\0') {
    if (::access(pw->pw_shell, X_OK) == 0) {
      candidates.emplace_back(pw->pw_shell);
    }
  }

  // 3. Fallback candidates by platform priority
#if defined(__APPLE__)
  constexpr std::array<std::string_view, 8> list{
      "/bin/zsh",
      "/usr/bin/zsh",
      "/opt/homebrew/bin/zsh",
      "/opt/homebrew/bin/fish",
      "/usr/local/bin/zsh",
      "/usr/local/bin/fish",
      "/bin/bash",
      "/bin/sh",
  };
#else
  constexpr std::array<std::string_view, 8> list{
      "/usr/bin/bash", "/bin/bash", "/usr/bin/zsh",        "/bin/zsh",
      "/usr/bin/fish", "/bin/fish", "/usr/local/bin/fish", "/bin/sh",
  };
#endif
  for (const std::string_view candidate : list) {
    if (::access(std::string{candidate}.c_str(), X_OK) == 0) {
      candidates.emplace_back(candidate);
    }
  }

  // Remove duplicates
  std::vector<std::filesystem::path> unique_candidates;
  for (const auto &p : candidates) {
    if (std::find(unique_candidates.begin(), unique_candidates.end(), p) ==
        unique_candidates.end()) {
      unique_candidates.push_back(p);
    }
  }
  return unique_candidates;
}
#endif

std::filesystem::path TerminalSession::resolve_host_shell() {
  const auto candidates = resolve_host_shell_candidates();
  return candidates.empty() ? std::filesystem::path{} : candidates.front();
}

bool TerminalSession::start(const std::filesystem::path &working_directory,
                            std::size_t initial_columns,
                            std::size_t initial_rows) {
  stop();
  m_lines.assign(1, std::string{});
  m_cursor_line = 0;
  m_cursor_column = 0;
  m_saved_cursor_line = 0;
  m_saved_cursor_column = 0;
  m_input_start_column = 0;
  m_pending_input.clear();
  m_command_history.clear();
  m_history_index.reset();
  m_saved_pending_input.clear();
  m_parser_state = ParserState::Text;
  m_control_sequence.clear();
  m_columns = std::clamp<std::size_t>(initial_columns, 40, 65535);
  m_rows = std::clamp<std::size_t>(initial_rows, 10, 65535);

  const auto shell_candidates = resolve_host_shell_candidates();
  if (shell_candidates.empty()) {
    m_shell_path = "Terminal";
    append_status("[Unable to find a local shell executable]");
    return true;
  }

#if defined(_WIN32)
  _wputenv_s(L"COLUMNS", std::to_wstring(m_columns).c_str());
  _wputenv_s(L"LINES", std::to_wstring(m_rows).c_str());
  _wputenv_s(L"TERM", L"xterm-256color");
  _wputenv_s(L"COLORTERM", L"truecolor");
  load_conpty_api();

  m_working_directory = working_directory;

  std::wstring directory_str;
  std::error_code dir_ec;
  if (!working_directory.empty() &&
      std::filesystem::is_directory(working_directory, dir_ec)) {
    directory_str = working_directory.wstring();
  } else {
    std::array<wchar_t, 32768> user_profile{};
    const DWORD profile_length =
        GetEnvironmentVariableW(L"USERPROFILE", user_profile.data(),
                                static_cast<DWORD>(user_profile.size()));
    if (profile_length > 0 && profile_length < user_profile.size()) {
      if (std::filesystem::is_directory(user_profile.data(), dir_ec)) {
        directory_str = user_profile.data();
      }
    }
  }
  const wchar_t *directory_pointer =
      directory_str.empty() ? nullptr : directory_str.c_str();

  bool started = false;
  std::size_t candidate_index = 0;

  terminal_debug_log("[ZDE Terminal] Starting session. Resolved " +
                     std::to_string(shell_candidates.size()) +
                     " candidate(s):");
  for (const auto &cand : shell_candidates) {
    terminal_debug_log("  - Candidate: \"" + cand.string() + "\"");
  }

  for (const auto &candidate_path : shell_candidates) {
    m_shell_path = candidate_path;
    const DWORD liveness_timeout = get_liveness_timeout_ms(candidate_path);

    terminal_debug_log("[ZDE Terminal] Attempting candidate #" +
                       std::to_string(candidate_index++) + ": \"" +
                       candidate_path.string() + "\" (liveness timeout " +
                       std::to_string(liveness_timeout) + "ms)");

    std::wstring command = quote_windows_argument(candidate_path.wstring()) +
                           windows_shell_arguments(candidate_path);

    bool candidate_started = false;

    // Step 1: Attempt ConPTY if API is available and not disabled
    if (s_fn_CreatePseudoConsole != nullptr &&
        s_fn_ResizePseudoConsole != nullptr &&
        s_fn_ClosePseudoConsole != nullptr) {
      HANDLE input_read = nullptr;
      HANDLE output_write = nullptr;

      if (CreatePipe(&input_read, &m_implementation->input_write, nullptr, 0) !=
              FALSE &&
          CreatePipe(&m_implementation->output_read, &output_write, nullptr,
                     0) != FALSE) {
        COORD console_size{
            static_cast<SHORT>(std::clamp<std::size_t>(m_columns, 1, 32767)),
            static_cast<SHORT>(std::clamp<std::size_t>(m_rows, 1, 32767))};
        HPCON hPC = nullptr;
        const HRESULT create_pcon_hr = s_fn_CreatePseudoConsole(
            console_size, input_read, output_write, 0, &hPC);
        if (SUCCEEDED(create_pcon_hr)) {
          m_implementation->pseudo_console = hPC;

          SIZE_T attr_list_size = 0;
          InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_list_size);
          if (attr_list_size > 0) {
            m_implementation->attribute_buffer.resize(attr_list_size);
            m_implementation->attribute_list =
                reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                    m_implementation->attribute_buffer.data());
            if (InitializeProcThreadAttributeList(
                    m_implementation->attribute_list, 1, 0, &attr_list_size) !=
                FALSE) {
              if (UpdateProcThreadAttribute(m_implementation->attribute_list, 0,
                                            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                            m_implementation->pseudo_console,
                                            sizeof(HPCON), nullptr,
                                            nullptr) != FALSE) {
                STARTUPINFOEXW startup_ex{};
                startup_ex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
                startup_ex.lpAttributeList = m_implementation->attribute_list;

                PROCESS_INFORMATION process_info{};
                std::vector<wchar_t> mutable_command(command.begin(),
                                                     command.end());
                mutable_command.push_back(L'\0');

                BOOL created = CreateProcessW(
                    nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                    EXTENDED_STARTUPINFO_PRESENT, nullptr, directory_pointer,
                    &startup_ex.StartupInfo, &process_info);

                if (created == FALSE && directory_pointer != nullptr) {
                  created = CreateProcessW(
                      nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                      EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                      &startup_ex.StartupInfo, &process_info);
                }

                // Close the PTY pipe ends only after child process creation
                CloseHandle(input_read);
                input_read = nullptr;
                CloseHandle(output_write);
                output_write = nullptr;

                if (created != FALSE) {
                  CloseHandle(process_info.hThread);
                  m_implementation->process = process_info.hProcess;
                  const DWORD pid = process_info.dwProcessId;
                  terminal_debug_log(
                      "[ZDE Terminal] ConPTY CreateProcessW OK (PID " +
                      std::to_string(pid) + ")");

                  // Instantaneous non-blocking launch check (0ms)
                  if (WaitForSingleObject(m_implementation->process, 0) ==
                      WAIT_OBJECT_0) {
                    DWORD early_exit_code = 0;
                    GetExitCodeProcess(m_implementation->process,
                                       &early_exit_code);
                    terminal_debug_log(
                        "[ZDE Terminal] ConPTY shell failed at launch for \"" +
                        candidate_path.string() + "\" (exit code " +
                        std::to_string(early_exit_code) + ")");
                    CloseHandle(m_implementation->process);
                    m_implementation->process = nullptr;
                  } else {
                    candidate_started = true;
                    m_implementation->is_conpty = true;
                    m_implementation->start_time =
                        std::chrono::steady_clock::now();
                    m_implementation->persisted_last_good = false;
                    m_running = true;
                    terminal_debug_log(
                        "[ZDE Terminal] Shell committed (ConPTY mode) for \"" +
                        candidate_path.string() + "\"");
                  }

                  DeleteProcThreadAttributeList(
                      m_implementation->attribute_list);
                  m_implementation->attribute_list = nullptr;
                  m_implementation->attribute_buffer.clear();
                } else {
                  const DWORD conpty_error = GetLastError();
                  terminal_debug_log(
                      "[ZDE Terminal] ConPTY CreateProcessW failed for \"" +
                      candidate_path.string() + "\" (Win32 error " +
                      std::to_string(conpty_error) + ")");
                }
              }
            }
          }

          if (!candidate_started) {
            if (s_fn_ClosePseudoConsole != nullptr &&
                m_implementation->pseudo_console != nullptr) {
              s_fn_ClosePseudoConsole(m_implementation->pseudo_console);
              m_implementation->pseudo_console = nullptr;
            }
            if (m_implementation->attribute_list != nullptr) {
              DeleteProcThreadAttributeList(m_implementation->attribute_list);
              m_implementation->attribute_list = nullptr;
            }
            m_implementation->attribute_buffer.clear();
          }
        } else {
          terminal_debug_log(
              "[ZDE Terminal] CreatePseudoConsole failed (HRESULT 0x" +
              std::to_string(create_pcon_hr) + ")");
        }
      }
      if (input_read != nullptr) {
        CloseHandle(input_read);
        input_read = nullptr;
      }
      if (output_write != nullptr) {
        CloseHandle(output_write);
        output_write = nullptr;
      }
    }

    if (candidate_started) {
      started = true;
      break;
    }

    // Step 2: ConPTY failed or shell died in ConPTY -> Attempt Pipe mode
    // fallback for this candidate
    terminal_debug_log("[ZDE Terminal] ConPTY unavailable/failed for \"" +
                       candidate_path.string() +
                       "\", falling back to Pipe mode...");
    if (m_implementation->input_write != nullptr) {
      CloseHandle(m_implementation->input_write);
      m_implementation->input_write = nullptr;
    }
    if (m_implementation->output_read != nullptr) {
      CloseHandle(m_implementation->output_read);
      m_implementation->output_read = nullptr;
    }

    HANDLE pipe_in_read = nullptr;
    HANDLE pipe_out_write = nullptr;
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    if (CreatePipe(&pipe_in_read, &m_implementation->input_write,
                   &security_attributes, 0) != FALSE &&
        CreatePipe(&m_implementation->output_read, &pipe_out_write,
                   &security_attributes, 0) != FALSE) {
      SetHandleInformation(m_implementation->input_write, HANDLE_FLAG_INHERIT,
                           0);
      SetHandleInformation(m_implementation->output_read, HANDLE_FLAG_INHERIT,
                           0);
      SetHandleInformation(pipe_in_read, HANDLE_FLAG_INHERIT,
                           HANDLE_FLAG_INHERIT);
      SetHandleInformation(pipe_out_write, HANDLE_FLAG_INHERIT,
                           HANDLE_FLAG_INHERIT);

      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      startup.wShowWindow = SW_HIDE;
      startup.hStdInput = pipe_in_read;
      startup.hStdOutput = pipe_out_write;
      startup.hStdError = pipe_out_write;

      STARTUPINFOW startup_info{};
      startup_info.cb = sizeof(startup_info);
      startup_info.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      startup_info.wShowWindow = SW_HIDE;
      startup_info.hStdInput = pipe_in_read;
      startup_info.hStdOutput = pipe_out_write;
      startup_info.hStdError = pipe_out_write;

      PROCESS_INFORMATION process_info{};
      std::vector<wchar_t> mutable_command(command.begin(), command.end());
      mutable_command.push_back(L'\0');

      BOOL created =
          CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr,
                         TRUE, CREATE_NO_WINDOW, nullptr, directory_pointer,
                         &startup_info, &process_info);

      if (created == FALSE && directory_pointer != nullptr) {
        created = CreateProcessW(nullptr, mutable_command.data(), nullptr,
                                 nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                 nullptr, &startup_info, &process_info);
      }

      if (pipe_in_read != nullptr)
        CloseHandle(pipe_in_read);
      if (pipe_out_write != nullptr)
        CloseHandle(pipe_out_write);

      if (created != FALSE) {
        CloseHandle(process_info.hThread);
        m_implementation->process = process_info.hProcess;
        const DWORD pid = process_info.dwProcessId;
        terminal_debug_log("[ZDE Terminal] Pipe-mode CreateProcessW OK (PID " +
                           std::to_string(pid) + ")");

        // Instantaneous non-blocking launch check (0ms)
        if (WaitForSingleObject(m_implementation->process, 0) ==
            WAIT_OBJECT_0) {
          DWORD early_exit_code = 0;
          GetExitCodeProcess(m_implementation->process, &early_exit_code);
          terminal_debug_log(
              "[ZDE Terminal] Pipe-mode shell failed at launch for \"" +
              candidate_path.string() + "\" (exit code " +
              std::to_string(early_exit_code) + ")");
          CloseHandle(m_implementation->process);
          m_implementation->process = nullptr;
          mark_shell_dead(candidate_path);
        } else {
          m_implementation->is_conpty = false;
          m_implementation->start_time = std::chrono::steady_clock::now();
          m_implementation->persisted_last_good = false;
          m_running = true;
          terminal_debug_log(
              "[ZDE Terminal] Shell committed (Pipe mode) for \"" +
              candidate_path.string() + "\"");
          started = true;
          break;
        }
      } else {
        const DWORD pipe_error = GetLastError();
        terminal_debug_log(
            "[ZDE Terminal] Pipe-mode CreateProcessW failed for \"" +
            candidate_path.string() + "\" (Win32 error " +
            std::to_string(pipe_error) + ")");
        mark_shell_dead(candidate_path);
      }
    } else {
      if (pipe_in_read != nullptr)
        CloseHandle(pipe_in_read);
      if (pipe_out_write != nullptr)
        CloseHandle(pipe_out_write);
    }
  }

  if (!started) {
    stop();
    m_shell_path = "Terminal";
    terminal_debug_log(
        "[ZDE Terminal] Failed to start any local shell process.");
    append_status("[Unable to start a local terminal shell]");
    return true;
  }
#else
  m_shell_path = shell_candidates.front();
  winsize terminal_size{};
  terminal_size.ws_col = static_cast<unsigned short>(m_columns);
  terminal_size.ws_row = static_cast<unsigned short>(m_rows);
  const pid_t process_id =
      ::forkpty(&m_implementation->master_fd, nullptr, nullptr, &terminal_size);
  if (process_id < 0) {
    m_implementation->master_fd = -1;
    append_status("[Unable to create the local terminal PTY]");
    return true;
  }
  if (process_id == 0) {
    if (!working_directory.empty()) {
      static_cast<void>(::chdir(working_directory.c_str()));
    }
    const std::string executable = m_shell_path.string();
    ::setenv("SHELL", executable.c_str(), 1);
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("COLORTERM", "truecolor", 1);
    ::setenv("TERM_PROGRAM", "ZDE", 1);
    ::setenv("TERM_PROGRAM_VERSION", "1.0.0", 1);
    const std::string shell_name = m_shell_path.filename().string();
    if (shell_name == "fish") {
      ::execl(executable.c_str(), executable.c_str(), "-i", nullptr);
    } else if (shell_name == "zsh" || shell_name == "bash") {
      ::execl(executable.c_str(), executable.c_str(), "-i", "-l", nullptr);
    }
    ::execl(executable.c_str(), executable.c_str(), "-i", nullptr);
    ::execl(executable.c_str(), executable.c_str(), nullptr);
    ::_exit(127);
  }
  m_implementation->process_id = process_id;
  const int current_flags = ::fcntl(m_implementation->master_fd, F_GETFL, 0);
  if (current_flags >= 0) {
    static_cast<void>(::fcntl(m_implementation->master_fd, F_SETFL,
                              current_flags | O_NONBLOCK));
  }
#endif
  m_running = true;
  return true;
}

void TerminalSession::stop() noexcept {
  if (!m_implementation) {
    return;
  }
#if defined(_WIN32)
  if (m_implementation->pseudo_console != nullptr) {
    if (s_fn_ClosePseudoConsole != nullptr) {
      s_fn_ClosePseudoConsole(m_implementation->pseudo_console);
    }
    m_implementation->pseudo_console = nullptr;
  }
  if (m_implementation->attribute_list != nullptr) {
    DeleteProcThreadAttributeList(m_implementation->attribute_list);
    m_implementation->attribute_list = nullptr;
    m_implementation->attribute_buffer.clear();
  }
  if (m_implementation->input_write != nullptr) {
    CloseHandle(m_implementation->input_write);
    m_implementation->input_write = nullptr;
  }
  if (m_implementation->output_read != nullptr) {
    CloseHandle(m_implementation->output_read);
    m_implementation->output_read = nullptr;
  }
  if (m_implementation->process != nullptr) {
    if (WaitForSingleObject(m_implementation->process, 20) == WAIT_TIMEOUT) {
      TerminateProcess(m_implementation->process, 0);
      WaitForSingleObject(m_implementation->process, 100);
    }
    CloseHandle(m_implementation->process);
    m_implementation->process = nullptr;
  }
#else
  if (m_implementation->master_fd >= 0) {
    ::close(m_implementation->master_fd);
    m_implementation->master_fd = -1;
  }
  if (m_implementation->process_id > 0) {
    ::kill(m_implementation->process_id, SIGHUP);
    int status = 0;
    const pid_t wait_result =
        ::waitpid(m_implementation->process_id, &status, WNOHANG);
    if (wait_result == 0) {
      ::kill(m_implementation->process_id, SIGTERM);
      for (int i = 0; i < 5; ++i) {
        if (::waitpid(m_implementation->process_id, &status, WNOHANG) != 0) {
          break;
        }
        ::usleep(10000);
      }
      if (::waitpid(m_implementation->process_id, &status, WNOHANG) == 0) {
        ::kill(m_implementation->process_id, SIGKILL);
        ::waitpid(m_implementation->process_id, &status, 0);
      }
    }
    m_implementation->process_id = -1;
  }
#endif
  m_running = false;
}

bool TerminalSession::is_running() const noexcept { return m_running; }

const std::filesystem::path &TerminalSession::get_shell_path() const noexcept {
  return m_shell_path;
}

std::span<const std::string> TerminalSession::get_lines() const noexcept {
  return m_lines;
}

void erase_utf8_from(std::string &line, std::size_t target_col) {
  std::size_t current_col = 0;
  std::size_t byte_pos = 0;
  while (byte_pos < line.size()) {
    if (current_col == target_col) {
      line.erase(byte_pos);
      return;
    }
    const unsigned char c = static_cast<unsigned char>(line[byte_pos]);
    std::size_t char_len = 1;
    if ((c & 0x80) == 0)
      char_len = 1;
    else if ((c & 0xE0) == 0xC0)
      char_len = 2;
    else if ((c & 0xF0) == 0xE0)
      char_len = 3;
    else if ((c & 0xF8) == 0xF0)
      char_len = 4;
    char_len = std::min(char_len, line.size() - byte_pos);
    byte_pos += char_len;
    ++current_col;
  }
}

bool TerminalSession::write_input(std::string_view text) {
  if (text.empty() || !m_running) {
    return false;
  }
  const bool printable = is_printable_input(text);
  const bool is_backspace = text == "\x08" || text == "\x7F" || text == "\b";
  const auto track_input = [&]() {
    if (m_pending_input.empty() && m_input_start_column == 0) {
      m_input_start_column = m_cursor_column;
    }
    if (text == "\r" || text == "\n") {
      if (!m_pending_input.empty()) {
        m_command_history.push_back(m_pending_input);
        m_pending_input.clear();
      }
      m_history_index.reset();
      m_saved_pending_input.clear();
      m_input_start_column = 0;
      return;
    }

    if (is_backspace) {
      if (!m_pending_input.empty()) {
        remove_last_utf8_code_point(m_pending_input);
      }
    } else if (text == "\x17") {
      if (!m_pending_input.empty()) {
        auto it = m_pending_input.rbegin();
        while (it != m_pending_input.rend() && *it == ' ')
          ++it;
        while (it != m_pending_input.rend() && *it != ' ')
          ++it;
        m_pending_input.erase(it.base(), m_pending_input.end());
      }
    } else if (printable) {
      m_pending_input.append(text);
    }
  };

#if defined(_WIN32)
  if (m_implementation->input_write != nullptr) {
    // ─── Pipe mode: local line editing ───────────────────────────────
    // In Pipe mode the shell has no console, so interactive editing
    // characters (backspace, Ctrl+Backspace) crash the shell with
    // ERROR_ACCESS_DENIED (exit code 5).  Buffer all input locally
    // and only send complete lines to the shell on Enter.
    if (!m_implementation->is_conpty) {
      // Enter: send the complete buffered line to the shell
      if (text == "\r" || text == "\n") {
        std::string payload = m_pending_input + "\r\n";
        DWORD bytes_written = 0;
        const BOOL succeeded =
            WriteFile(m_implementation->input_write, payload.data(),
                      static_cast<DWORD>(payload.size()), &bytes_written,
                      nullptr);
        if (succeeded != FALSE && bytes_written == payload.size()) {
          track_input();
        }
        // Advance display to next line for shell output
        if (!m_lines.empty()) {
          m_lines.emplace_back();
          m_cursor_line = m_lines.size() - 1;
          m_cursor_column = 0;
        }
        trim_scrollback();
        return succeeded != FALSE;
      }

      // Backspace: erase one character locally, never send to pipe
      if (is_backspace) {
        if (!m_pending_input.empty()) {
          track_input();
          if (m_cursor_column > 0) {
            --m_cursor_column;
          } else if (m_cursor_line > 0) {
            --m_cursor_line;
            m_cursor_column = m_columns > 0 ? m_columns - 1 : 0;
          }
          if (m_cursor_line < m_lines.size()) {
            erase_utf8_from(m_lines[m_cursor_line], m_cursor_column);
          }
        }
        return true;
      }

      // Ctrl+Backspace (delete word): erase word locally
      if (text == "\x17") {
        if (!m_pending_input.empty()) {
          track_input();
          std::size_t cps = 0;
          for (std::size_t j = 0; j < m_pending_input.size();) {
            const auto uc =
                static_cast<unsigned char>(m_pending_input[j]);
            if ((uc & 0x80) == 0)
              j += 1;
            else if ((uc & 0xE0) == 0xC0)
              j += 2;
            else if ((uc & 0xF0) == 0xE0)
              j += 3;
            else if ((uc & 0xF8) == 0xF0)
              j += 4;
            else
              j += 1;
            ++cps;
          }
          std::size_t total_cols = m_input_start_column + cps;
          if (m_columns > 0) {
            // Recompute line offset from start line
            // Wait, we don't track m_input_start_line.
            // But we know how many columns back we need to go.
            // Actually, for simplicity, just recompute based on m_cursor_column 
            m_cursor_column = total_cols % m_columns;
            // Note: we can't easily jump back multiple lines without start_line.
            // But this handles single line properly.
          } else {
            m_cursor_column = total_cols;
          }
          if (m_cursor_line < m_lines.size()) {
            erase_utf8_from(m_lines[m_cursor_line], m_cursor_column);
          }
        }
        return true;
      }

      // Printable characters: buffer + display locally
      if (printable) {
        track_input();
        append_codepoint(text);
        return true;
      }

      // Tab: no completion available in pipe mode
      if (text == "\t") {
        return true;
      }

      // Other control characters (Ctrl+C, escape seqs, etc.):
      // send directly to the pipe
      std::string payload(text);
      DWORD bytes_written = 0;
      const BOOL succeeded =
          WriteFile(m_implementation->input_write, payload.data(),
                    static_cast<DWORD>(payload.size()), &bytes_written,
                    nullptr);
      return succeeded != FALSE;
    }

    // ─── ConPTY mode: pass-through to pseudoconsole ──────────────────
    std::string payload(text);
    if (is_backspace) {
      payload = "\x7F";
    }

    DWORD bytes_written = 0;
    const BOOL succeeded =
        WriteFile(m_implementation->input_write, payload.data(),
                  static_cast<DWORD>(payload.size()), &bytes_written, nullptr);

    if (succeeded != FALSE && bytes_written == payload.size()) {
      track_input();
    }
    return succeeded != FALSE;
  }
  return false;
#else
  std::size_t total_written = 0;
  while (total_written < text.size()) {
    const ssize_t written =
        ::write(m_implementation->master_fd, text.data() + total_written,
                text.size() - total_written);
    if (written > 0) {
      total_written += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  const bool succeeded = total_written == text.size();
  if (succeeded) {
    track_input();
  }
  return succeeded;
#endif
}

bool TerminalSession::poll() {
  if (!m_running) {
    return false;
  }
  bool changed = false;
  std::array<char, 8192> buffer{};
#if defined(_WIN32)
  for (;;) {
    DWORD available = 0;
    if (m_implementation->output_read == nullptr ||
        PeekNamedPipe(m_implementation->output_read, nullptr, 0, nullptr,
                      &available, nullptr) == FALSE ||
        available == 0) {
      break;
    }
    DWORD bytes_read = 0;
    const DWORD requested =
        std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
    if (ReadFile(m_implementation->output_read, buffer.data(), requested,
                 &bytes_read, nullptr) == FALSE ||
        bytes_read == 0) {
      break;
    }
    consume_output(std::string_view{buffer.data(), bytes_read});
    changed = true;
  }

  if (m_implementation->process != nullptr &&
      !m_implementation->persisted_last_good) {
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_implementation->start_time)
            .count();
    if (elapsed_ms >= 3000) {
      m_implementation->persisted_last_good = true;
      save_last_good_shell(m_shell_path);
      terminal_debug_log("[ZDE Terminal] Shell healthy for >=3s, saved to "
                         "persistent last_good_shell: \"" +
                         m_shell_path.string() + "\"");
    }
  }

  if (m_implementation->process != nullptr &&
      WaitForSingleObject(m_implementation->process, 0) == WAIT_OBJECT_0) {
    DWORD exit_code = 0;
    GetExitCodeProcess(m_implementation->process, &exit_code);
    m_running = false;

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_implementation->start_time)
            .count();

    // Automatic self-healing: if shell crashed in early startup (<5000ms)
    // without user interaction
    constexpr unsigned k_max_recovery_attempts = 2;
    if (elapsed_ms < 5000 && m_command_history.empty() &&
        m_pending_input.empty() && exit_code != 0 && exit_code != 0xC0000B5B &&
        m_implementation->recovery_attempts < k_max_recovery_attempts) {
      ++m_implementation->recovery_attempts;
      terminal_debug_log(
          "[ZDE Terminal] Early startup crash for \"" + m_shell_path.string() +
          "\" (code " + std::to_string(exit_code) + " at " +
          std::to_string(elapsed_ms) + "ms). Switching to Pipe mode...");
      s_conpty_permanently_failed = true;
      const auto recovery_working_dir = m_working_directory;
      const auto cols = m_columns;
      const auto rows = m_rows;
      const auto attempts = m_implementation->recovery_attempts;
      stop();

      if (start(recovery_working_dir, cols, rows)) {
        m_implementation->recovery_attempts = attempts;
        terminal_debug_log("[ZDE Terminal] Self-healing succeeded with \"" +
                           m_shell_path.string() + "\" (Pipe mode)");
        return true;
      }
    }

    const auto exit_info = decode_terminal_exit(exit_code, m_shell_path,
                                                m_implementation->is_conpty);
    terminal_debug_log("[ZDE Terminal] Process exited with code " +
                       std::to_string(exit_code) +
                       " (Hex: " + exit_info.hex_code +
                       ", Summary: " + exit_info.summary + ")");
    append_status(exit_info.formatted_message);
    changed = true;
  }
#else
  for (;;) {
    const ssize_t bytes_read =
        ::read(m_implementation->master_fd, buffer.data(), buffer.size());
    if (bytes_read > 0) {
      consume_output(std::string_view{buffer.data(),
                                      static_cast<std::size_t>(bytes_read)});
      changed = true;
      continue;
    }
    if (bytes_read < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  int status = 0;
  const pid_t wait_result =
      ::waitpid(m_implementation->process_id, &status, WNOHANG);
  if (wait_result == m_implementation->process_id) {
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    m_running = false;
    m_implementation->process_id = -1;

    std::string exit_message =
        "[Process exited with code " + std::to_string(exit_code) + "]";
    if (WIFSIGNALED(status)) {
      exit_message =
          "[Process killed by signal " + std::to_string(WTERMSIG(status)) + "]";
    }
    append_status(exit_message);
    changed = true;
  }
#endif
  return changed;
}

void TerminalSession::resize(std::size_t columns, std::size_t rows) noexcept {
  columns = std::clamp<std::size_t>(columns, 1, 65535);
  rows = std::clamp<std::size_t>(rows, 1, 65535);
  if (columns == m_columns && rows == m_rows) {
    return;
  }
  m_columns = columns;
  m_rows = rows;
  if (m_in_alternate_screen) {
    m_lines.resize(m_rows);
    if (m_cursor_line >= m_rows) {
      m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
    }
  }
#if !defined(_WIN32)
  if (m_implementation->master_fd >= 0) {
    winsize size{};
    size.ws_col = static_cast<unsigned short>(columns);
    size.ws_row = static_cast<unsigned short>(rows);
    static_cast<void>(::ioctl(m_implementation->master_fd, TIOCSWINSZ, &size));
  }
#else
  if (m_implementation->pseudo_console != nullptr &&
      s_fn_ResizePseudoConsole != nullptr) {
    COORD console_size{
        static_cast<SHORT>(std::clamp<std::size_t>(columns, 1, 32767)),
        static_cast<SHORT>(std::clamp<std::size_t>(rows, 1, 32767))};
    s_fn_ResizePseudoConsole(m_implementation->pseudo_console, console_size);
  }
  _wputenv_s(L"COLUMNS", std::to_wstring(columns).c_str());
  _wputenv_s(L"LINES", std::to_wstring(rows).c_str());
#endif
}

static std::vector<std::string> split_utf8_codepoints(std::string_view s) {
  std::vector<std::string> cps;
  for (std::size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    if ((c & 0x80) == 0)
      len = 1;
    else if ((c & 0xE0) == 0xC0)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0)
      len = 4;

    len = std::min(len, s.size() - i);
    cps.emplace_back(s.substr(i, len));
    i += len;
  }
  return cps;
}

static void set_utf8_cell(std::string &line, std::size_t target_col,
                          std::string_view utf8_char) {
  std::size_t current_col = 0;
  std::size_t byte_pos = 0;
  std::size_t replace_byte_start = std::string::npos;
  std::size_t replace_byte_len = 0;

  while (byte_pos < line.size()) {
    if (current_col == target_col) {
      replace_byte_start = byte_pos;
      const unsigned char c = static_cast<unsigned char>(line[byte_pos]);
      if ((c & 0x80) == 0)
        replace_byte_len = 1;
      else if ((c & 0xE0) == 0xC0)
        replace_byte_len = 2;
      else if ((c & 0xF0) == 0xE0)
        replace_byte_len = 3;
      else if ((c & 0xF8) == 0xF0)
        replace_byte_len = 4;
      else
        replace_byte_len = 1;
      replace_byte_len = std::min(replace_byte_len, line.size() - byte_pos);
      break;
    }

    const unsigned char c = static_cast<unsigned char>(line[byte_pos]);
    std::size_t char_len = 1;
    if ((c & 0x80) == 0)
      char_len = 1;
    else if ((c & 0xE0) == 0xC0)
      char_len = 2;
    else if ((c & 0xF0) == 0xE0)
      char_len = 3;
    else if ((c & 0xF8) == 0xF0)
      char_len = 4;
    char_len = std::min(char_len, line.size() - byte_pos);
    byte_pos += char_len;
    ++current_col;
  }

  if (replace_byte_start != std::string::npos) {
    line.replace(replace_byte_start, replace_byte_len, utf8_char);
  } else {
    if (target_col > current_col) {
      line.append(target_col - current_col, ' ');
    }
    line.append(utf8_char);
  }
}

void TerminalSession::consume_output(std::string_view output) {
  for (std::size_t i = 0; i < output.size(); ++i) {
    const char character = output[i];

    if (m_parser_state == ParserState::Text) {
      if (m_utf8_expected > 0) {
        if ((static_cast<unsigned char>(character) & 0xC0) == 0x80) {
          m_utf8_sequence.push_back(character);
          --m_utf8_expected;
          if (m_utf8_expected == 0) {
            append_codepoint(m_utf8_sequence);
            m_utf8_sequence.clear();
          }
        } else {
          m_utf8_sequence.clear();
          m_utf8_expected = 0;
        }
        continue;
      }

      if (static_cast<unsigned char>(character) >= 0x80U) {
        const unsigned char uc = static_cast<unsigned char>(character);
        if ((uc & 0xE0) == 0xC0) {
          m_utf8_sequence = character;
          m_utf8_expected = 1;
        } else if ((uc & 0xF0) == 0xE0) {
          m_utf8_sequence = character;
          m_utf8_expected = 2;
        } else if ((uc & 0xF8) == 0xF0) {
          m_utf8_sequence = character;
          m_utf8_expected = 3;
        }
        continue;
      }
    }

    switch (m_parser_state) {
    case ParserState::Text:
      if (character == '\x1B') {
        m_utf8_sequence.clear();
        m_utf8_expected = 0;
        m_parser_state = ParserState::Escape;
      } else if (character == '\n') {
        if (m_in_alternate_screen) {
          if (m_cursor_line + 1 < m_rows) {
            ++m_cursor_line;
            while (m_cursor_line >= m_lines.size()) {
              m_lines.emplace_back();
            }
          } else {
            if (!m_lines.empty()) {
              m_lines.erase(m_lines.begin());
              m_lines.emplace_back();
            }
            m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
          }
        } else {
          if (m_cursor_line + 1 < m_lines.size()) {
            ++m_cursor_line;
          } else {
            m_lines.emplace_back();
            m_cursor_line = m_lines.size() - 1;
          }
          trim_scrollback();
        }
      } else if (character == '\r') {
        m_cursor_column = 0;
      } else if (character == '\b' || character == '\x7F') {
        if (m_cursor_column > 0) {
          --m_cursor_column;
        }
      } else if (character == '\t') {
        const std::size_t count = 4 - (m_cursor_column % 4);
        for (std::size_t index = 0; index < count; ++index) {
          append_codepoint(" ");
        }
      } else if (static_cast<unsigned char>(character) >= 0x20U &&
                 character != '\x7F') {
        append_codepoint(std::string_view(&character, 1));
      }
      break;

    case ParserState::Escape:
      if (character == '[') {
        m_control_sequence.clear();
        m_parser_state = ParserState::ControlSequence;
      } else if (character == ']') {
        m_parser_state = ParserState::OperatingSystemCommand;
      } else if (character == 'P' || character == '_' || character == '^') {
        m_parser_state = ParserState::DeviceControlString;
      } else if (character == '(' || character == ')' || character == '*' ||
                 character == '+') {
        m_parser_state = ParserState::DesignateCharacterSet;
      } else if (character == '7') {
        m_saved_cursor_line = m_cursor_line;
        m_saved_cursor_column = m_cursor_column;
        m_parser_state = ParserState::Text;
      } else if (character == '8') {
        m_cursor_line = std::min(m_saved_cursor_line,
                                 m_lines.empty() ? 0 : m_lines.size() - 1);
        m_cursor_column = m_saved_cursor_column;
        m_parser_state = ParserState::Text;
      } else if (character == 'M') {
        // Reverse Index
        if (m_cursor_line > 0) {
          --m_cursor_line;
        } else {
          m_lines.insert(m_lines.begin(), std::string{});
          if (m_in_alternate_screen && m_lines.size() > m_rows) {
            m_lines.pop_back();
          }
        }
        m_parser_state = ParserState::Text;
      } else if (character == 'E') {
        // Next Line (NEL)
        m_cursor_column = 0;
        if (m_in_alternate_screen) {
          if (m_cursor_line + 1 < m_rows) {
            ++m_cursor_line;
            while (m_cursor_line >= m_lines.size()) {
              m_lines.emplace_back();
            }
          } else {
            if (!m_lines.empty()) {
              m_lines.erase(m_lines.begin());
              m_lines.emplace_back();
            }
            m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
          }
        } else {
          if (m_cursor_line + 1 < m_lines.size()) {
            ++m_cursor_line;
          } else {
            m_lines.emplace_back();
            m_cursor_line = m_lines.size() - 1;
          }
          trim_scrollback();
        }
        m_parser_state = ParserState::Text;
      } else if (character == 'D') {
        // Index (IND)
        if (m_in_alternate_screen) {
          if (m_cursor_line + 1 < m_rows) {
            ++m_cursor_line;
            while (m_cursor_line >= m_lines.size()) {
              m_lines.emplace_back();
            }
          } else {
            if (!m_lines.empty()) {
              m_lines.erase(m_lines.begin());
              m_lines.emplace_back();
            }
            m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
          }
        } else {
          if (m_cursor_line + 1 < m_lines.size()) {
            ++m_cursor_line;
          } else {
            m_lines.emplace_back();
            m_cursor_line = m_lines.size() - 1;
          }
          trim_scrollback();
        }
        m_parser_state = ParserState::Text;
      } else if (character == 'c') {
        clear_screen();
        m_parser_state = ParserState::Text;
      } else if (character == '#') {
        m_parser_state = ParserState::DesignateCharacterSet;
      } else {
        m_parser_state = ParserState::Text;
      }
      break;

    case ParserState::DesignateCharacterSet:
      m_parser_state = ParserState::Text;
      break;

    case ParserState::ControlSequence:
      if (character >= '@' && character <= '~') {
        apply_control_sequence(character);
        m_parser_state = ParserState::Text;
      } else if (m_control_sequence.size() < 256) {
        m_control_sequence.push_back(character);
      }
      break;

    case ParserState::OperatingSystemCommand:
      if (character == '\a') {
        m_parser_state = ParserState::Text;
      } else if (character == '\x1B') {
        m_parser_state = ParserState::OperatingSystemCommandEscape;
      }
      break;

    case ParserState::OperatingSystemCommandEscape:
      m_parser_state = character == '\\' ? ParserState::Text
                                         : ParserState::OperatingSystemCommand;
      break;

    case ParserState::DeviceControlString:
      if (character == '\a') {
        m_parser_state = ParserState::Text;
      } else if (character == '\x1B') {
        m_parser_state = ParserState::DeviceControlStringEscape;
      }
      break;

    case ParserState::DeviceControlStringEscape:
      m_parser_state = character == '\\' ? ParserState::Text
                                         : ParserState::DeviceControlString;
      break;
    }
  }
  trim_scrollback();
}

void TerminalSession::apply_control_sequence(char command) {
  std::vector<std::size_t> params;
  bool is_private = false;
  std::string_view seq = m_control_sequence;
  if (!seq.empty() && (seq.front() == '?' || seq.front() == '>' ||
                       seq.front() == '<' || seq.front() == '=')) {
    is_private = (seq.front() == '?');
    seq.remove_prefix(1);
  }

  while (!seq.empty()) {
    const std::size_t sep = seq.find_first_of(";:");
    const std::string_view token =
        (sep != std::string_view::npos) ? seq.substr(0, sep) : seq;
    if (!token.empty()) {
      std::size_t v = 0;
      bool valid = true;
      for (const char c : token) {
        if (c >= '0' && c <= '9') {
          v = v * 10 + static_cast<std::size_t>(c - '0');
        } else {
          valid = false;
          break;
        }
      }
      if (valid) {
        params.push_back(v);
      }
    } else {
      params.push_back(0);
    }
    if (sep == std::string_view::npos) {
      break;
    }
    seq.remove_prefix(sep + 1);
  }

  const std::size_t param1 = params.size() > 0 ? params[0] : 0;
  const std::size_t param2 = params.size() > 1 ? params[1] : 0;

  if (m_lines.empty()) {
    m_lines.emplace_back();
    m_cursor_line = 0;
  }
  if (m_cursor_line >= m_lines.size()) {
    m_cursor_line = m_lines.size() - 1;
  }

  switch (command) {
  case 'A': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_line = (count <= m_cursor_line) ? (m_cursor_line - count) : 0;
    break;
  }
  case 'B': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_line += count;
    if (m_in_alternate_screen && m_rows > 0 && m_cursor_line >= m_rows) {
      m_cursor_line = m_rows - 1;
    }
    while (m_cursor_line >= m_lines.size()) {
      m_lines.emplace_back();
    }
    break;
  }
  case 'C': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_column += count;
    if (m_columns > 0 && m_cursor_column >= m_columns) {
      m_cursor_column = m_columns - 1;
    }
    break;
  }
  case 'D': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_column =
        (count <= m_cursor_column) ? (m_cursor_column - count) : 0;
    break;
  }
  case 'E': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_column = 0;
    m_cursor_line += count;
    if (m_in_alternate_screen && m_rows > 0 && m_cursor_line >= m_rows) {
      m_cursor_line = m_rows - 1;
    }
    while (m_cursor_line >= m_lines.size()) {
      m_lines.emplace_back();
    }
    break;
  }
  case 'F': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_column = 0;
    m_cursor_line = (count <= m_cursor_line) ? (m_cursor_line - count) : 0;
    break;
  }
  case 'G':
  case '`': {
    m_cursor_column = param1 > 0 ? (param1 - 1) : 0;
    if (m_columns > 0 && m_cursor_column >= m_columns) {
      m_cursor_column = m_columns - 1;
    }
    break;
  }
  case 'd': {
    const std::size_t row = param1 > 0 ? (param1 - 1) : 0;
    if (m_in_alternate_screen) {
      m_cursor_line = std::min(row, m_rows > 0 ? m_rows - 1 : 0);
    } else {
      std::size_t target_line = (m_lines.size() >= m_rows && m_rows > 0)
                                    ? (m_lines.size() - m_rows) + row
                                    : row;
      while (target_line >= m_lines.size()) {
        m_lines.emplace_back();
      }
      m_cursor_line = target_line;
    }
    break;
  }
  case 'e': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_line += count;
    if (m_in_alternate_screen && m_rows > 0 && m_cursor_line >= m_rows) {
      m_cursor_line = m_rows - 1;
    }
    while (m_cursor_line >= m_lines.size()) {
      m_lines.emplace_back();
    }
    break;
  }
  case 'a': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_cursor_column += count;
    if (m_columns > 0 && m_cursor_column >= m_columns) {
      m_cursor_column = m_columns - 1;
    }
    break;
  }
  case 'H':
  case 'f': {
    const std::size_t row = param1 > 0 ? (param1 - 1) : 0;
    const std::size_t col = param2 > 0 ? (param2 - 1) : 0;
    if (m_in_alternate_screen) {
      m_cursor_line = std::min(row, m_rows > 0 ? m_rows - 1 : 0);
      m_cursor_column = (m_columns > 0) ? std::min(col, m_columns - 1) : col;
    } else {
      std::size_t target_line = (m_lines.size() >= m_rows && m_rows > 0)
                                    ? (m_lines.size() - m_rows) + row
                                    : row;
      while (target_line >= m_lines.size()) {
        m_lines.emplace_back();
      }
      m_cursor_line = target_line;
      m_cursor_column = (m_columns > 0) ? std::min(col, m_columns - 1) : col;
    }
    break;
  }
  case 'I': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    for (std::size_t k = 0; k < count; ++k) {
      m_cursor_column = ((m_cursor_column / 8) + 1) * 8;
    }
    if (m_columns > 0 && m_cursor_column >= m_columns) {
      m_cursor_column = m_columns - 1;
    }
    break;
  }
  case 'Z': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    for (std::size_t k = 0; k < count; ++k) {
      if (m_cursor_column <= 8) {
        m_cursor_column = 0;
        break;
      }
      m_cursor_column = ((m_cursor_column - 1) / 8) * 8;
    }
    break;
  }
  case 'h': {
    if (is_private) {
      for (const auto val : params) {
        if (val == 1049 || val == 47 || val == 1047) {
          if (!m_in_alternate_screen) {
            m_in_alternate_screen = true;
            m_main_screen_lines = m_lines;
            m_main_cursor_line = m_cursor_line;
            m_main_cursor_column = m_cursor_column;
            const std::size_t count = std::max<std::size_t>(m_rows, 1);
            m_lines.assign(count, std::string{});
            m_cursor_line = 0;
            m_cursor_column = 0;
          }
        } else if (val == 1000) {
          m_mouse_tracking = MouseTracking::X10;
        } else if (val == 1002) {
          m_mouse_tracking = MouseTracking::ButtonEvent;
        } else if (val == 1003) {
          m_mouse_tracking = MouseTracking::AnyEvent;
        } else if (val == 1006) {
          m_sgr_mouse = true;
        } else if (val == 1007) {
          m_alternate_scroll = true;
        } else if (val == 1) {
          m_application_cursor_keys = true;
        }
      }
    }
    break;
  }
  case 'l': {
    if (is_private) {
      for (const auto val : params) {
        if (val == 1049 || val == 47 || val == 1047) {
          if (m_in_alternate_screen) {
            m_in_alternate_screen = false;
            m_lines = std::move(m_main_screen_lines);
            m_cursor_line = std::min(m_main_cursor_line,
                                     m_lines.empty() ? 0 : m_lines.size() - 1);
            m_cursor_column = m_main_cursor_column;
          }
        } else if (val == 1000 || val == 1002 || val == 1003) {
          m_mouse_tracking = MouseTracking::Off;
        } else if (val == 1006) {
          m_sgr_mouse = false;
        } else if (val == 1007) {
          m_alternate_scroll = false;
        } else if (val == 1) {
          m_application_cursor_keys = false;
        }
      }
    }
    break;
  }
  case 's': {
    m_saved_cursor_line = m_cursor_line;
    m_saved_cursor_column = m_cursor_column;
    break;
  }
  case 'u': {
    m_cursor_line =
        std::min(m_saved_cursor_line, m_lines.empty() ? 0 : m_lines.size() - 1);
    m_cursor_column = m_saved_cursor_column;
    break;
  }
  case 'K': {
    std::string &line = m_lines[m_cursor_line];
    if (param1 == 2 || m_control_sequence == "2") {
      line.clear();
    } else if (param1 == 1 || m_control_sequence == "1") {
      std::vector<std::string> cps = split_utf8_codepoints(line);
      const std::size_t end = std::min(m_cursor_column + 1, cps.size());
      for (std::size_t idx = 0; idx < end; ++idx) {
        cps[idx] = " ";
      }
      line.clear();
      for (const auto &cp : cps)
        line.append(cp);
    } else {
      erase_utf8_from(line, m_cursor_column);
    }
    break;
  }
  case 'X': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    std::string &line = m_lines[m_cursor_line];
    std::vector<std::string> cps = split_utf8_codepoints(line);
    if (m_cursor_column < cps.size()) {
      const std::size_t erase_len =
          std::min(count, cps.size() - m_cursor_column);
      for (std::size_t idx = m_cursor_column; idx < m_cursor_column + erase_len;
           ++idx) {
        cps[idx] = " ";
      }
      line.clear();
      for (const auto &cp : cps)
        line.append(cp);
    }
    break;
  }
  case 'P': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    std::string &line = m_lines[m_cursor_line];
    std::vector<std::string> cps = split_utf8_codepoints(line);
    if (m_cursor_column < cps.size()) {
      const std::size_t del_len = std::min(count, cps.size() - m_cursor_column);
      cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(m_cursor_column),
                cps.begin() +
                    static_cast<std::ptrdiff_t>(m_cursor_column + del_len));
      line.clear();
      for (const auto &cp : cps)
        line.append(cp);
    }
    break;
  }
  case '@': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    std::string &line = m_lines[m_cursor_line];
    std::vector<std::string> cps = split_utf8_codepoints(line);
    if (m_cursor_column <= cps.size()) {
      cps.insert(cps.begin() + static_cast<std::ptrdiff_t>(m_cursor_column),
                 count, " ");
      line.clear();
      for (const auto &cp : cps)
        line.append(cp);
    }
    break;
  }
  case 'M': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    if (m_cursor_line < m_lines.size()) {
      const std::size_t del_count =
          std::min(count, m_lines.size() - m_cursor_line);
      m_lines.erase(m_lines.begin() +
                        static_cast<std::ptrdiff_t>(m_cursor_line),
                    m_lines.begin() +
                        static_cast<std::ptrdiff_t>(m_cursor_line + del_count));
      if (m_in_alternate_screen) {
        m_lines.resize(std::max<std::size_t>(m_rows, 1));
      }
    }
    break;
  }
  case 'L': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    if (m_cursor_line < m_lines.size()) {
      m_lines.insert(m_lines.begin() +
                         static_cast<std::ptrdiff_t>(m_cursor_line),
                     count, std::string{});
      if (m_in_alternate_screen && m_lines.size() > m_rows) {
        m_lines.resize(m_rows);
      }
    }
    break;
  }
  case 'J': {
    if (param1 == 2 || m_control_sequence == "2") {
      if (m_in_alternate_screen) {
        const std::size_t count = std::max<std::size_t>(m_rows, 1);
        m_lines.assign(count, std::string{});
      } else {
        clear_screen();
      }
    } else if (param1 == 3 || m_control_sequence == "3") {
      trim_scrollback();
    } else if (param1 == 1 || m_control_sequence == "1") {
      if (m_cursor_line < m_lines.size()) {
        for (std::size_t r = 0; r < m_cursor_line; ++r) {
          m_lines[r].clear();
        }
        std::vector<std::string> cps =
            split_utf8_codepoints(m_lines[m_cursor_line]);
        const std::size_t end = std::min(m_cursor_column + 1, cps.size());
        for (std::size_t idx = 0; idx < end; ++idx) {
          cps[idx] = " ";
        }
        m_lines[m_cursor_line].clear();
        for (const auto &cp : cps)
          m_lines[m_cursor_line].append(cp);
      }
    } else {
      if (m_cursor_line < m_lines.size()) {
        erase_utf8_from(m_lines[m_cursor_line], m_cursor_column);
        if (m_in_alternate_screen) {
          for (std::size_t r = m_cursor_line + 1; r < m_lines.size(); ++r) {
            m_lines[r].clear();
          }
        } else if (m_cursor_line + 1 < m_lines.size()) {
          m_lines.erase(m_lines.begin() +
                            static_cast<std::ptrdiff_t>(m_cursor_line + 1),
                        m_lines.end());
        }
      }
    }
    break;
  }
  case 'S': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    if (count >= m_lines.size()) {
      m_lines.assign(m_in_alternate_screen ? m_rows : 1, std::string{});
    } else {
      m_lines.erase(m_lines.begin(),
                    m_lines.begin() + static_cast<std::ptrdiff_t>(count));
      if (m_in_alternate_screen) {
        m_lines.resize(m_rows);
      }
    }
    break;
  }
  case 'T':
  case '^': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_lines.insert(m_lines.begin(), count, std::string{});
    if (m_in_alternate_screen && m_lines.size() > m_rows) {
      m_lines.resize(m_rows);
    }
    break;
  }
  case 'b': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    if (!m_lines.empty() && m_cursor_line < m_lines.size() &&
        !m_lines[m_cursor_line].empty()) {
      const auto cps = split_utf8_codepoints(m_lines[m_cursor_line]);
      if (!cps.empty()) {
        const std::string last_cp = cps.back();
        for (std::size_t idx = 0; idx < count; ++idx) {
          append_codepoint(last_cp);
        }
      }
    }
    break;
  }
  case 'n': {
    auto send_pty_response = [this](std::string_view resp) {
      if (!m_running || resp.empty() || !m_implementation)
        return;
#if defined(_WIN32)
      if (m_implementation->input_write != nullptr) {
        DWORD written = 0;
        WriteFile(m_implementation->input_write, resp.data(),
                  static_cast<DWORD>(resp.size()), &written, nullptr);
      }
#else
      if (m_implementation->master_fd >= 0) {
        static_cast<void>(
            ::write(m_implementation->master_fd, resp.data(), resp.size()));
      }
#endif
    };

    if (param1 == 6 || m_control_sequence == "6" ||
        m_control_sequence == "?6") {
      const std::size_t report_row =
          (m_in_alternate_screen
               ? m_cursor_line
               : (m_cursor_line >= m_rows ? m_rows - 1 : m_cursor_line)) +
          1;
      const std::size_t report_col = m_cursor_column + 1;
      send_pty_response("\x1b[" + std::to_string(report_row) + ";" +
                        std::to_string(report_col) + "R");
    } else if (param1 == 5 || m_control_sequence == "5" ||
               m_control_sequence == "?5") {
      send_pty_response("\x1b[0n");
    }
    break;
  }
  case 'c': {
    auto send_pty_response = [this](std::string_view resp) {
      if (!m_running || resp.empty() || !m_implementation)
        return;
#if defined(_WIN32)
      if (m_implementation->input_write != nullptr) {
        DWORD written = 0;
        WriteFile(m_implementation->input_write, resp.data(),
                  static_cast<DWORD>(resp.size()), &written, nullptr);
      }
#else
      if (m_implementation->master_fd >= 0) {
        static_cast<void>(
            ::write(m_implementation->master_fd, resp.data(), resp.size()));
      }
#endif
    };

    if (!m_control_sequence.empty() && m_control_sequence.front() == '>') {
      send_pty_response("\x1b[>0;10;0c");
    } else {
      send_pty_response("\x1b[?62;1;2;6;7;8;9c");
    }
    break;
  }
  case 't': {
    auto send_pty_response = [this](std::string_view resp) {
      if (!m_running || resp.empty() || !m_implementation)
        return;
#if defined(_WIN32)
      if (m_implementation->input_write != nullptr) {
        DWORD written = 0;
        WriteFile(m_implementation->input_write, resp.data(),
                  static_cast<DWORD>(resp.size()), &written, nullptr);
      }
#else
      if (m_implementation->master_fd >= 0) {
        static_cast<void>(
            ::write(m_implementation->master_fd, resp.data(), resp.size()));
      }
#endif
    };

    if (param1 == 18 || m_control_sequence == "18") {
      send_pty_response("\x1b[8;" + std::to_string(m_rows) + ";" +
                        std::to_string(m_columns) + "t");
    }
    break;
  }
  default:
    break;
  }
}

void TerminalSession::append_codepoint(std::string_view utf8_char) {
  if (m_lines.empty()) {
    m_lines.emplace_back();
    m_cursor_line = 0;
  }
  while (m_cursor_line >= m_lines.size()) {
    m_lines.emplace_back();
  }
  if (m_cursor_column >= m_columns) {
    if (m_in_alternate_screen) {
      if (m_cursor_line + 1 < m_rows) {
        ++m_cursor_line;
        while (m_cursor_line >= m_lines.size()) {
          m_lines.emplace_back();
        }
        m_cursor_column = 0;
      } else {
        m_cursor_column = m_columns - 1;
      }
    } else {
      if (m_cursor_line + 1 < m_lines.size()) {
        ++m_cursor_line;
      } else {
        m_lines.emplace_back();
        m_cursor_line = m_lines.size() - 1;
      }
      m_cursor_column = 0;
      trim_scrollback();
    }
  }
  set_utf8_cell(m_lines[m_cursor_line], m_cursor_column, utf8_char);
  ++m_cursor_column;
}

void TerminalSession::append_character(char character) {
  append_codepoint(std::string_view(&character, 1));
}

void TerminalSession::append_line() {
  if (m_in_alternate_screen) {
    if (m_cursor_line + 1 < m_rows) {
      ++m_cursor_line;
      while (m_cursor_line >= m_lines.size()) {
        m_lines.emplace_back();
      }
    }
    m_cursor_column = 0;
  } else {
    m_lines.emplace_back();
    m_cursor_line = m_lines.size() - 1;
    m_cursor_column = 0;
    m_input_start_column = 0;
    m_pending_input.clear();
    trim_scrollback();
  }
}

void TerminalSession::append_status(std::string message) {
  if (!m_lines.empty() && m_lines.back().empty()) {
    m_lines.back() = std::move(message);
  } else {
    m_lines.push_back(std::move(message));
  }
  append_line();
}

void TerminalSession::clear_screen() noexcept {
  if (m_in_alternate_screen) {
    const std::size_t count = std::max<std::size_t>(m_rows, 1);
    m_lines.assign(count, std::string{});
  } else {
    m_lines.assign(1, std::string{});
  }
  m_cursor_line = 0;
  m_cursor_column = 0;
  m_saved_cursor_line = 0;
  m_saved_cursor_column = 0;
  m_input_start_column = 0;
  m_pending_input.clear();
  m_parser_state = ParserState::Text;
  m_control_sequence.clear();
}

void TerminalSession::trim_scrollback() {
  if (m_in_alternate_screen) {
    return;
  }
  if (m_lines.size() <= maximum_scrollback_lines) {
    return;
  }
  const std::size_t remove_count = m_lines.size() - maximum_scrollback_lines;
  m_lines.erase(m_lines.begin(),
                m_lines.begin() + static_cast<std::ptrdiff_t>(remove_count));
  if (m_cursor_line >= remove_count) {
    m_cursor_line -= remove_count;
  } else {
    m_cursor_line = 0;
  }
  if (m_saved_cursor_line >= remove_count) {
    m_saved_cursor_line -= remove_count;
  } else {
    m_saved_cursor_line = 0;
  }
}

bool TerminalSession::navigate_history(bool up) {
#if defined(_WIN32)
  if (m_implementation && m_implementation->is_conpty) {
    return false;
  }
#else
  return false;
#endif

  if (m_command_history.empty() || m_lines.empty()) {
    return false;
  }

  m_cursor_line = m_lines.size() - 1;

  if (m_input_start_column == 0 &&
      m_lines.back().size() >= m_pending_input.size()) {
    m_input_start_column = m_lines.back().size() - m_pending_input.size();
  }

  if (up) {
    if (!m_history_index.has_value()) {
      m_saved_pending_input = m_pending_input;
      m_history_index = m_command_history.size() - 1;
    } else if (*m_history_index > 0) {
      --(*m_history_index);
    } else {
      return false;
    }
  } else {
    if (!m_history_index.has_value()) {
      return false;
    }
    if (*m_history_index + 1 < m_command_history.size()) {
      ++(*m_history_index);
    } else {
      m_history_index.reset();
    }
  }

  const std::string_view target_text =
      m_history_index.has_value()
          ? std::string_view(m_command_history[*m_history_index])
          : std::string_view(m_saved_pending_input);

  if (m_input_start_column > m_lines.back().size()) {
    m_input_start_column = m_lines.back().size();
  }

  m_lines.back().resize(m_input_start_column);
  m_lines.back().append(target_text);
  m_cursor_column = m_lines.back().size();
  m_pending_input = std::string(target_text);
  m_pending_input_cursor = m_pending_input.size();
  return true;
}

bool TerminalSession::send_mouse_scroll(std::ptrdiff_t line_delta,
                                        std::size_t column, std::size_t row) {
  if (!m_running || line_delta == 0) {
    return false;
  }

  if (m_mouse_tracking != MouseTracking::Off) {
    const int button =
        (line_delta < 0) ? 64 : 65; // 64 = WheelUp, 65 = WheelDown
    const std::size_t steps =
        std::clamp<std::size_t>(std::abs(line_delta) / 3, 1, 5);
    if (m_sgr_mouse) {
      // SGR format: \x1b[<button;col;rowM
      std::string seq;
      for (std::size_t i = 0; i < steps; ++i) {
        seq += "\x1B[<" + std::to_string(button) + ";" +
               std::to_string(std::max<std::size_t>(1, column)) + ";" +
               std::to_string(std::max<std::size_t>(1, row)) + "M";
      }
      return write_input(seq);
    } else {
      // X10 / normal mouse format: \x1b[M Cb Cx Cy (where each is 32 + val)
      std::string seq;
      const char cb = static_cast<char>(32 + button);
      const char cx =
          static_cast<char>(32 + std::clamp<std::size_t>(column, 1, 223));
      const char cy =
          static_cast<char>(32 + std::clamp<std::size_t>(row, 1, 223));
      for (std::size_t i = 0; i < steps; ++i) {
        seq += "\x1B[M";
        seq.push_back(cb);
        seq.push_back(cx);
        seq.push_back(cy);
      }
      return write_input(seq);
    }
  }

  if (m_in_alternate_screen && m_alternate_scroll) {
    // Alternate scroll mode: Send Arrow Up / Down keys to the running CLI
    // program (less, vim, nano, man, etc.)
    const std::string_view arrow =
        (line_delta < 0) ? (m_application_cursor_keys ? "\x1BOA" : "\x1B[A")
                         : (m_application_cursor_keys ? "\x1BOB" : "\x1B[B");
    const std::size_t steps =
        std::clamp<std::size_t>(std::abs(line_delta), 1, 5);
    std::string seq;
    for (std::size_t i = 0; i < steps; ++i) {
      seq.append(arrow);
    }
    return write_input(seq);
  }

  return false;
}

bool TerminalSession::send_mouse_button(MouseButton button, MouseAction action,
                                        std::size_t column, std::size_t row,
                                        bool shift, bool meta, bool ctrl) {
  if (!m_running || m_mouse_tracking == MouseTracking::Off) {
    return false;
  }

  if (m_mouse_tracking == MouseTracking::X10 && action != MouseAction::Press) {
    return false;
  }

  int btn_code = static_cast<int>(button);
  if (action == MouseAction::Motion) {
    btn_code += 32;
  }
  if (shift)
    btn_code += 4;
  if (meta)
    btn_code += 8;
  if (ctrl)
    btn_code += 16;

  const std::size_t col = std::max<std::size_t>(1, column);
  const std::size_t r = std::max<std::size_t>(1, row);

  if (m_sgr_mouse) {
    const char terminator = (action == MouseAction::Release) ? 'm' : 'M';
    const std::string seq = "\x1B[<" + std::to_string(btn_code) + ";" +
                            std::to_string(col) + ";" + std::to_string(r) +
                            terminator;
    return write_input(seq);
  } else {
    if (action == MouseAction::Release) {
      btn_code = 3;
      if (shift)
        btn_code += 4;
      if (meta)
        btn_code += 8;
      if (ctrl)
        btn_code += 16;
    }
    const char cb = static_cast<char>(32 + btn_code);
    const char cx =
        static_cast<char>(32 + std::clamp<std::size_t>(col, 1, 223));
    const char cy = static_cast<char>(32 + std::clamp<std::size_t>(r, 1, 223));
    std::string seq = "\x1B[M";
    seq.push_back(cb);
    seq.push_back(cx);
    seq.push_back(cy);
    return write_input(seq);
  }
}

bool TerminalSession::send_mouse_motion(std::size_t column, std::size_t row,
                                        bool button_pressed,
                                        MouseButton pressed_button, bool shift,
                                        bool meta, bool ctrl) {
  if (!m_running || m_mouse_tracking == MouseTracking::Off) {
    return false;
  }

  if (m_mouse_tracking == MouseTracking::X10) {
    return false;
  }

  if (m_mouse_tracking == MouseTracking::ButtonEvent && !button_pressed) {
    return false;
  }

  int btn_code = button_pressed ? static_cast<int>(pressed_button) : 3;
  btn_code += 32;
  if (shift)
    btn_code += 4;
  if (meta)
    btn_code += 8;
  if (ctrl)
    btn_code += 16;

  const std::size_t col = std::max<std::size_t>(1, column);
  const std::size_t r = std::max<std::size_t>(1, row);

  if (m_sgr_mouse) {
    const std::string seq = "\x1B[<" + std::to_string(btn_code) + ";" +
                            std::to_string(col) + ";" + std::to_string(r) + "M";
    return write_input(seq);
  } else {
    const char cb = static_cast<char>(32 + btn_code);
    const char cx =
        static_cast<char>(32 + std::clamp<std::size_t>(col, 1, 223));
    const char cy = static_cast<char>(32 + std::clamp<std::size_t>(r, 1, 223));
    std::string seq = "\x1B[M";
    seq.push_back(cb);
    seq.push_back(cx);
    seq.push_back(cy);
    return write_input(seq);
  }
}

} // namespace Zenvra::Terminal
