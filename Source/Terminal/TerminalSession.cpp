#include "TerminalSession.h"
#include "TerminalExitDecoder.h"
#include "Terminal/TerminalBackend.h"

#if defined(_WIN32)
#include "Terminal/ConPTYBackend.h"
#else
#include "Terminal/PosixTTYBackend.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
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


// ─── Platform includes ────────────────────────────────────────────────
#if defined(_WIN32)
// ── Win32: ConPTY + Win32 API ────────────────────────────────────────
#include <windows.h>
#elif defined(__APPLE__)
// ── macOS: forkpty via libutil ────────────────────────────────────────
#include <csignal>
#include <fcntl.h>
#include <util.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
// ── Linux: forkpty via libutil ────────────────────────────────────────
#include <csignal>
#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Zenvra::Terminal {

static std::mutex& get_log_mutex() {
  static auto* m = new std::mutex();
  return *m;
}

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
  // Win32: use %TEMP%
  char tmp_buf[512] = {};
  std::size_t tmp_len = 0;
  if (getenv_s(&tmp_len, tmp_buf, sizeof(tmp_buf), "TEMP") == 0 &&
      tmp_len > 0) {
    log_path = std::filesystem::path(tmp_buf);
  } else {
    log_path = std::filesystem::current_path(ec);
  }
#elif defined(__APPLE__)
  // macOS: use $TMPDIR (typically /var/folders/...)
  const char *tmp = std::getenv("TMPDIR");
  log_path =
      tmp ? std::filesystem::path(tmp) : std::filesystem::current_path(ec);
#else
  // Linux: use $TMPDIR or fallback to cwd
  const char *tmp = std::getenv("TMPDIR");
  log_path =
      tmp ? std::filesystem::path(tmp) : std::filesystem::current_path(ec);
#endif
  log_path /= "zde-terminal.log";

  std::lock_guard<std::mutex> lock(get_log_mutex());
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

namespace {

constexpr std::size_t maximum_scrollback_lines = 4000;

#if defined(_WIN32)
static std::string normalize_shell_key(const std::filesystem::path &path) {
  std::string s = path.string();
  for (char &c : s) {
    if (c == '/')
      c = '\\';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
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

std::wstring windows_shell_arguments(const std::filesystem::path &shell_path) {
  const std::wstring stem = shell_path.stem().wstring();
  std::wstring lower_stem;
  lower_stem.reserve(stem.size());
  for (wchar_t c : stem) {
    lower_stem.push_back(static_cast<wchar_t>(std::towlower(c)));
  }
  if (lower_stem.find(L"powershell") != std::wstring::npos ||
      lower_stem.find(L"pwsh") != std::wstring::npos) {
    return L" -NoLogo -NoExit -ExecutionPolicy Bypass -NoProfile";
  }
  return L"";
}
#endif

} // namespace

struct TerminalSession::Implementation {
  std::unique_ptr<ITerminalBackend> backend;
};

TerminalSession::TerminalSession()
    : m_implementation(std::make_unique<Implementation>()) {}

TerminalSession::~TerminalSession() { stop(); }

// ─── Win32: ConPTY API, shell helpers ────────────────────────────────
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
      const std::string key = normalize_shell_key(p);
      for (const auto &existing : candidates) {
        if (normalize_shell_key(existing) == key) {
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

  // 1. TOP PRIORITY: PowerShell 7+ (pwsh.exe) in standard installation directories
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
// ── Unix (Linux/macOS): resolve shell via $SHELL, getpwuid, fallback ──
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
  // macOS: zsh-first (default since Catalina), then fish, then bash/sh
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
  // Linux: bash-first, then zsh, then fish, then sh
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
  m_grid.assign(1, std::vector<TerminalCell>{});
  m_main_screen_lines.clear();
  m_main_screen_grid.clear();
  m_current_attributes = TerminalCellAttributes{};
  m_saved_attributes = TerminalCellAttributes{};
  m_cursor_line = 0;
  m_cursor_column = 0;
  m_saved_cursor_line = 0;
  m_saved_cursor_column = 0;
  m_main_cursor_line = 0;
  m_main_cursor_column = 0;
  m_utf8_sequence.clear();
  m_utf8_expected = 0;
  m_parser_state = ParserState::Text;
  m_control_sequence.clear();
  m_osc_payload.clear();
  m_title.clear();
  m_in_alternate_screen = false;
  m_cursor_visible = true;
  m_mouse_tracking = MouseTracking::Off;
  m_sgr_mouse = false;
  m_application_cursor_keys = false;
  m_alternate_scroll = true;
  m_columns = std::clamp<std::size_t>(initial_columns, 40, 65535);
  m_rows = std::clamp<std::size_t>(initial_rows, 10, 65535);

  const auto shell_candidates = resolve_host_shell_candidates();
  if (shell_candidates.empty()) {
    m_shell_path = "PowerShell";
    append_status("[Unable to find a local PowerShell executable]");
    return false;
  }

#if defined(_WIN32)
  // ═══ Win32: Pure ConPTY Mode ────────────────────────────────────────
  _wputenv_s(L"COLUMNS", std::to_wstring(m_columns).c_str());
  _wputenv_s(L"LINES", std::to_wstring(m_rows).c_str());
  _wputenv_s(L"COLORTERM", L"truecolor");
  _wputenv_s(L"TERM_PROGRAM", L"ZDE");
  _wputenv_s(L"TERM_PROGRAM_VERSION", L"1.0.0");
  _wputenv_s(L"FORCE_COLOR", L"3");
  _wputenv_s(L"CLICOLOR", L"1");
  _wputenv_s(L"CLICOLOR_FORCE", L"1");

  SetEnvironmentVariableW(L"COLUMNS", std::to_wstring(m_columns).c_str());
  SetEnvironmentVariableW(L"LINES", std::to_wstring(m_rows).c_str());
  SetEnvironmentVariableW(L"TERM", nullptr);
  SetEnvironmentVariableW(L"COLORTERM", L"truecolor");
  SetEnvironmentVariableW(L"TERM_PROGRAM", L"ZDE");
  SetEnvironmentVariableW(L"TERM_PROGRAM_VERSION", L"1.0.0");
  SetEnvironmentVariableW(L"FORCE_COLOR", L"3");
  SetEnvironmentVariableW(L"CLICOLOR", L"1");
  SetEnvironmentVariableW(L"CLICOLOR_FORCE", L"1");

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

  bool started = false;
  std::size_t candidate_index = 0;

  terminal_debug_log("[ZDE Terminal] Starting session. Resolved " +
                     std::to_string(shell_candidates.size()) +
                     " candidate(s):");
  for (const auto &cand : shell_candidates) {
    terminal_debug_log("  - Candidate: \"" + cand.string() + "\"");
  }

  const std::filesystem::path effective_dir =
      directory_str.empty() ? working_directory
                            : std::filesystem::path(directory_str);

  if (ConPTYBackend::is_supported()) {
    for (const auto &candidate_path : shell_candidates) {
      m_shell_path = candidate_path;
      auto conpty = std::make_unique<ConPTYBackend>();
      terminal_debug_log("[ZDE Terminal] Attempting ConPTY candidate #" +
                         std::to_string(candidate_index++) + ": \"" +
                         candidate_path.string() + "\"");

      if (conpty->start(candidate_path, windows_shell_arguments(candidate_path),
                        effective_dir, m_columns, m_rows)) {
        m_implementation->backend = std::move(conpty);
        m_running = true;
        terminal_debug_log("[ZDE Terminal] Shell committed (ConPTY mode) for \"" +
                           candidate_path.string() + "\"");
        started = true;
        break;
      }
    }
  }

  if (!started) {
    stop();
    m_shell_path = "PowerShell";
    terminal_debug_log(
        "[ZDE Terminal] Failed to start PowerShell via ConPTY.");
    append_status("[Unable to start PowerShell via ConPTY]");
    return false;
  }
#else
  // ═══ Unix (Linux/macOS): PosixTTYBackend ───────────────────────────
  m_shell_path = shell_candidates.front();
  auto posix_backend = std::make_unique<PosixTTYBackend>();
  if (!posix_backend->start(m_shell_path, L"", working_directory, m_columns, m_rows)) {
    append_status("[Unable to create the local terminal PTY]");
    return true;
  }
  m_implementation->backend = std::move(posix_backend);
  m_running = true;
#endif
  return true;
}

void TerminalSession::stop() noexcept {
  if (!m_implementation) {
    return;
  }
  if (m_implementation->backend) {
    m_implementation->backend->stop();
    m_implementation->backend.reset();
  }
  m_running = false;
}

bool TerminalSession::is_running() const noexcept { return m_running; }

bool TerminalSession::is_conpty_mode() const noexcept {
  return m_implementation != nullptr && m_implementation->backend != nullptr &&
         m_implementation->backend->is_pty();
}

const std::filesystem::path &TerminalSession::get_shell_path() const noexcept {
  return m_shell_path;
}

std::span<const std::string> TerminalSession::get_lines() const noexcept {
  return m_lines;
}

static std::size_t utf8_column_count(std::string_view s) {
  std::size_t cols = 0;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c & 0x80) == 0)
      i += 1;
    else if ((c & 0xE0) == 0xC0)
      i += 2;
    else if ((c & 0xF0) == 0xE0)
      i += 3;
    else if ((c & 0xF8) == 0xF0)
      i += 4;
    else
      i += 1;
    ++cols;
  }
  return cols;
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

std::vector<TerminalStyledSpan>
TerminalSession::get_line_spans(std::size_t line_index) const {
  if (line_index >= m_lines.size()) {
    return {};
  }

  // Check if grid row has explicit styling
  bool has_explicit_styles = false;
  if (line_index < m_grid.size() && !m_grid[line_index].empty()) {
    for (const auto &cell : m_grid[line_index]) {
      if (!cell.attributes.foreground.is_default ||
          !cell.attributes.background.is_default ||
          cell.attributes.bold || cell.attributes.underline) {
        has_explicit_styles = true;
        break;
      }
    }
  }

  // If cells have explicit ANSI styling, return the spans from grid
  if (has_explicit_styles) {
    const auto &row = m_grid[line_index];
    std::vector<TerminalStyledSpan> spans;
    TerminalStyledSpan current_span;
    bool in_span = false;

    for (const auto &cell : row) {
      if (!in_span) {
        current_span.text = cell.codepoint;
        current_span.attributes = cell.attributes;
        in_span = true;
      } else if (current_span.attributes == cell.attributes) {
        current_span.text.append(cell.codepoint);
      } else {
        spans.push_back(std::move(current_span));
        current_span.text = cell.codepoint;
        current_span.attributes = cell.attributes;
      }
    }
    if (in_span && !current_span.text.empty()) {
      spans.push_back(std::move(current_span));
    }
    return spans;
  }

  // Smart semantic colorization for unstyled output
  const std::string &line = m_lines[line_index];
  if (line.empty()) {
    return {};
  }

  std::string_view lv = line;
  while (!lv.empty() && (lv.front() == ' ' || lv.front() == '\t')) {
    lv.remove_prefix(1);
  }

  // 1. Error highlighting (Red for process exit/fatal crashes)
  const bool is_error =
      lv.starts_with("[Process exited with code") || lv.starts_with("[Process crashed") ||
      lv.starts_with("fatal:") || lv.starts_with("FATAL:") ||
      lv.starts_with("Traceback (most recent call last):");

  if (is_error) {
    TerminalCellAttributes err_attr{};
    err_attr.foreground = TerminalColor{0xf4, 0x47, 0x47, false}; // ANSI Red
    return {TerminalStyledSpan{.text = line, .attributes = err_attr}};
  }

  // Default clean primary white text for unstyled terminal lines and prompts (accurate across all panel resizes)
  return {TerminalStyledSpan{
      .text = line,
      .attributes = TerminalCellAttributes{},
  }};
}

bool TerminalSession::write_input(std::string_view text) {
  if (text.empty() || !m_running || !m_implementation || !m_implementation->backend) {
    return false;
  }
  return m_implementation->backend->write_input(text);
}

bool TerminalSession::poll() {
  if (!m_running || !m_implementation || !m_implementation->backend) {
    return false;
  }
  bool changed = false;
  std::array<char, 8192> buffer{};

  for (;;) {
    const std::size_t bytes_read = m_implementation->backend->read_output(buffer);
    if (bytes_read == 0) {
      break;
    }
    consume_output(std::string_view{buffer.data(), bytes_read});
    terminal_debug_log("[ZDE Terminal] read " + std::to_string(bytes_read) + " bytes: " +
                       std::string(buffer.data(), std::min<std::size_t>(bytes_read, 300)));
    changed = true;
  }
  if (changed) {
    terminal_debug_log("[ZDE Terminal] poll changed, total lines=" + std::to_string(m_lines.size()));
    for (std::size_t i = 0; i < std::min<std::size_t>(m_lines.size(), 10); ++i) {
      terminal_debug_log("  [line " + std::to_string(i) + "] \"" + m_lines[i] + "\"");
    }
  }

  uint32_t exit_code = 0;
  if (m_implementation->backend->check_exit(exit_code)) {
    m_running = false;

#if defined(_WIN32)
    const auto exit_info = decode_terminal_exit(
        exit_code, m_shell_path,
        m_implementation->backend ? m_implementation->backend->is_pty() : true);
    terminal_debug_log("[ZDE Terminal] Process exited with code " +
                       std::to_string(exit_code) +
                       " (Hex: " + exit_info.hex_code +
                       ", Summary: " + exit_info.summary + ")");
    append_status(exit_info.formatted_message);
    changed = true;
#else
    std::string exit_message =
        "[Process exited with code " + std::to_string(exit_code) + "]";
    append_status(exit_message);
    changed = true;
#endif
  }
  return changed;
}

void TerminalSession::resize(std::size_t columns, std::size_t rows) noexcept {
  columns = std::clamp<std::size_t>(columns, 20, 65535);
  rows = std::clamp<std::size_t>(rows, 2, 65535);
  if (columns == m_columns && rows == m_rows) {
    return;
  }
  m_columns = columns;
  m_rows = rows;
  if (m_in_alternate_screen) {
    m_lines.resize(m_rows);
    m_grid.resize(m_rows);
    if (m_cursor_line >= m_rows) {
      m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
    }
  }
  if (m_implementation && m_implementation->backend) {
    m_implementation->backend->resize(columns, rows);
  }
#if defined(_WIN32)
  _wputenv_s(L"COLUMNS", std::to_wstring(columns).c_str());
  _wputenv_s(L"LINES", std::to_wstring(rows).c_str());
#endif
}

static std::string sanitize_terminal_title(std::string_view raw_title) {
  if (raw_title.empty()) {
    return {};
  }
  std::string_view title = raw_title;
  if (title.starts_with("Administrator: ")) {
    title.remove_prefix(15);
  } else if (title.starts_with("Administrator:")) {
    title.remove_prefix(14);
  } else if (title.starts_with("Select ")) {
    title.remove_prefix(7);
  }
  while (!title.empty() && (title.front() == ' ' || title.front() == '\t')) {
    title.remove_prefix(1);
  }
  while (!title.empty() && (title.back() == ' ' || title.back() == '\t' ||
                            title.back() == '\r' || title.back() == '\n')) {
    title.remove_suffix(1);
  }
  if (title.find('\\') != std::string_view::npos ||
      title.find('/') != std::string_view::npos) {
    const std::filesystem::path p(title);
    std::string stem = p.stem().string();
    if (!stem.empty()) {
      return stem;
    }
  }
  return std::string{title};
}

static TerminalColor ansi_indexed_color(std::size_t index) {
  static const std::array<TerminalColor, 16> standard_16 = {{
      {0x1e, 0x1e, 0x24, false}, // 0: Black (proper dark charcoal for dark theme)
      {0xf4, 0x47, 0x47, false}, // 1: Red
      {0x60, 0x8b, 0x4e, false}, // 2: Green
      {0xd7, 0xba, 0x7d, false}, // 3: Yellow
      {0x56, 0x9c, 0xd6, false}, // 4: Blue
      {0xc5, 0x86, 0xc0, false}, // 5: Magenta
      {0x4e, 0xc9, 0xb0, false}, // 6: Cyan
      {0xd4, 0xd4, 0xd4, false}, // 7: White
      {0x5c, 0x63, 0x70, false}, // 8: Bright Black (Gray)
      {0xf1, 0x4c, 0x4c, false}, // 9: Bright Red
      {0x23, 0xd1, 0x8b, false}, // 10: Bright Green
      {0xf5, 0xf5, 0x43, false}, // 11: Bright Yellow
      {0x3b, 0x8e, 0xed, false}, // 12: Bright Blue
      {0xd6, 0x70, 0xd6, false}, // 13: Bright Magenta
      {0x29, 0xb8, 0xdb, false}, // 14: Bright Cyan
      {0xff, 0xff, 0xff, false}, // 15: Bright White
  }};

  if (index < 16) {
    return standard_16[index];
  }
  if (index >= 16 && index <= 231) {
    const std::size_t cube_idx = index - 16;
    const uint8_t r_val = static_cast<uint8_t>(
        ((cube_idx / 36) % 6) == 0 ? 0 : 55 + ((cube_idx / 36) % 6) * 40);
    const uint8_t g_val = static_cast<uint8_t>(
        ((cube_idx / 6) % 6) == 0 ? 0 : 55 + ((cube_idx / 6) % 6) * 40);
    const uint8_t b_val =
        static_cast<uint8_t>((cube_idx % 6) == 0 ? 0 : 55 + (cube_idx % 6) * 40);
    return TerminalColor{r_val, g_val, b_val, false};
  }
  if (index >= 232 && index <= 255) {
    const uint8_t gray = static_cast<uint8_t>(8 + (index - 232) * 10);
    return TerminalColor{gray, gray, gray, false};
  }
  return TerminalColor{0, 0, 0, true};
}

void TerminalSession::apply_sgr_parameters(
    std::span<const std::size_t> params) {
  if (params.empty()) {
    m_current_attributes = TerminalCellAttributes{};
    return;
  }

  for (std::size_t i = 0; i < params.size(); ++i) {
    const std::size_t code = params[i];
    switch (code) {
    case 0:
      m_current_attributes = TerminalCellAttributes{};
      break;
    case 1:
      m_current_attributes.bold = true;
      break;
    case 2:
      m_current_attributes.dim = true;
      break;
    case 3:
      m_current_attributes.italic = true;
      break;
    case 4:
      m_current_attributes.underline = true;
      break;
    case 7:
      m_current_attributes.inverse = true;
      break;
    case 8:
      m_current_attributes.hidden = true;
      break;
    case 22:
      m_current_attributes.bold = false;
      m_current_attributes.dim = false;
      break;
    case 23:
      m_current_attributes.italic = false;
      break;
    case 24:
      m_current_attributes.underline = false;
      break;
    case 27:
      m_current_attributes.inverse = false;
      break;
    case 28:
      m_current_attributes.hidden = false;
      break;
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
      m_current_attributes.foreground = ansi_indexed_color(code - 30);
      break;
    case 39:
      m_current_attributes.foreground = TerminalColor{0, 0, 0, true};
      break;
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
      m_current_attributes.background = ansi_indexed_color(code - 40);
      break;
    case 49:
      m_current_attributes.background = TerminalColor{0, 0, 0, true};
      break;
    case 90:
    case 91:
    case 92:
    case 93:
    case 94:
    case 95:
    case 96:
    case 97:
      m_current_attributes.foreground = ansi_indexed_color(8 + code - 90);
      break;
    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
      m_current_attributes.background = ansi_indexed_color(8 + code - 100);
      break;
    case 38: {
      if (i + 1 < params.size()) {
        if (params[i + 1] == 5 && i + 2 < params.size()) {
          m_current_attributes.foreground = ansi_indexed_color(params[i + 2]);
          i += 2;
        } else if (params[i + 1] == 2) {
          if (params.size() - i == 6 && params[i + 2] == 0) {
            // Standalone ITU T.416 format: 38;2;0;r;g;b
            m_current_attributes.foreground = TerminalColor{
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 3], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 4], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 5], 255)),
                false};
            i += 5;
          } else if (i + 4 < params.size()) {
            // Standard TrueColor: 38;2;r;g;b
            m_current_attributes.foreground = TerminalColor{
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 2], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 3], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 4], 255)),
                false};
            i += 4;
          }
        }
      }
      break;
    }
    case 48: {
      if (i + 1 < params.size()) {
        if (params[i + 1] == 5 && i + 2 < params.size()) {
          m_current_attributes.background = ansi_indexed_color(params[i + 2]);
          i += 2;
        } else if (params[i + 1] == 2) {
          if (params.size() - i == 6 && params[i + 2] == 0) {
            // Standalone ITU T.416 format: 48;2;0;r;g;b
            m_current_attributes.background = TerminalColor{
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 3], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 4], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 5], 255)),
                false};
            i += 5;
          } else if (i + 4 < params.size()) {
            // Standard TrueColor: 48;2;r;g;b
            m_current_attributes.background = TerminalColor{
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 2], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 3], 255)),
                static_cast<uint8_t>(std::min<std::size_t>(params[i + 4], 255)),
                false};
            i += 4;
          }
        }
      }
      break;
    }
    default:
      break;
    }
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
          continue;
        } else {
          m_utf8_sequence.clear();
          m_utf8_expected = 0;
        }
      }

      if (static_cast<unsigned char>(character) >= 0x80U) {
        const unsigned char uc = static_cast<unsigned char>(character);
        if ((uc & 0xE0) == 0xC0) {
          m_utf8_sequence = character;
          m_utf8_expected = 1;
          continue;
        } else if ((uc & 0xF0) == 0xE0) {
          m_utf8_sequence = character;
          m_utf8_expected = 2;
          continue;
        } else if ((uc & 0xF8) == 0xF0) {
          m_utf8_sequence = character;
          m_utf8_expected = 3;
          continue;
        }
      }
    }

    switch (m_parser_state) {
    case ParserState::Text:
      if (character == '\x1B') {
        m_utf8_sequence.clear();
        m_utf8_expected = 0;
        m_parser_state = ParserState::Escape;
      } else if (character == '\n' || character == '\x0B' || character == '\x0C') {
        m_cursor_column = 0;
        if (m_in_alternate_screen) {
          if (m_cursor_line + 1 < m_rows) {
            ++m_cursor_line;
            while (m_cursor_line >= m_lines.size()) {
              m_lines.emplace_back();
              m_grid.emplace_back();
            }
          } else {
            if (!m_lines.empty()) {
              m_lines.erase(m_lines.begin());
              m_lines.emplace_back();
            }
            if (!m_grid.empty()) {
              m_grid.erase(m_grid.begin());
              m_grid.emplace_back();
            }
            m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
          }
        } else {
          if (m_cursor_line + 1 < m_lines.size()) {
            ++m_cursor_line;
          } else {
            m_lines.emplace_back();
            m_grid.emplace_back();
            m_cursor_line = m_lines.size() - 1;
          }
          trim_scrollback();
        }
      } else if (character == '\r') {
        m_cursor_column = 0;
      } else if (character == '\b' || character == '\x7F') {
        if (m_cursor_column > 0) {
          --m_cursor_column;
        } else if (m_cursor_line > 0 && !m_in_alternate_screen) {
          --m_cursor_line;
          m_cursor_column = (m_cursor_line < m_lines.size())
                                ? utf8_column_count(m_lines[m_cursor_line])
                                : (m_columns > 0 ? m_columns - 1 : 0);
          if (m_cursor_column > 0) {
            --m_cursor_column;
          }
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
        m_osc_payload.clear();
        m_parser_state = ParserState::OperatingSystemCommand;
      } else if (character == 'P' || character == '_' || character == '^') {
        m_parser_state = ParserState::DeviceControlString;
      } else if (character == '(' || character == ')' || character == '*' ||
                 character == '+') {
        m_parser_state = ParserState::DesignateCharacterSet;
      } else if (character == '7') {
        m_saved_cursor_line = m_cursor_line;
        m_saved_cursor_column = m_cursor_column;
        m_saved_attributes = m_current_attributes;
        m_parser_state = ParserState::Text;
      } else if (character == '8') {
        m_cursor_line = std::min(m_saved_cursor_line,
                                 m_lines.empty() ? 0 : m_lines.size() - 1);
        m_cursor_column = m_saved_cursor_column;
        m_current_attributes = m_saved_attributes;
        m_parser_state = ParserState::Text;
      } else if (character == 'M') {
        // Reverse Index
        if (m_cursor_line > 0) {
          --m_cursor_line;
        } else {
          m_lines.insert(m_lines.begin(), std::string{});
          m_grid.insert(m_grid.begin(), std::vector<TerminalCell>{});
          if (m_in_alternate_screen && m_lines.size() > m_rows) {
            m_lines.pop_back();
            if (m_grid.size() > m_rows) {
              m_grid.pop_back();
            }
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
              m_grid.emplace_back();
            }
          } else {
            if (!m_lines.empty()) {
              m_lines.erase(m_lines.begin());
              m_lines.emplace_back();
            }
            if (!m_grid.empty()) {
              m_grid.erase(m_grid.begin());
              m_grid.emplace_back();
            }
            m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
          }
        } else {
          if (m_cursor_line + 1 < m_lines.size()) {
            ++m_cursor_line;
          } else {
            m_lines.emplace_back();
            m_grid.emplace_back();
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
              m_grid.emplace_back();
            }
          } else {
            if (!m_lines.empty()) {
              m_lines.erase(m_lines.begin());
              m_lines.emplace_back();
            }
            if (!m_grid.empty()) {
              m_grid.erase(m_grid.begin());
              m_grid.emplace_back();
            }
            m_cursor_line = m_rows > 0 ? m_rows - 1 : 0;
          }
        } else {
          if (m_cursor_line + 1 < m_lines.size()) {
            ++m_cursor_line;
          } else {
            m_lines.emplace_back();
            m_grid.emplace_back();
            m_cursor_line = m_lines.size() - 1;
          }
          trim_scrollback();
        }
        m_parser_state = ParserState::Text;
      } else if (character == 'c') {
        clear_screen();
        m_current_attributes = TerminalCellAttributes{};
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
        if (m_osc_payload.starts_with("0;") ||
            m_osc_payload.starts_with("2;")) {
          m_title = sanitize_terminal_title(m_osc_payload.substr(2));
        }
        m_osc_payload.clear();
        m_parser_state = ParserState::Text;
      } else if (character == '\x1B') {
        m_parser_state = ParserState::OperatingSystemCommandEscape;
      } else if (m_osc_payload.size() < 1024) {
        m_osc_payload.push_back(character);
      }
      break;

    case ParserState::OperatingSystemCommandEscape:
      if (character == '\\') {
        if (m_osc_payload.starts_with("0;") ||
            m_osc_payload.starts_with("2;")) {
          m_title = sanitize_terminal_title(m_osc_payload.substr(2));
        }
        m_osc_payload.clear();
        m_parser_state = ParserState::Text;
      } else {
        m_parser_state = ParserState::OperatingSystemCommand;
      }
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

void TerminalSession::send_pty_response(std::string_view resp) {
  if (!m_running || resp.empty() || !m_implementation || !m_implementation->backend)
    return;
#if defined(_WIN32)
  if (is_conpty_mode()) {
    return;
  }
#endif
  static_cast<void>(m_implementation->backend->write_input(resp));
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
    m_grid.emplace_back();
    m_cursor_line = 0;
  }
  if (m_cursor_line >= m_lines.size()) {
    m_cursor_line = m_lines.size() - 1;
  }
  if (m_grid.size() < m_lines.size()) {
    m_grid.resize(m_lines.size());
  }

  switch (command) {
  case 'm': {
    apply_sgr_parameters(params);
    break;
  }
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
      m_grid.emplace_back();
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
    if (count <= m_cursor_column) {
      m_cursor_column -= count;
    } else {
      std::size_t remaining = count - m_cursor_column;
      m_cursor_column = 0;
      while (remaining > 0 && m_cursor_line > 0 && !m_in_alternate_screen) {
        --m_cursor_line;
        const std::size_t prev_cols = (m_cursor_line < m_lines.size())
                                          ? utf8_column_count(m_lines[m_cursor_line])
                                          : (m_columns > 0 ? m_columns - 1 : 0);
        if (remaining <= prev_cols) {
          m_cursor_column = prev_cols - remaining;
          remaining = 0;
        } else {
          remaining -= prev_cols;
          m_cursor_column = 0;
        }
      }
    }
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
      m_grid.emplace_back();
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
        m_grid.emplace_back();
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
      m_grid.emplace_back();
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
        m_grid.emplace_back();
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
        if (val == 25) {
          m_cursor_visible = true;
        } else if (val == 1049 || val == 47 || val == 1047) {
          if (!m_in_alternate_screen) {
            m_in_alternate_screen = true;
            m_main_screen_lines = m_lines;
            m_main_screen_grid = m_grid;
            m_main_cursor_line = m_cursor_line;
            m_main_cursor_column = m_cursor_column;
            const std::size_t count = std::max<std::size_t>(m_rows, 1);
            m_lines.assign(count, std::string{});
            m_grid.assign(count, std::vector<TerminalCell>{});
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
        if (val == 25) {
          m_cursor_visible = false;
        } else if (val == 1049 || val == 47 || val == 1047) {
          if (m_in_alternate_screen) {
            m_in_alternate_screen = false;
            m_lines = std::move(m_main_screen_lines);
            m_grid = std::move(m_main_screen_grid);
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
    m_saved_attributes = m_current_attributes;
    break;
  }
  case 'u': {
    m_cursor_line =
        std::min(m_saved_cursor_line, m_lines.empty() ? 0 : m_lines.size() - 1);
    m_cursor_column = m_saved_cursor_column;
    m_current_attributes = m_saved_attributes;
    break;
  }
  case 'K': {
    std::string &line = m_lines[m_cursor_line];
    auto &row_cells = m_grid[m_cursor_line];
    if (param1 == 2 || m_control_sequence == "2") {
      line.clear();
      row_cells.clear();
    } else if (param1 == 1 || m_control_sequence == "1") {
      std::vector<std::string> cps = split_utf8_codepoints(line);
      const std::size_t end = std::min(m_cursor_column + 1, cps.size());
      for (std::size_t idx = 0; idx < end; ++idx) {
        cps[idx] = " ";
      }
      line.clear();
      for (const auto &cp : cps)
        line.append(cp);
      const std::size_t grid_end = std::min(m_cursor_column + 1, row_cells.size());
      for (std::size_t idx = 0; idx < grid_end; ++idx) {
        row_cells[idx] = TerminalCell{" ", m_current_attributes};
      }
    } else {
      erase_utf8_from(line, m_cursor_column);
      if (m_cursor_column < row_cells.size()) {
        row_cells.resize(m_cursor_column);
      }
    }
    break;
  }
  case 'X': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    std::string &line = m_lines[m_cursor_line];
    auto &row_cells = m_grid[m_cursor_line];
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
    if (m_cursor_column < row_cells.size()) {
      const std::size_t erase_len =
          std::min(count, row_cells.size() - m_cursor_column);
      for (std::size_t idx = m_cursor_column; idx < m_cursor_column + erase_len;
           ++idx) {
        row_cells[idx] = TerminalCell{" ", m_current_attributes};
      }
    }
    break;
  }
  case 'P': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    std::string &line = m_lines[m_cursor_line];
    auto &row_cells = m_grid[m_cursor_line];
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
    if (m_cursor_column < row_cells.size()) {
      const std::size_t del_len =
          std::min(count, row_cells.size() - m_cursor_column);
      row_cells.erase(
          row_cells.begin() + static_cast<std::ptrdiff_t>(m_cursor_column),
          row_cells.begin() +
              static_cast<std::ptrdiff_t>(m_cursor_column + del_len));
    }
    break;
  }
  case '@': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    std::string &line = m_lines[m_cursor_line];
    auto &row_cells = m_grid[m_cursor_line];
    std::vector<std::string> cps = split_utf8_codepoints(line);
    if (m_cursor_column <= cps.size()) {
      cps.insert(cps.begin() + static_cast<std::ptrdiff_t>(m_cursor_column),
                 count, " ");
      line.clear();
      for (const auto &cp : cps)
        line.append(cp);
    }
    if (m_cursor_column <= row_cells.size()) {
      row_cells.insert(
          row_cells.begin() + static_cast<std::ptrdiff_t>(m_cursor_column),
          count, TerminalCell{" ", m_current_attributes});
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
      if (m_cursor_line < m_grid.size()) {
        const std::size_t del_grid =
            std::min(count, m_grid.size() - m_cursor_line);
        m_grid.erase(
            m_grid.begin() + static_cast<std::ptrdiff_t>(m_cursor_line),
            m_grid.begin() +
                static_cast<std::ptrdiff_t>(m_cursor_line + del_grid));
      }
      if (m_in_alternate_screen) {
        m_lines.resize(std::max<std::size_t>(m_rows, 1));
        m_grid.resize(std::max<std::size_t>(m_rows, 1));
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
      if (m_cursor_line <= m_grid.size()) {
        m_grid.insert(m_grid.begin() +
                          static_cast<std::ptrdiff_t>(m_cursor_line),
                      count, std::vector<TerminalCell>{});
      }
      if (m_in_alternate_screen && m_lines.size() > m_rows) {
        m_lines.resize(m_rows);
        m_grid.resize(m_rows);
      }
    }
    break;
  }
  case 'J': {
    if (param1 == 2 || m_control_sequence == "2") {
      if (m_in_alternate_screen) {
        const std::size_t count = std::max<std::size_t>(m_rows, 1);
        m_lines.assign(count, std::string{});
        m_grid.assign(count, std::vector<TerminalCell>{});
      } else {
        clear_screen();
      }
    } else if (param1 == 3 || m_control_sequence == "3") {
      trim_scrollback();
    } else if (param1 == 1 || m_control_sequence == "1") {
      if (m_cursor_line < m_lines.size()) {
        for (std::size_t r = 0; r < m_cursor_line; ++r) {
          m_lines[r].clear();
          if (r < m_grid.size())
            m_grid[r].clear();
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

        if (m_cursor_line < m_grid.size()) {
          auto &row_cells = m_grid[m_cursor_line];
          const std::size_t grid_end =
              std::min(m_cursor_column + 1, row_cells.size());
          for (std::size_t idx = 0; idx < grid_end; ++idx) {
            row_cells[idx] = TerminalCell{" ", m_current_attributes};
          }
        }
      }
    } else {
      if (m_cursor_line < m_lines.size()) {
        erase_utf8_from(m_lines[m_cursor_line], m_cursor_column);
        if (m_cursor_line < m_grid.size() &&
            m_cursor_column < m_grid[m_cursor_line].size()) {
          m_grid[m_cursor_line].resize(m_cursor_column);
        }
        if (m_in_alternate_screen) {
          for (std::size_t r = m_cursor_line + 1; r < m_lines.size(); ++r) {
            m_lines[r].clear();
            if (r < m_grid.size())
              m_grid[r].clear();
          }
        } else if (m_cursor_line + 1 < m_lines.size()) {
          m_lines.erase(m_lines.begin() +
                            static_cast<std::ptrdiff_t>(m_cursor_line + 1),
                        m_lines.end());
          if (m_cursor_line + 1 < m_grid.size()) {
            m_grid.erase(m_grid.begin() +
                             static_cast<std::ptrdiff_t>(m_cursor_line + 1),
                         m_grid.end());
          }
        }
      }
    }
    break;
  }
  case 'S': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    if (count >= m_lines.size()) {
      m_lines.assign(m_in_alternate_screen ? m_rows : 1, std::string{});
      m_grid.assign(m_in_alternate_screen ? m_rows : 1,
                    std::vector<TerminalCell>{});
    } else {
      m_lines.erase(m_lines.begin(),
                    m_lines.begin() + static_cast<std::ptrdiff_t>(count));
      if (m_grid.size() >= count) {
        m_grid.erase(m_grid.begin(),
                     m_grid.begin() + static_cast<std::ptrdiff_t>(count));
      }
      if (m_in_alternate_screen) {
        m_lines.resize(m_rows);
        m_grid.resize(m_rows);
      }
    }
    break;
  }
  case 'T':
  case '^': {
    const std::size_t count = param1 > 0 ? param1 : 1;
    m_lines.insert(m_lines.begin(), count, std::string{});
    m_grid.insert(m_grid.begin(), count, std::vector<TerminalCell>{});
    if (m_in_alternate_screen && m_lines.size() > m_rows) {
      m_lines.resize(m_rows);
      m_grid.resize(m_rows);
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
    if (!m_control_sequence.empty() && m_control_sequence.front() == '>') {
      send_pty_response("\x1b[>0;10;0c");
    } else {
      send_pty_response("\x1b[?62;1;2;6;7;8;9c");
    }
    break;
  }
  case 't': {
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
    m_grid.emplace_back();
    m_cursor_line = 0;
  }
  while (m_cursor_line >= m_lines.size()) {
    m_lines.emplace_back();
    m_grid.emplace_back();
  }
  if (m_grid.size() < m_lines.size()) {
    m_grid.resize(m_lines.size());
  }

  if (m_cursor_column >= m_columns) {
    if (m_in_alternate_screen) {
      if (m_cursor_line + 1 < m_rows) {
        ++m_cursor_line;
        while (m_cursor_line >= m_lines.size()) {
          m_lines.emplace_back();
          m_grid.emplace_back();
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
        m_grid.emplace_back();
        m_cursor_line = m_lines.size() - 1;
      }
      m_cursor_column = 0;
      trim_scrollback();
    }
  }

  set_utf8_cell(m_lines[m_cursor_line], m_cursor_column, utf8_char);

  auto &row_cells = m_grid[m_cursor_line];
  if (m_cursor_column >= row_cells.size()) {
    row_cells.resize(m_cursor_column,
                     TerminalCell{" ", m_current_attributes});
    row_cells.push_back(
        TerminalCell{std::string(utf8_char), m_current_attributes});
  } else {
    row_cells[m_cursor_column] =
        TerminalCell{std::string(utf8_char), m_current_attributes};
  }

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
        m_grid.emplace_back();
      }
    }
    m_cursor_column = 0;
  } else {
    m_lines.emplace_back();
    m_grid.emplace_back();
    m_cursor_line = m_lines.size() - 1;
    m_cursor_column = 0;
    trim_scrollback();
  }
}

void TerminalSession::append_status(std::string message) {
  if (!m_lines.empty() && m_lines.back().empty()) {
    m_lines.back() = std::move(message);
    if (!m_grid.empty()) {
      m_grid.back().clear();
    }
  } else {
    m_lines.push_back(std::move(message));
    m_grid.emplace_back();
  }
  append_line();
}

void TerminalSession::clear_screen() noexcept {
  if (m_in_alternate_screen) {
    const std::size_t count = std::max<std::size_t>(m_rows, 1);
    m_lines.assign(count, std::string{});
    m_grid.assign(count, std::vector<TerminalCell>{});
  } else {
    m_lines.assign(1, std::string{});
    m_grid.assign(1, std::vector<TerminalCell>{});
  }
  m_cursor_line = 0;
  m_cursor_column = 0;
  m_saved_cursor_line = 0;
  m_saved_cursor_column = 0;
  m_saved_attributes = TerminalCellAttributes{};
  m_parser_state = ParserState::Text;
  m_control_sequence.clear();
  m_osc_payload.clear();
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
  if (m_grid.size() >= remove_count) {
    m_grid.erase(m_grid.begin(),
                 m_grid.begin() + static_cast<std::ptrdiff_t>(remove_count));
  }
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

bool TerminalSession::send_mouse_scroll(std::ptrdiff_t line_delta,
                                        std::size_t column, std::size_t row) {
  if (!m_running || line_delta == 0) {
    return false;
  }

  if (m_mouse_tracking != MouseTracking::Off) {
    const int button = (line_delta < 0) ? 64 : 65;
    const std::size_t steps =
        std::clamp<std::size_t>(std::abs(line_delta) / 3, 1, 5);
    if (m_sgr_mouse) {
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
      const char cx = static_cast<char>(32 + std::clamp<std::size_t>(column, 1, 223));
      const char cy = static_cast<char>(32 + std::clamp<std::size_t>(row, 1, 223));
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
    const char cx = static_cast<char>(32 + std::clamp<std::size_t>(col, 1, 223));
    const char cy = static_cast<char>(32 + std::clamp<std::size_t>(r, 1, 223));
    std::string seq = "\x1B[M";
    seq.push_back(cb);
    seq.push_back(cx);
    seq.push_back(cy);
    return write_input(seq);
  }
}

} // namespace Zenvra::Terminal
