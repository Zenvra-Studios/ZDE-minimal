#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "Platform/Win32/Utility/Win32Compat.h"
#include "Commands/CommandIds.h"
#include "Language/LanguageServerManager.h"
#include "Services/Installation/Catalog/CatalogLoader.h"
#include "Platform/HostSystem.h"
#include "Platform/PlatformDialogs.h"
#include "Plugins/PluginManager.h"
#include "Settings/SettingsService.h"
#include "UI/Theme/ThemeManager.h"
#include "Utility/Antialiasing.h"
#include "Utility/stb_image.h"

#include "UI/Editor/EditorFileSystem.h"
#include "Utility/Ascii/AsciiMascotRenderer.h"
#include "Utility/MathUtil.h"
#include "Utility/Shadows.h"
#include "Utility/Fonts.h"
#include "Utility/TextEncoding.h"

#include <lunasvg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Msimg32.lib")

namespace Zenvra::Platform::Win32::Components {

namespace {

// Reads big-endian 16-bit and 32-bit integers from byte streams (OpenType spec)
static inline uint16_t read_be16(const uint8_t *p) noexcept {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static inline uint32_t read_be32(const uint8_t *p) noexcept {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

// Normalizes font name for fuzzy comparison (strips whitespace, punctuation, lowercases)
static std::string normalize_font_name(std::string_view name) {
  std::string result;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return result;
}

// Extracts font family name and typographic family name from TTF/OTF files
static std::vector<std::string> extract_ttf_family_names(const std::filesystem::path &path) {
  std::vector<std::string> result;
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return result;

  uint8_t header[12];
  if (!f.read(reinterpret_cast<char *>(header), sizeof(header))) return result;

  const uint16_t num_tables = read_be16(header + 4);
  if (num_tables == 0 || num_tables > 128) return result;

  uint32_t name_table_offset = 0;
  uint32_t name_table_length = 0;

  for (uint16_t i = 0; i < num_tables; ++i) {
    uint8_t entry[16];
    if (!f.read(reinterpret_cast<char *>(entry), sizeof(entry))) return result;
    if (std::memcmp(entry, "name", 4) == 0) {
      name_table_offset = read_be32(entry + 8);
      name_table_length = read_be32(entry + 12);
      break;
    }
  }

  if (name_table_offset == 0 || name_table_length == 0 || name_table_length > 1024 * 1024) {
    return result;
  }

  f.seekg(name_table_offset, std::ios::beg);
  std::vector<uint8_t> table_data(name_table_length);
  if (!f.read(reinterpret_cast<char *>(table_data.data()), name_table_length)) {
    return result;
  }

  if (table_data.size() < 6) return result;

  const uint16_t count = read_be16(table_data.data() + 2);
  const uint16_t string_offset = read_be16(table_data.data() + 4);

  for (uint16_t i = 0; i < count; ++i) {
    const size_t rec_offset = 6 + i * 12;
    if (rec_offset + 12 > table_data.size()) break;

    const uint8_t *rec = table_data.data() + rec_offset;
    const uint16_t platform_id = read_be16(rec);
    const uint16_t encoding_id = read_be16(rec + 2);
    const uint16_t name_id = read_be16(rec + 6);
    const uint16_t length = read_be16(rec + 8);
    const uint16_t offset = read_be16(rec + 10);

    // name_id 1 = Font Family, name_id 4 = Full Name, name_id 16 = Typographic Family
    if (name_id != 1 && name_id != 4 && name_id != 16) continue;

    const size_t str_pos = static_cast<size_t>(string_offset) + offset;
    if (str_pos + length > table_data.size()) continue;

    const uint8_t *str_bytes = table_data.data() + str_pos;
    std::string name_utf8;

    if (platform_id == 3 || platform_id == 0) {
      // Windows Unicode BMP or Unicode: UTF-16BE
      std::wstring wstr;
      for (size_t c = 0; c + 1 < length; c += 2) {
        wchar_t ch = static_cast<wchar_t>(read_be16(str_bytes + c));
        if (ch != 0) wstr.push_back(ch);
      }
      if (!wstr.empty()) {
        int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
        if (u8len > 0) {
          name_utf8.resize(u8len);
          WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), &name_utf8[0], u8len, nullptr, nullptr);
        }
      }
    } else if (platform_id == 1 && encoding_id == 0) {
      // Macintosh Roman ASCII
      name_utf8.assign(reinterpret_cast<const char *>(str_bytes), length);
    }

    if (!name_utf8.empty()) {
      if (std::find(result.begin(), result.end(), name_utf8) == result.end()) {
        result.push_back(name_utf8);
      }
    }
  }

  return result;
}

// Static registry of font family names parsed from bundled TTF/OTF files
static std::vector<std::string> g_bundled_font_names;
// Static catalog of all available font families (OS fonts + process private fonts)
static std::vector<std::string> g_all_available_fonts;
static bool g_fonts_enumerated = false;
static std::vector<std::wstring> g_registered_font_files;

static void ensure_bundled_fonts_loaded() {
  std::vector<std::filesystem::path> font_search_dirs;

  auto add_font_search_root = [&](const std::filesystem::path &root) {
    if (root.empty())
      return;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
      return;
    const std::filesystem::path candidates[] = {
        root / "Assets" / "fonts",
        root / "Resources" / "fonts",
        root / "Assets" / "fonts" / "JetBrainsMono",
        root / "Resources" / "fonts" / "JetBrainsMono",
        root / "fonts",
        root / "fonts" / "JetBrainsMono"};
    for (const auto &p : candidates) {
      ec.clear();
      if (std::filesystem::is_directory(p, ec)) {
        if (std::find(font_search_dirs.begin(), font_search_dirs.end(), p) ==
            font_search_dirs.end()) {
          font_search_dirs.push_back(p);
        }
      }
    }
  };

  std::error_code ec;
  std::filesystem::path exe_dir;
  std::array<wchar_t, 32768> executable_path{};
  const DWORD length =
      GetModuleFileNameW(nullptr, executable_path.data(),
                         static_cast<DWORD>(executable_path.size()));
  if (length > 0 && length < executable_path.size()) {
    exe_dir = std::filesystem::path{executable_path.data()}.parent_path();
  }

  const std::filesystem::path current_path =
      std::filesystem::current_path(ec);
  ec.clear();

  std::optional<std::filesystem::path> project_root;
  if (!exe_dir.empty()) {
    project_root = UI::Editor::EditorFileSystem::find_project_root(exe_dir);
  }
  if (!project_root && !current_path.empty()) {
    project_root = UI::Editor::EditorFileSystem::find_project_root(current_path);
  }

  if (project_root)
    add_font_search_root(*project_root);
  add_font_search_root(exe_dir);
  add_font_search_root(current_path);

  // Traverse ancestor paths up to 6 levels to locate workspace source trees
  std::filesystem::path cur = exe_dir;
  for (int i = 0; i < 6 && !cur.empty(); ++i) {
    add_font_search_root(cur);
    const auto parent = cur.parent_path();
    if (parent == cur)
      break;
    cur = parent;
  }

  cur = current_path;
  for (int i = 0; i < 6 && !cur.empty(); ++i) {
    add_font_search_root(cur);
    const auto parent = cur.parent_path();
    if (parent == cur)
      break;
    cur = parent;
  }

  // Load and privately register every TTF and OTF file found
  for (const auto &fdir : font_search_dirs) {
    std::error_code dir_ec;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
             fdir,
             std::filesystem::directory_options::skip_permission_denied,
             dir_ec)) {
      if (dir_ec)
        break;
      std::error_code file_ec;
      if (entry.is_regular_file(file_ec)) {
        const auto ext = entry.path().extension().string();
        std::string ext_lower = ext;
        for (char &c : ext_lower) {
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext_lower == ".ttf" || ext_lower == ".otf") {
          std::error_code sz_err;
          if (std::filesystem::file_size(entry.path(), sz_err) > 100) {
            std::error_code abs_err;
            const auto abs_path =
                std::filesystem::absolute(entry.path(), abs_err);
            const std::wstring wpath = abs_path.wstring();
            if (std::find(g_registered_font_files.begin(),
                          g_registered_font_files.end(),
                          wpath) == g_registered_font_files.end()) {
              g_registered_font_files.push_back(wpath);
              AddFontResourceExW(wpath.c_str(), FR_PRIVATE, nullptr);
              auto names = extract_ttf_family_names(abs_path);
              for (auto &n : names) {
                if (std::find(g_bundled_font_names.begin(),
                              g_bundled_font_names.end(),
                              n) == g_bundled_font_names.end()) {
                  g_bundled_font_names.push_back(std::move(n));
                }
              }
            }
          }
        }
      }
    }
  }
}

static void refresh_available_fonts() {
  ensure_bundled_fonts_loaded();

  g_all_available_fonts.clear();
  HDC dc = GetDC(nullptr);
  if (dc) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfFaceName[0] = L'\0'; // An empty string tells GDI to enumerate ALL font families in the OS & process!
    auto proc = [](const LOGFONTW *lpelfe, const TEXTMETRICW *, DWORD, LPARAM lParam) -> int {
      if (lpelfe && lpelfe->lfFaceName[0] != L'\0') {
        auto *vec = reinterpret_cast<std::vector<std::string> *>(lParam);
        char u8[LF_FACESIZE * 3]{};
        int len = WideCharToMultiByte(CP_UTF8, 0, lpelfe->lfFaceName, -1, u8, sizeof(u8) - 1, nullptr, nullptr);
        if (len > 0) {
          std::string name(u8);
          if (std::find(vec->begin(), vec->end(), name) == vec->end()) {
            vec->push_back(std::move(name));
          }
        }
      }
      return 1;
    };
    EnumFontFamiliesExW(dc, &lf, proc, reinterpret_cast<LPARAM>(&g_all_available_fonts), 0);
    ReleaseDC(nullptr, dc);
  }

  // Merge in bundled font names detected from TTF metadata
  for (const auto &b : g_bundled_font_names) {
    if (std::find(g_all_available_fonts.begin(), g_all_available_fonts.end(), b) == g_all_available_fonts.end()) {
      g_all_available_fonts.push_back(b);
    }
  }
  g_fonts_enumerated = true;
}

static bool is_font_available(const std::string &font_name) {
  if (font_name.empty())
    return false;
  if (!g_fonts_enumerated) {
    refresh_available_fonts();
  }

  // 1. Exact case-insensitive match
  for (const auto &fam : g_all_available_fonts) {
    if (_stricmp(fam.c_str(), font_name.c_str()) == 0) {
      return true;
    }
  }

  // 2. Normalized match (ignoring spaces, hyphens, and casing)
  const std::string norm_target = normalize_font_name(font_name);
  if (!norm_target.empty()) {
    for (const auto &fam : g_all_available_fonts) {
      if (normalize_font_name(fam) == norm_target) {
        return true;
      }
    }
  }

  // 3. Substring match for known variants (JetBrains Mono, Fira Code, etc.)
  if (norm_target.find("jetbrainsmono") != std::string::npos ||
      norm_target.find("jetbrains") != std::string::npos) {
    for (const auto &fam : g_all_available_fonts) {
      if (normalize_font_name(fam).find("jetbrainsmono") != std::string::npos ||
          normalize_font_name(fam).find("jetbrains") != std::string::npos) {
        return true;
      }
    }
  }

  // 4. Fallback: Direct test with GDI CreateFontW & GetTextFaceW
  HDC dc = GetDC(nullptr);
  if (!dc)
    return false;
  std::wstring wtarget = Utility::utf8_to_wide(font_name).value_or(L"");
  if (wtarget.empty()) {
    ReleaseDC(nullptr, dc);
    return false;
  }

  HFONT test_font = CreateFontW(
      -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wtarget.c_str());
  bool found = false;
  if (test_font) {
    HGDIOBJ old_font = SelectObject(dc, test_font);
    wchar_t actual_face[LF_FACESIZE]{};
    GetTextFaceW(dc, LF_FACESIZE, actual_face);
    SelectObject(dc, old_font);
    DeleteObject(test_font);
    if (_wcsicmp(actual_face, wtarget.c_str()) == 0) {
      found = true;
    }
  }
  ReleaseDC(nullptr, dc);
  return found;
}

using Zenvra::Utility::round_to_int;

COLORREF to_color_ref(const UI::Theme::Color &color) {
  return RGB(color.red, color.green, color.blue);
}

std::string to_font_color(const UI::Theme::Color &color) {
  char value[8]{};
  std::snprintf(value, sizeof(value), "#%02x%02x%02x",
                static_cast<unsigned int>(color.red),
                static_cast<unsigned int>(color.green),
                static_cast<unsigned int>(color.blue));
  return value;
}

RECT to_native_rect(const UI::Rect &rectangle) {
  return RECT{
      round_to_int(rectangle.x),
      round_to_int(rectangle.y),
      round_to_int(rectangle.right()),
      round_to_int(rectangle.bottom()),
  };
}

} // namespace

StudioWorkspaceRenderer::StudioWorkspaceRenderer() = default;

StudioWorkspaceRenderer::~StudioWorkspaceRenderer() { shutdown(); }

bool StudioWorkspaceRenderer::initialize(UINT dpi) {
  shutdown();
  m_dpi = std::max(dpi, 48U);
  m_dpi_scale = static_cast<float>(m_dpi) / 96.0F;

  std::error_code path_error;
  std::filesystem::path exe_dir;
  std::array<wchar_t, 32768> executable_path{};
  const DWORD length =
      GetModuleFileNameW(nullptr, executable_path.data(),
                         static_cast<DWORD>(executable_path.size()));
  if (length > 0 && length < executable_path.size()) {
    exe_dir = std::filesystem::path{executable_path.data()}.parent_path();
  }

  const std::filesystem::path current_path =
      std::filesystem::current_path(path_error);
  std::optional<std::filesystem::path> project_root;
  if (!path_error) {
    project_root =
        UI::Editor::EditorFileSystem::find_project_root(current_path);
  }
  if (!project_root && !exe_dir.empty()) {
    project_root = UI::Editor::EditorFileSystem::find_project_root(exe_dir);
  }

  // 1. Resolve Icon Directory (prioritizes project_root/Assets/icons)
  const std::vector<std::filesystem::path> icon_candidates = {
      project_root ? (*project_root / "Assets" / "icons")
                   : std::filesystem::path{},
      project_root ? (*project_root / "Resources" / "icons")
                   : std::filesystem::path{},
      exe_dir / "Assets" / "icons",
      exe_dir / "Resources" / "icons",
      exe_dir.parent_path() / "Assets" / "icons",
      exe_dir.parent_path() / "Resources" / "icons",
      current_path / "Assets" / "icons",
      current_path / "Resources" / "icons",
  };
  for (const auto &candidate : icon_candidates) {
    std::error_code icon_ec;
    if (!candidate.empty() &&
        std::filesystem::is_directory(candidate, icon_ec)) {
      m_icon_asset_root = candidate;
      break;
    }
  }

  // 2. Discover, register and enumerate fonts (both bundled Assets/fonts and OS fonts)
  ensure_bundled_fonts_loaded();
  refresh_available_fonts();

  m_editor_font_name = resolve_font_family_name("'JetBrains Mono', Consolas, monospace");
  m_ui_font_name = is_font_available("Open Sans") ? "Open Sans" : "Segoe UI";

  auto &settings_service = Settings::SettingsService::instance();
  settings_service.initialize();
  update_theme(UI::Theme::ThemeManager::instance().get_current_theme());

  const auto existing_workspace =
      m_tool_sidebar.get_model().get_workspace_root();
  if (!existing_workspace.empty()) {
    settings_service.set_workspace_path(existing_workspace);
  }

  const int configured_size = settings_service.get<int>("editor.fontSize");
  const int effective_size =
      (configured_size >= 8 && configured_size <= 72) ? configured_size : 14;
  const std::string configured_font =
      settings_service.get<std::string>("editor.fontFamily");
  if (!configured_font.empty()) {
    m_editor_font_name = resolve_font_family_name(configured_font);
  }

  const int term_size = settings_service.get<int>("terminal.fontSize");
  const int eff_term_size =
      (term_size >= 8 && term_size <= 72) ? term_size : 14;
  const std::string term_font =
      settings_service.get<std::string>("terminal.fontFamily");
  m_terminal_font_name = !term_font.empty()
                             ? resolve_font_family_name(term_font)
                             : m_editor_font_name;

  const std::string ui_font =
      settings_service.get<std::string>("ui.fontFamily");
  if (!ui_font.empty() && is_font_available(ui_font)) {
    m_ui_font_name = ui_font;
  } else {
    m_ui_font_name = is_font_available("Open Sans") ? "Open Sans" : "Segoe UI";
  }
  const int ui_size = settings_service.get<int>("ui.fontSize");
  const int effective_ui_size = (ui_size >= 8 && ui_size <= 48) ? ui_size : 12;

  m_ui_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(static_cast<float>(effective_ui_size) * m_dpi_scale), 9));
  m_small_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(static_cast<float>(effective_ui_size) * m_dpi_scale), 9));
  m_editor_font = std::make_unique<AntialiasedFont>(
      m_editor_font_name,
      std::max(round_to_int(static_cast<float>(effective_size) * m_dpi_scale),
               10));
  const bool editor_ligatures =
      settings_service.has("editor.fontLigatures")
          ? settings_service.get<bool>("editor.fontLigatures")
          : true;
  m_editor_font->setLigaturesEnabled(editor_ligatures);
  m_terminal_font = std::make_unique<AntialiasedFont>(
      m_terminal_font_name,
      std::max(round_to_int(static_cast<float>(eff_term_size) * m_dpi_scale),
               10));
  const bool term_ligatures =
      settings_service.has("terminal.fontLigatures")
          ? settings_service.get<bool>("terminal.fontLigatures")
          : true;
  m_terminal_font->setLigaturesEnabled(term_ligatures);
  m_minimap_font = std::make_unique<AntialiasedFont>(
      m_editor_font_name, std::max(round_to_int(3.0F * m_dpi_scale), 3));
  m_large_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(24.0F * m_dpi_scale), 18), FW_BOLD);
  if (!m_ui_font->isValid() || !m_small_font->isValid() ||
      !m_editor_font->isValid() || !m_terminal_font->isValid() ||
      !m_minimap_font->isValid() || !m_large_font->isValid()) {
    shutdown();
    return false;
  }

  // Synchronize TextEditor settings on startup
  m_text_editor.set_tab_size(static_cast<std::size_t>(
      std::max(1, settings_service.get<int>("editor.tabSize"))));
  m_text_editor.set_cursor_style(
      settings_service.get<std::string>("editor.cursorStyle"));
  m_text_editor.set_render_whitespace(
      settings_service.get<std::string>("editor.renderWhitespace"));

  const std::string interaction_mode =
      settings_service.get<std::string>("editor.interactionMode");
  const bool vim_enabled = settings_service.get<bool>("vim.enabled");
  if (interaction_mode == "vim" || vim_enabled) {
    m_text_editor.get_input_router().set_interaction_mode(
        Editors::InteractionMode::Vim);
  } else {
    m_text_editor.get_input_router().set_interaction_mode(
        Editors::InteractionMode::Default);
  }
  const std::string line_numbers =
      settings_service.get<std::string>("editor.lineNumbers");
  m_text_editor.set_relative_line_numbers(
      line_numbers == "relative" ||
      settings_service.get<bool>("vim.relativeLineNumbers"));
  if (auto *vim = m_text_editor.get_input_router().get_vim_mode()) {
    const int timeout_ms = settings_service.get<int>("vim.timeout");
    vim->get_state().timeout_seconds =
        static_cast<double>(std::max(100, timeout_ms)) / 1000.0;
    vim->get_state().leader_key =
        settings_service.get<std::string>("vim.leaderKey");
    vim->get_state().escape_key =
        settings_service.get<std::string>("vim.escapeKey");
    const std::string start_mode =
        settings_service.get<std::string>("vim.startMode");
    if (start_mode == "insert") {
      vim->set_mode(Editors::VimMode::Insert);
    }
  }

  static_cast<void>(settings_service.subscribe(
      [this, &settings_service](const Settings::SettingsChangedEvent &event) {
        if (event.id == "editor.fontSize" || event.id == "editor.fontFamily" ||
            event.id == "editor.lineHeight" || event.id == "editor.fontLigatures") {
          reload_editor_font();
        } else if (event.id == "terminal.fontSize" ||
                   event.id == "terminal.fontFamily" ||
                   event.id == "terminal.fontLigatures") {
          reload_terminal_font();
        } else if (event.id == "ui.fontSize" ||
                   event.id == "ui.fontFamily") {
          reload_ui_font();
        } else if (event.id == "editor.tabSize") {
          const int ts = settings_service.get<int>("editor.tabSize");
          m_text_editor.set_tab_size(static_cast<std::size_t>(std::max(1, ts)));
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "editor.cursorStyle") {
          m_text_editor.set_cursor_style(
              settings_service.get<std::string>("editor.cursorStyle"));
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "editor.renderWhitespace") {
          m_text_editor.set_render_whitespace(
              settings_service.get<std::string>("editor.renderWhitespace"));
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "editor.interactionMode") {
          const std::string im =
              settings_service.get<std::string>("editor.interactionMode");
          if (im == "vim") {
            m_text_editor.get_input_router().set_interaction_mode(
                Editors::InteractionMode::Vim);
            if (!settings_service.get<bool>("vim.enabled")) {
              settings_service.set("vim.enabled", true);
            }
          } else {
            m_text_editor.get_input_router().set_interaction_mode(
                Editors::InteractionMode::Default);
            if (settings_service.get<bool>("vim.enabled")) {
              settings_service.set("vim.enabled", false);
            }
          }
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "vim.enabled") {
          const bool ve = settings_service.get<bool>("vim.enabled");
          if (ve) {
            m_text_editor.get_input_router().set_interaction_mode(
                Editors::InteractionMode::Vim);
            if (settings_service.get<std::string>("editor.interactionMode") !=
                "vim") {
              settings_service.set("editor.interactionMode",
                                   std::string("vim"));
            }
          } else {
            m_text_editor.get_input_router().set_interaction_mode(
                Editors::InteractionMode::Default);
            if (settings_service.get<std::string>("editor.interactionMode") !=
                "default") {
              settings_service.set("editor.interactionMode",
                                   std::string("default"));
            }
          }
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "editor.lineNumbers") {
          const std::string ln =
              settings_service.get<std::string>("editor.lineNumbers");
          if (ln == "relative") {
            m_text_editor.set_relative_line_numbers(true);
          } else {
            m_text_editor.set_relative_line_numbers(
                settings_service.get<bool>("vim.relativeLineNumbers"));
          }
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "vim.relativeLineNumbers") {
          const std::string ln =
              settings_service.get<std::string>("editor.lineNumbers");
          if (ln != "relative") {
            m_text_editor.set_relative_line_numbers(
                settings_service.get<bool>("vim.relativeLineNumbers"));
          }
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "vim.showModeIndicator") {
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "vim.timeout") {
          if (auto *vim = m_text_editor.get_input_router().get_vim_mode()) {
            const int timeout_ms = settings_service.get<int>("vim.timeout");
            vim->get_state().timeout_seconds =
                static_cast<double>(std::max(100, timeout_ms)) / 1000.0;
          }
        } else if (event.id == "vim.leaderKey") {
          if (auto *vim = m_text_editor.get_input_router().get_vim_mode()) {
            vim->get_state().leader_key =
                settings_service.get<std::string>("vim.leaderKey");
          }
        } else if (event.id == "vim.escapeKey") {
          if (auto *vim = m_text_editor.get_input_router().get_vim_mode()) {
            vim->get_state().escape_key =
                settings_service.get<std::string>("vim.escapeKey");
          }
        } else if (event.id == "vim.startMode") {
          if (auto *vim = m_text_editor.get_input_router().get_vim_mode()) {
            const std::string start_mode =
                settings_service.get<std::string>("vim.startMode");
            if (start_mode == "insert") {
              vim->set_mode(Editors::VimMode::Insert);
            } else {
              vim->set_mode(Editors::VimMode::Normal);
            }
          }
          if (m_window_handle)
            InvalidateRect(m_window_handle, nullptr, FALSE);
        } else if (event.id == "editor.minimap.enabled" ||
                   event.id == "workbench.activityBar.visible" ||
                   event.id == "workbench.activityBar.location" ||
                   event.id == "workbench.sidebar.position" ||
                   event.id == "theme.current" ||
                   event.id == "workbench.mascot.image" ||
                   event.id == "workbench.mascot.renderMode" ||
                   event.id == "workbench.app.title") {
          Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
          Utility::Ascii::AsciiArtConverter::clear_cache();
          update_theme(UI::Theme::ThemeManager::instance().get_current_theme());
          if (m_window_handle) {
            PostMessageW(m_window_handle, WM_THEMECHANGED, 0, 0);
            InvalidateRect(m_window_handle, nullptr, FALSE);
            UpdateWindow(m_window_handle);
          }
        }
      }));

  if (existing_workspace.empty()) {
    static_cast<void>(m_tool_sidebar.initialize());
  } else {
    static_cast<void>(m_tool_sidebar.set_workspace_root(existing_workspace));
  }
  const auto active_workspace = m_tool_sidebar.get_model().get_workspace_root();
  if (!active_workspace.empty()) {
    m_terminal_panel.set_working_directory(active_workspace);
  } else {
    m_terminal_panel.set_working_directory(
        Platform::HostSystem::get_user_home_directory());
  }
  m_terminal_panel.set_focused(false);
  static_cast<void>(m_shader_sandbox_panel.initialize());

  Language::LanguageServerManager::instance().set_missing_server_callback(
      [this](const std::string &lang_id, const std::string &lang_name, const std::string &server_name) {
        show_missing_lsp_dialog(lang_id, lang_name, server_name);
      });

  m_text_editor.sync_lsp_active_document();

  return true;
}

void StudioWorkspaceRenderer::update_dpi(UINT dpi) {
  m_dpi = std::max(dpi, 48U);
  m_dpi_scale = static_cast<float>(m_dpi) / 96.0F;

  auto &settings_service = Settings::SettingsService::instance();
  const int ui_size = settings_service.get<int>("ui.fontSize");
  const int effective_ui_size = (ui_size >= 8 && ui_size <= 48) ? ui_size : 12;

  m_ui_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(static_cast<float>(effective_ui_size) * m_dpi_scale), 9));
  m_small_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(static_cast<float>(effective_ui_size) * m_dpi_scale), 9));
  m_large_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(24.0F * m_dpi_scale), 18), FW_BOLD);

  reload_editor_font();
  reload_terminal_font();

  m_svg_cache.clear();
}

UI::Editor::StudioEditorLayoutResult
StudioWorkspaceRenderer::calculate_layout(int client_width, int client_height,
                                          float content_top) const noexcept {
  const std::size_t line_count = m_text_editor.get_active_document_line_count();
  const std::optional<float> custom_nav =
      m_file_buffer_bounds.has_value()
          ? std::optional<float>(m_file_buffer_bounds->x)
          : std::nullopt;
  const std::optional<float> custom_tab_w =
      m_file_buffer_bounds.has_value()
          ? std::optional<float>(m_file_buffer_bounds->width)
          : std::nullopt;
  auto &settings = Settings::SettingsService::instance();
  std::string activity_bar_loc = "Default";
  if (settings.has("workbench.activityBar.location")) {
    activity_bar_loc = settings.get<std::string>("workbench.activityBar.location");
  }
  bool activity_bar_visible = true;
  if (settings.has("workbench.activityBar.visible")) {
    activity_bar_visible = settings.get<bool>("workbench.activityBar.visible");
  }
  if (!activity_bar_visible || activity_bar_loc == "Hidden") {
    activity_bar_loc = "Hidden";
  }

  bool sidebar_on_right = false;
  if (settings.has("workbench.sidebar.position")) {
    const std::string sidebar_pos = settings.get<std::string>("workbench.sidebar.position");
    sidebar_on_right = (sidebar_pos == "Right" || sidebar_pos == "right");
  }

  auto result = m_layout_engine.calculate(
      static_cast<float>(client_width), static_cast<float>(client_height),
      content_top, m_dpi_scale, m_terminal_panel.is_visible(),
      m_terminal_panel.get_height(), m_terminal_panel.is_maximized(),
      m_tool_sidebar.is_visible(), m_tool_sidebar.get_width(),
      m_shader_sandbox_panel.is_visible(), m_shader_sandbox_panel.get_width(),
      custom_nav, line_count, custom_tab_w,
      activity_bar_loc, sidebar_on_right);
  if (m_file_buffer_bounds.has_value() && !m_file_buffer_bounds->is_empty()) {
    result.tab_bar_bounds.x = m_file_buffer_bounds->x;
    result.tab_bar_bounds.width = m_file_buffer_bounds->width;
  }

  const bool is_modern =
      m_palette.is_modern || m_theme.is_modern || m_theme.enable_os_blur;
  if (is_modern) {
    const float scale = result.dpi_scale;
    const float sep_gap = std::max(std::round(10.0F * scale), 10.0F);
    const float outer_gap = std::max(std::round(6.0F * scale), 5.0F);

    const bool has_sidebar =
        m_tool_sidebar.is_visible() && !result.tool_sidebar_bounds.is_empty();
    const bool sidebar_is_right =
        has_sidebar && (result.tool_sidebar_bounds.x > result.editor_bounds.x);

    float editor_col_left = outer_gap;
    float editor_col_right = result.workspace_bounds.right() - outer_gap;
    const float shader_split_x =
        result.shader_splitter_bounds.x + result.shader_splitter_bounds.width * 0.5F;

    if (sidebar_is_right) {
      if (!result.activity_bar_bounds.is_empty() &&
          result.activity_bar_bounds.width < 100.0F &&
          result.activity_bar_bounds.x < result.editor_bounds.x) {
        editor_col_left = result.activity_bar_bounds.right() + outer_gap;
      } else {
        editor_col_left = outer_gap;
      }
      if (result.shader_panel_visible) {
        editor_col_right = shader_split_x - sep_gap * 0.5F;
      } else {
        editor_col_right = result.tool_sidebar_bounds.x - sep_gap * 0.5F;
      }
    } else {
      editor_col_left =
          has_sidebar ? (result.tool_sidebar_bounds.right() + sep_gap * 0.5F)
                      : (!result.activity_bar_bounds.is_empty() &&
                                 result.activity_bar_bounds.width < 100.0F
                             ? (result.activity_bar_bounds.right() + outer_gap)
                             : outer_gap);

      editor_col_right = result.shader_panel_visible
                             ? (shader_split_x - sep_gap * 0.5F)
                             : (result.workspace_bounds.right() - outer_gap);
    }

    const float editor_col_top = (result.editor_header_bounds.is_empty()
                                      ? result.gutter_bounds.y
                                      : result.editor_header_bounds.y) +
                                 outer_gap;
    const float editor_col_bottom = result.status_bar_bounds.y - outer_gap;
    const float card_w = std::max(0.0F, editor_col_right - editor_col_left);

    const bool has_terminal = m_terminal_panel.is_visible() &&
                              !result.terminal_panel_bounds.is_empty();

    if (has_terminal && m_terminal_panel.is_maximized()) {
      result.editor_header_bounds = UI::Rect{};
      result.gutter_bounds = UI::Rect{};
      result.editor_bounds = UI::Rect{};
      result.scrollbar_bounds = UI::Rect{};
      result.minimap_bounds = UI::Rect{};

      const float term_top = editor_col_top;
      const float term_height = std::max(0.0F, editor_col_bottom - term_top);
      result.terminal_panel_bounds =
          UI::Rect{editor_col_left, term_top, card_w, term_height};
      const float term_header_h = std::min(28.0F * scale, term_height);
      result.terminal_header_bounds =
          UI::Rect{editor_col_left, term_top, card_w, term_header_h};
      result.terminal_content_bounds =
          UI::Rect{editor_col_left, term_top + term_header_h, card_w,
                   std::max(0.0F, term_height - term_header_h)};
    } else {
      const float editor_card_bottom =
          has_terminal ? (result.terminal_panel_bounds.y - sep_gap * 0.5F)
                       : editor_col_bottom;

      const float header_h = result.editor_header_bounds.height;
      result.editor_header_bounds =
          UI::Rect{editor_col_left, editor_col_top, card_w, header_h};

      // Internal editor padding: 4px top between header & code, 6px bottom
      const float editor_pad_top = 4.0F * scale;
      const float editor_pad_bottom = 6.0F * scale;
      const float gutter_w = result.gutter_bounds.width;
      const float code_top = result.editor_header_bounds.bottom() + editor_pad_top;
      const float code_height =
          std::max(0.0F, editor_card_bottom - editor_pad_bottom - code_top);

      result.gutter_bounds =
          UI::Rect{editor_col_left, code_top, gutter_w, code_height};
      result.editor_bounds =
          UI::Rect{editor_col_left + gutter_w, code_top,
                   std::max(0.0F, card_w - gutter_w), code_height};

      const float scrollbar_w = 14.0F * scale;
      const float minimap_w = (result.editor_bounds.width >= 120.0F * scale)
                                  ? (112.0F * scale)
                                  : 0.0F;
      result.minimap_bounds = UI::Rect{
          std::max(result.editor_bounds.right() - scrollbar_w - minimap_w,
                   result.editor_bounds.x),
          code_top,
          std::min(minimap_w,
                   std::max(result.editor_bounds.width - scrollbar_w, 0.0F)),
          code_height,
      };
      result.scrollbar_bounds = UI::Rect{
          std::max(result.editor_bounds.right() - scrollbar_w,
                   result.editor_bounds.x),
          code_top,
          std::min(scrollbar_w, result.editor_bounds.width),
          code_height,
      };

      if (has_terminal) {
        const float term_top =
            result.terminal_panel_bounds.y + sep_gap * 0.5F;
        const float term_height = std::max(0.0F, editor_col_bottom - term_top);
        result.terminal_panel_bounds =
            UI::Rect{editor_col_left, term_top, card_w, term_height};
        const float term_header_h = std::min(28.0F * scale, term_height);
        result.terminal_header_bounds =
            UI::Rect{editor_col_left, term_top, card_w, term_header_h};
        result.terminal_content_bounds =
            UI::Rect{editor_col_left, term_top + term_header_h, card_w,
                     std::max(0.0F, term_height - term_header_h)};
      }
    }

    if (result.shader_panel_visible && !result.shader_panel_bounds.is_empty()) {
      const float shader_left = shader_split_x + sep_gap * 0.5F;
      const float shader_w =
          std::max(0.0F, result.shader_panel_bounds.right() - outer_gap - shader_left);
      const float shader_h =
          std::max(0.0F, result.shader_panel_bounds.height - outer_gap * 2.0F);
      const float shader_top = result.shader_panel_bounds.y + outer_gap;

      result.shader_panel_bounds =
          UI::Rect{shader_left, shader_top, shader_w, shader_h};
      const float s_head_h =
          std::min(28.0F * scale, result.shader_panel_header_bounds.height);
      const float s_ctrl_h =
          std::min(36.0F * scale, result.shader_panel_controls_bounds.height);
      result.shader_panel_header_bounds =
          UI::Rect{shader_left, shader_top, shader_w, s_head_h};
      result.shader_panel_viewport_bounds =
          UI::Rect{shader_left, shader_top + s_head_h, shader_w,
                   std::max(0.0F, shader_h - s_head_h - s_ctrl_h)};
      result.shader_panel_controls_bounds =
          UI::Rect{shader_left, shader_top + shader_h - s_ctrl_h, shader_w,
                   s_ctrl_h};
    }
  }

  return result;
}

void StudioWorkspaceRenderer::sync_shader_sandbox() const {
  if (const UI::Editor::TextDocumentModel *doc = m_text_editor.get_document()) {
    const std::string filename = std::string(doc->get_file_name());
    const std::filesystem::path file_path(filename);
    const std::string ext = file_path.extension().string();

    std::string full_text;
    for (const auto &line : doc->get_lines()) {
      full_text += line;
      full_text += '\n';
    }

    if (Services::Shader::ShaderService::is_shader_source_candidate(full_text,
                                                                    ext) &&
        !full_text.empty()) {
      const_cast<StudioWorkspaceRenderer *>(this)
          ->m_shader_sandbox_panel.stage_source_code(full_text);
    }
  }
}

bool StudioWorkspaceRenderer::build_and_simulate_current_shader() {
  if (const UI::Editor::TextDocumentModel *doc = m_text_editor.get_document()) {
    std::string full_text;
    for (const auto &line : doc->get_lines()) {
      full_text += line;
      full_text += '\n';
    }
    if (!full_text.empty()) {
      m_shader_sandbox_panel.set_visible(true);
      return m_shader_sandbox_panel.build_and_run(full_text);
    }
  }
  return false;
}

bool StudioWorkspaceRenderer::open_file(const std::filesystem::path &path) {
  const bool res = m_text_editor.open_file(path);
  if (res) {
    const std::string ext = path.extension().string();
    if (ext == ".glsl" || ext == ".frag" || ext == ".vert" || ext == ".comp" ||
        ext == ".shader" || ext == ".hlsl" || ext == ".wgsl" || ext == ".fs" ||
        ext == ".vs") {
      m_shader_sandbox_panel.set_visible(true);
    }
    sync_shader_sandbox();
  }
  return res;
}

bool StudioWorkspaceRenderer::open_file_at_location(
    const std::filesystem::path &path, std::size_t line, std::size_t column) {
  const bool res = m_text_editor.open_file_at_location(path, line, column);
  if (res) {
    const std::string ext = path.extension().string();
    if (ext == ".glsl" || ext == ".frag" || ext == ".vert" || ext == ".comp" ||
        ext == ".shader" || ext == ".hlsl" || ext == ".wgsl" || ext == ".fs" ||
        ext == ".vs") {
      m_shader_sandbox_panel.set_visible(true);
    }
    sync_shader_sandbox();
  }
  return res;
}

bool StudioWorkspaceRenderer::set_workspace_root(
    const std::filesystem::path &root) {
  m_text_editor.close_all_files();
  if (!m_tool_sidebar.set_workspace_root(root)) {
    return false;
  }
  m_terminal_panel.set_working_directory(root);
  Language::LanguageServerManager::instance().set_workspace_root(root);
  Settings::SettingsService::instance().set_workspace_path(root);
  return true;
}

bool StudioWorkspaceRenderer::close_project() {
  m_text_editor.close_all_files();
  m_tool_sidebar.clear_workspace();
  m_terminal_panel.shutdown();
  m_terminal_panel.set_working_directory(
      Platform::HostSystem::get_user_home_directory());
  m_shader_sandbox_panel.set_visible(false);
  Language::LanguageServerManager::instance().shutdown_all();
  Language::LanguageServerManager::instance().set_workspace_root({});
  Settings::SettingsService::instance().close_workspace();
  return true;
}

std::size_t StudioWorkspaceRenderer::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths) {
  const std::size_t count = m_text_editor.open_dropped_paths(dropped_paths);
  if (count > 0) {
    sync_shader_sandbox();
  }
  return count;
}

bool StudioWorkspaceRenderer::create_buffer() {
  const bool res = m_text_editor.create_buffer();
  if (res) {
    sync_shader_sandbox();
  }
  return res;
}

bool StudioWorkspaceRenderer::handle_pointer_press(
    HDC device_context, float point_x, float point_y, int client_width,
    int client_height, float content_top, bool extend_selection,
    std::string &command_out) {

  if (m_prompt_modal.is_visible()) {
    const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width),
                            static_cast<float>(client_height)};
    const auto prompt_layout =
        m_prompt_modal.calculate_layout(viewport, m_dpi_scale);
    return m_prompt_modal.handle_pointer_press(point_x, point_y, prompt_layout);
  }

  if (m_tool_switcher_popup.is_visible()) {
    const auto pop_res = m_tool_switcher_popup.handle_pointer_press(
        point_x, point_y, m_dpi_scale);
    if (pop_res.handled) {
      if (!pop_res.switched_tool_id.empty()) {
        auto &pm = Zenvra::Plugins::PluginManager::instance();
        pm.set_active_tool_plugin_id(pop_res.switched_tool_id);
        auto active_tool = pm.get_active_tool_plugin();
        if (active_tool) {
          UI::Editor::set_active_tool_sidebar_item(active_tool->get_id(),
                                                   active_tool->get_name());
        }
        m_tool_sidebar.get_model().set_visible(true);
        static_cast<void>(
            m_tool_sidebar.activate(UI::Editor::SidebarIcon::ToolPlugin));
      }
      if (m_window_handle)
        InvalidateRect(m_window_handle, nullptr, FALSE);
      return true;
    }
    m_tool_switcher_popup.hide();
    if (m_window_handle)
      InvalidateRect(m_window_handle, nullptr, FALSE);
  }

  if (m_text_editor.is_media_fullscreen()) {
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, 0.0F);
    return m_text_editor.handle_pointer_press(*this, device_context, layout,
                                              point_x, point_y,
                                              extend_selection, command_out);
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (const std::optional<std::size_t> sidebar_index =
          UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y)) {
    const std::span<const UI::Editor::SidebarItem> items =
        UI::Editor::get_studio_sidebar_items();
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Terminal) {
      if (!m_terminal_panel.is_visible()) {
        m_terminal_panel.set_active_channel(
            TerminalPanel::PanelChannel::Terminal);
        return m_terminal_panel.toggle();
      }
      if (m_terminal_panel.get_active_channel() !=
          TerminalPanel::PanelChannel::Terminal) {
        m_terminal_panel.set_active_channel(
            TerminalPanel::PanelChannel::Terminal);
        m_terminal_panel.set_focused(true);
        return true;
      }
      return m_terminal_panel.toggle();
    }
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Shader) {
      const bool res = m_shader_sandbox_panel.toggle();
      if (res) {
        sync_shader_sandbox();
      }
      return true;
    }
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::More) {
      const UI::Rect item_bounds =
          UI::Editor::calculate_studio_sidebar_item_bounds(layout,
                                                           *sidebar_index);
      m_tool_switcher_popup.show(item_bounds.right() + 4.0F * m_dpi_scale,
                                 item_bounds.y);
      if (m_window_handle)
        InvalidateRect(m_window_handle, nullptr, FALSE);
      return true;
    }
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::ToolPlugin) {
      m_tool_sidebar.get_model().set_visible(true);
      return m_tool_sidebar.activate(UI::Editor::SidebarIcon::ToolPlugin);
    }
    return m_tool_sidebar.activate(items[*sidebar_index].icon);
  }

  // Check 2D Corner Splitter Resizing First!
  const SplitterCornerKind corner =
      get_splitter_corner_at(layout, point_x, point_y);
  if (corner == SplitterCornerKind::SidebarTerminal) {
    m_active_corner_resizing = SplitterCornerKind::SidebarTerminal;
    m_tool_sidebar.begin_resize(point_x);
    static_cast<void>(m_terminal_panel.begin_resize());
    m_terminal_panel.set_focused(false);
    return true;
  }
  if (corner == SplitterCornerKind::ShaderTerminal) {
    m_active_corner_resizing = SplitterCornerKind::ShaderTerminal;
    m_shader_sandbox_panel.begin_resize(point_x);
    static_cast<void>(m_terminal_panel.begin_resize());
    m_terminal_panel.set_focused(false);
    return true;
  }
  if (corner == SplitterCornerKind::EditorSplitTerminal) {
    m_active_corner_resizing = SplitterCornerKind::EditorSplitTerminal;
    m_text_editor.begin_split_resize();
    static_cast<void>(m_terminal_panel.begin_resize());
    m_terminal_panel.set_focused(false);
    return true;
  }

  if (m_shader_sandbox_panel.handle_pointer_press(layout, point_x, point_y)) {
    return true;
  }
  const SidebarPressResult sidebar_res =
      m_tool_sidebar.handle_pointer_press(layout, point_x, point_y);
  if (sidebar_res.handled) {
    m_terminal_panel.set_focused(false);
    if (sidebar_res.action == SidebarActionKind::OpenFile && sidebar_res.path) {
      if (sidebar_res.path->string() == "::OPEN_FOLDER::") {
        if (const auto folder = Platform::open_folder_dialog()) {
          if (m_workspace_root_request_handler) {
            static_cast<void>(m_workspace_root_request_handler(*folder));
          } else {
            static_cast<void>(set_workspace_root(*folder));
          }
        }
        return true;
      }
      if (sidebar_res.line > 0) {
        static_cast<void>(open_file_at_location(
            *sidebar_res.path, sidebar_res.line, sidebar_res.column));
      } else {
        static_cast<void>(open_file(*sidebar_res.path));
      }
    } else if (sidebar_res.action == SidebarActionKind::NewFile &&
               sidebar_res.path) {
      const auto root = m_tool_sidebar.get_model().get_workspace_root();
      const std::string proj_name = root.filename().string();
      m_add_item_dialog.open(
          m_window_handle, *sidebar_res.path, proj_name,
          [this](const std::string &name, const std::string &initial_content) {
            std::filesystem::path created_p;
            if (m_tool_sidebar.get_model().create_file(name, created_p)) {
              if (!initial_content.empty()) {
                std::ofstream out(created_p, std::ios::binary);
                if (out.is_open()) {
                  out.write(initial_content.data(), initial_content.size());
                  out.close();
                }
              }
              static_cast<void>(open_file(created_p));
            }
          });
    } else if (sidebar_res.action == SidebarActionKind::NewFolder &&
               sidebar_res.path) {
      m_prompt_dialog.open_new_folder(
          m_window_handle, *sidebar_res.path, [this](const std::string &name) {
            std::filesystem::path created_p;
            m_tool_sidebar.get_model().create_directory(name, created_p);
            if (m_window_handle)
              InvalidateRect(m_window_handle, nullptr, FALSE);
          });
    } else if (sidebar_res.action == SidebarActionKind::SwitchTool) {
      const UI::Rect panel = layout.tool_sidebar_bounds;
      m_tool_switcher_popup.show(panel.right() + 4.0F * m_dpi_scale,
                                 panel.y + 10.0F * m_dpi_scale);
      if (m_window_handle)
        InvalidateRect(m_window_handle, nullptr, FALSE);
      return true;
    } else if (sidebar_res.action == SidebarActionKind::OpenTerminal) {
      m_terminal_panel.set_active_channel(
          TerminalPanel::PanelChannel::Terminal);
      if (!m_terminal_panel.is_visible()) {
        static_cast<void>(m_terminal_panel.toggle());
      }
      m_terminal_panel.set_focused(true);
      if (m_window_handle)
        InvalidateRect(m_window_handle, nullptr, FALSE);
      return true;
    }
    return true;
  }
  if (m_terminal_panel.is_visible() &&
      m_terminal_panel.handle_pointer_press(layout, point_x, point_y)) {
    m_tool_sidebar.set_focused(false);
    return true;
  }
  if (m_shader_sandbox_panel.is_visible() &&
      m_shader_sandbox_panel.contains(layout, point_x, point_y)) {
    m_tool_sidebar.set_focused(false);
    m_terminal_panel.set_focused(false);
    sync_shader_sandbox();
    return m_shader_sandbox_panel.handle_pointer_press(layout, point_x,
                                                       point_y);
  }
  m_terminal_panel.set_focused(false);
  m_tool_sidebar.set_focused(false);
  const bool editor_pressed = m_text_editor.handle_pointer_press(
      *this, device_context, layout, point_x, point_y, extend_selection,
      command_out);
  if (editor_pressed) {
    sync_shader_sandbox();
  }
  return editor_pressed;
}

bool StudioWorkspaceRenderer::handle_double_click(float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.handle_double_click(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_move(float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) noexcept {

  if (m_prompt_modal.is_visible()) {
    const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width),
                            static_cast<float>(client_height)};
    const auto prompt_layout =
        m_prompt_modal.calculate_layout(viewport, m_dpi_scale);
    return m_prompt_modal.handle_pointer_move(point_x, point_y, prompt_layout);
  }

  if (m_tool_switcher_popup.is_visible()) {
    if (m_tool_switcher_popup.handle_pointer_move(point_x, point_y,
                                                  m_dpi_scale)) {
      if (m_window_handle)
        InvalidateRect(m_window_handle, nullptr, FALSE);
    }
  }

  if (m_text_editor.is_media_fullscreen()) {
    const UI::Editor::StudioEditorLayoutResult layout =
        calculate_layout(client_width, client_height, 0.0F);
    return m_text_editor.handle_pointer_move(layout, point_x, point_y);
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);

  const SplitterCornerKind corner =
      get_splitter_corner_at(layout, point_x, point_y);
  if (corner == SplitterCornerKind::SidebarTerminal) {
    m_tool_sidebar.set_resize_hovered(true);
    m_terminal_panel.set_resize_hovered(true);
    static_cast<void>(
        m_text_editor.handle_pointer_move(layout, point_x, point_y));
    static_cast<void>(
        m_shader_sandbox_panel.handle_pointer_move(layout, point_x, point_y));
    m_tool_sidebar.set_resize_hovered(true);
    m_terminal_panel.set_resize_hovered(true);
    return true;
  }
  if (corner == SplitterCornerKind::ShaderTerminal) {
    m_shader_sandbox_panel.set_resize_hovered(true);
    m_terminal_panel.set_resize_hovered(true);
    static_cast<void>(
        m_tool_sidebar.handle_pointer_move(layout, point_x, point_y));
    static_cast<void>(
        m_text_editor.handle_pointer_move(layout, point_x, point_y));
    m_shader_sandbox_panel.set_resize_hovered(true);
    m_terminal_panel.set_resize_hovered(true);
    return true;
  }
  if (corner == SplitterCornerKind::EditorSplitTerminal) {
    m_text_editor.set_split_resize_hovered(true);
    m_terminal_panel.set_resize_hovered(true);
    static_cast<void>(
        m_tool_sidebar.handle_pointer_move(layout, point_x, point_y));
    static_cast<void>(
        m_shader_sandbox_panel.handle_pointer_move(layout, point_x, point_y));
    m_text_editor.set_split_resize_hovered(true);
    m_terminal_panel.set_resize_hovered(true);
    return true;
  }

  const bool sidebar_changed =
      m_tool_sidebar.handle_pointer_move(layout, point_x, point_y);
  const bool editor_changed =
      m_text_editor.handle_pointer_move(layout, point_x, point_y);
  const bool shader_changed =
      m_shader_sandbox_panel.handle_pointer_move(layout, point_x, point_y);
  return m_terminal_panel.handle_pointer_move(layout, point_x, point_y) ||
         sidebar_changed || editor_changed || shader_changed;
}

bool StudioWorkspaceRenderer::handle_pointer_drag(HDC device_context,
                                                  float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) {
  if (m_text_editor.is_media_fullscreen()) {
    const UI::Editor::StudioEditorLayoutResult fs_layout =
        calculate_layout(client_width, client_height, 0.0F);
    return m_text_editor.handle_pointer_drag(*this, device_context, fs_layout,
                                             point_x, point_y);
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);

  if (m_active_corner_resizing == SplitterCornerKind::SidebarTerminal) {
    bool changed = false;
    changed |= m_tool_sidebar.handle_pointer_drag(layout, point_x, point_y);
    changed |= m_terminal_panel.handle_pointer_drag(layout, point_y);
    return changed;
  }
  if (m_active_corner_resizing == SplitterCornerKind::ShaderTerminal) {
    bool changed = false;
    changed |=
        m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y);
    changed |= m_terminal_panel.handle_pointer_drag(layout, point_y);
    return changed;
  }
  if (m_active_corner_resizing == SplitterCornerKind::EditorSplitTerminal) {
    bool changed = false;
    changed |= m_text_editor.handle_pointer_drag(*this, device_context, layout,
                                                 point_x, point_y);
    changed |= m_terminal_panel.handle_pointer_drag(layout, point_y);
    return changed;
  }

  if (m_text_editor.is_pointer_selecting() ||
      m_text_editor.is_resizing_split() || m_text_editor.is_tab_dragging() ||
      m_text_editor.is_scrollbar_dragging() ||
      m_text_editor.is_media_dragging()) {
    return m_text_editor.handle_pointer_drag(*this, device_context, layout,
                                             point_x, point_y);
  }
  if (m_tool_sidebar.is_resizing() || m_tool_sidebar.is_dragging_item() ||
      m_tool_sidebar.is_holding_item() ||
      m_tool_sidebar.is_dragging_scrollbar()) {
    return m_tool_sidebar.handle_pointer_drag(layout, point_x, point_y);
  }
  if (m_terminal_panel.is_resizing()) {
    return m_terminal_panel.handle_pointer_drag(layout, point_y);
  }
  if (m_terminal_panel.is_dragging_tab_scrollbar() ||
      m_terminal_panel.is_selecting_text()) {
    return m_terminal_panel.handle_pointer_drag(layout, point_x, point_y);
  }
  if (m_shader_sandbox_panel.is_resizing()) {
    return m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y);
  }
  if (m_tool_sidebar.is_visible() &&
      m_tool_sidebar.contains(layout, point_x, point_y)) {
    if (m_tool_sidebar.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  if (m_terminal_panel.is_visible() &&
      m_terminal_panel.contains(layout, point_x, point_y)) {
    if (m_terminal_panel.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  if (m_shader_sandbox_panel.is_visible() &&
      m_shader_sandbox_panel.contains(layout, point_x, point_y)) {
    if (m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  return m_text_editor.handle_pointer_drag(*this, device_context, layout,
                                           point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_release() noexcept {

  const bool was_corner_resizing =
      (m_active_corner_resizing != SplitterCornerKind::None);
  m_active_corner_resizing = SplitterCornerKind::None;
  const bool terminal_changed = m_terminal_panel.handle_pointer_release();
  const bool sidebar_changed = m_tool_sidebar.handle_pointer_release();
  const bool editor_changed = m_text_editor.handle_pointer_release();
  const bool shader_changed = m_shader_sandbox_panel.handle_pointer_release();
  return was_corner_resizing || terminal_changed || sidebar_changed ||
         editor_changed || shader_changed;
}

bool StudioWorkspaceRenderer::handle_scroll(const Event::ScrollEvent &event,
                                            int client_width, int client_height,
                                            float content_top) noexcept {

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.handle_scroll(*this, layout, event);
}

bool StudioWorkspaceRenderer::handle_editor_input(
    UI::Editor::EditorInputCommand command, bool extend_selection) {
  const bool res = m_text_editor.handle_input(command, extend_selection);
  if (res) {
    sync_shader_sandbox();
  }
  return res;
}

bool StudioWorkspaceRenderer::handle_editor_action(
    UI::Editor::EditorAction action) {
  const bool res = m_text_editor.handle_action(action);
  if (res) {
    sync_shader_sandbox();
  }
  return res;
}

void StudioWorkspaceRenderer::reset_layout() noexcept {
  m_tool_sidebar.get_model().set_visible(true);
  static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Project));
  if (m_terminal_panel.is_visible()) {
    static_cast<void>(m_terminal_panel.toggle());
  }
  m_shader_sandbox_panel.set_visible(false);
  m_text_editor.reset_split();
}

std::optional<bool>
StudioWorkspaceRenderer::handle_editor_command(std::string_view command_id) {
  if (command_id == Commands::CommandIds::window_reset_layout) {
    reset_layout();
    return true;
  }
  if (command_id == Commands::CommandIds::build_build_project ||
      command_id == Commands::CommandIds::run_start) {
    if (const UI::Editor::TextDocumentModel *doc =
            m_text_editor.get_document()) {
      const std::string filename = std::string(doc->get_file_name());
      const std::filesystem::path file_path(filename);
      const std::string ext = file_path.extension().string();

      std::string full_text;
      for (const auto &line : doc->get_lines()) {
        full_text += line;
        full_text += '\n';
      }

      if (Services::Shader::ShaderService::is_shader_source_candidate(full_text,
                                                                      ext) &&
          !full_text.empty()) {
        m_shader_sandbox_panel.set_source_code(full_text);
        m_shader_sandbox_panel.get_service().compile_and_render();
        m_shader_sandbox_panel.get_service().play();
        m_shader_sandbox_panel.set_visible(true);
        return true;
      }
    }
  }
  if (command_id == Commands::CommandIds::view_toggle_right_dock ||
      command_id == "zde.view.shaderPanel" ||
      command_id == "zde.view.shader_sandbox") {
    const bool res = m_shader_sandbox_panel.toggle();
    if (res) {
      sync_shader_sandbox();
    }
    return res;
  }
  if (command_id == Commands::CommandIds::view_terminal_panel) {
    if (!m_terminal_panel.is_visible()) {
      m_terminal_panel.set_active_channel(
          TerminalPanel::PanelChannel::Terminal);
      return m_terminal_panel.toggle();
    }
    if (m_terminal_panel.get_active_channel() !=
        TerminalPanel::PanelChannel::Terminal) {
      m_terminal_panel.set_active_channel(
          TerminalPanel::PanelChannel::Terminal);
      m_terminal_panel.set_focused(true);
      return true;
    }
    return m_terminal_panel.toggle();
  }
  if (command_id == Commands::CommandIds::view_output) {
    if (!m_terminal_panel.is_visible()) {
      m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Output);
      return m_terminal_panel.toggle();
    }
    if (m_terminal_panel.get_active_channel() !=
        TerminalPanel::PanelChannel::Output) {
      m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Output);
      m_terminal_panel.set_focused(true);
      return true;
    }
    return m_terminal_panel.toggle();
  }
  if (command_id == Commands::CommandIds::view_toggle_bottom_dock ||
      command_id == Commands::CommandIds::view_problems ||
      command_id == Commands::CommandIds::view_diagnostics) {
    return toggle_terminal();
  }
  if (command_id == Commands::CommandIds::view_toggle_left_dock) {
    const bool vis = !m_tool_sidebar.is_visible();
    m_tool_sidebar.get_model().set_visible(vis);
    return true;
  }
  if (command_id == Commands::CommandIds::view_explorer ||
      command_id == Commands::CommandIds::view_project_panel ||
      command_id == Commands::CommandIds::view_outline_panel) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(
        m_tool_sidebar.activate(UI::Editor::SidebarIcon::Project));
    return true;
  }
  if (command_id == Commands::CommandIds::view_search) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Search));
    return true;
  }
  if (command_id == Commands::CommandIds::view_git_panel) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(
        m_tool_sidebar.activate(UI::Editor::SidebarIcon::VersionControl));
    return true;
  }
  if (command_id == Commands::CommandIds::view_debugger_panel) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Run));
    return true;
  }
  if (command_id == Commands::CommandIds::open_plugins) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(
        m_tool_sidebar.activate(UI::Editor::SidebarIcon::Services));
    return true;
  }
  if (command_id == Commands::CommandIds::open_settings) {
    m_settings_window.set_theme(
        UI::Theme::ThemeManager::instance().get_current_theme());
    m_settings_window.toggle(m_window_handle);
    return true;
  }
  if (command_id == Commands::CommandIds::open_themes ||
      command_id == Commands::CommandIds::more_tools) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::More));
    return true;
  }
  const auto res = m_text_editor.handle_command(command_id);
  if (res.has_value() && *res) {
    sync_shader_sandbox();
  }
  return res;
}

std::optional<bool> StudioWorkspaceRenderer::is_editor_command_enabled(
    std::string_view command_id) const noexcept {
  if (command_id == Commands::CommandIds::window_reset_layout ||
      command_id == Commands::CommandIds::view_toggle_right_dock ||
      command_id == "zde.view.shaderPanel" ||
      command_id == "zde.view.shader_sandbox" ||
      command_id == Commands::CommandIds::view_toggle_bottom_dock ||
      command_id == Commands::CommandIds::view_terminal_panel ||
      command_id == Commands::CommandIds::view_output ||
      command_id == Commands::CommandIds::view_problems ||
      command_id == Commands::CommandIds::view_diagnostics ||
      command_id == Commands::CommandIds::view_toggle_left_dock ||
      command_id == Commands::CommandIds::view_explorer ||
      command_id == Commands::CommandIds::view_project_panel ||
      command_id == Commands::CommandIds::view_outline_panel ||
      command_id == Commands::CommandIds::view_search ||
      command_id == Commands::CommandIds::view_git_panel ||
      command_id == Commands::CommandIds::view_debugger_panel ||
      command_id == Commands::CommandIds::open_plugins ||
      command_id == Commands::CommandIds::open_settings ||
      command_id == Commands::CommandIds::open_themes ||
      command_id == Commands::CommandIds::more_tools ||
      command_id == Commands::CommandIds::edit_profiles) {
    return true;
  }
  return m_text_editor.is_command_enabled(command_id);
}

bool StudioWorkspaceRenderer::handle_text_input(std::string_view utf8_text) {
  const bool res = m_terminal_panel.is_focused()
                       ? m_terminal_panel.handle_text_input(utf8_text)
                       : m_text_editor.handle_text_input(utf8_text);
  if (res && !m_terminal_panel.is_focused()) {
    sync_shader_sandbox();
  }
  return res;
}

bool StudioWorkspaceRenderer::trigger_editor_autocomplete() {
  return m_text_editor.trigger_completion();
}

bool StudioWorkspaceRenderer::handle_terminal_key(
    Terminal::TerminalInputKey key) {
  return m_terminal_panel.handle_key(key);
}

bool StudioWorkspaceRenderer::handle_terminal_control(char letter) {
  return m_terminal_panel.handle_control(letter);
}

bool StudioWorkspaceRenderer::handle_terminal_scroll(
    const Event::ScrollEvent &event) noexcept {
  return m_terminal_panel.handle_scroll(event);
}

bool StudioWorkspaceRenderer::handle_tool_sidebar_scroll(
    std::ptrdiff_t line_delta, int client_width, int client_height,
    float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.handle_scroll(layout, line_delta);
}

bool StudioWorkspaceRenderer::is_search_focused() const noexcept {
  return m_tool_sidebar.is_search_focused();
}

bool StudioWorkspaceRenderer::is_sidebar_focused() const noexcept {
  return m_tool_sidebar.is_focused();
}

bool StudioWorkspaceRenderer::is_explorer_focused() const noexcept {
  return m_tool_sidebar.is_explorer_focused();
}

bool StudioWorkspaceRenderer::handle_search_char(char32_t codepoint) {
  return m_tool_sidebar.handle_char(codepoint);
}

bool StudioWorkspaceRenderer::handle_search_key(int vkey, bool ctrl, bool shift,
                                                bool alt) {
  return m_tool_sidebar.handle_key(vkey, ctrl, shift, alt);
}

bool StudioWorkspaceRenderer::handle_sidebar_key(int vkey, bool ctrl,
                                                 bool shift, bool alt) {
  return m_tool_sidebar.handle_key(vkey, ctrl, shift, alt);
}

bool StudioWorkspaceRenderer::is_editor_focused() const noexcept {
  return !m_terminal_panel.is_focused() && !m_tool_sidebar.is_focused() &&
         m_text_editor.is_focused();
}

bool StudioWorkspaceRenderer::is_terminal_focused() const noexcept {
  return m_terminal_panel.is_focused();
}

bool StudioWorkspaceRenderer::is_activity_bar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (!layout.activity_bar_bounds.contains(point_x, point_y)) {
    return false;
  }
  return UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y).has_value();
}

bool StudioWorkspaceRenderer::is_activity_bar_area(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return layout.activity_bar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tab_bar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (m_ui_font == nullptr) {
    return false;
  }
  HDC device_context = GetDC(nullptr);
  if (device_context == nullptr) {
    return false;
  }
  const bool interactive = m_text_editor.is_tab_interactive_point(
      *this, device_context, layout, point_x, point_y);
  ReleaseDC(nullptr, device_context);
  return interactive;
}

bool StudioWorkspaceRenderer::is_editor_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  const float scale = layout.dpi_scale;
  const float editor_top = layout.editor_header_bounds.is_empty()
                               ? layout.gutter_bounds.y
                               : layout.editor_header_bounds.y;
  const float editor_bottom = layout.editor_bounds.bottom() + 6.0F * scale;
  const float left = layout.editor_header_bounds.is_empty()
                         ? layout.gutter_bounds.x
                         : layout.editor_header_bounds.x;
  const float width = layout.editor_header_bounds.is_empty()
                          ? (layout.gutter_bounds.width + layout.editor_bounds.width)
                          : layout.editor_header_bounds.width;
  const UI::Rect full_editor_rect{left, editor_top, width,
                                  std::max(0.0F, editor_bottom - editor_top)};

  return full_editor_rect.contains(point_x, point_y) &&
         !m_text_editor.is_minimap_point(layout, point_x, point_y) &&
         !m_text_editor.is_scrollbar_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_media_point(float point_x, float point_y,
                                             int client_width,
                                             int client_height,
                                             float content_top) const noexcept {
  const float effective_top =
      m_text_editor.is_media_fullscreen() ? 0.0F : content_top;
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, effective_top);
  return m_text_editor.is_media_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_media_interactive_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const float effective_top =
      m_text_editor.is_media_fullscreen() ? 0.0F : content_top;
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, effective_top);
  return m_text_editor.is_media_interactive_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_interactive_point(
    float point_x, float point_y) const noexcept {
  return m_text_editor.is_empty_state_interactive_point(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_empty_state_button_hovered() const noexcept {
  return m_text_editor.is_empty_state_button_hovered() ||
         m_tool_sidebar.is_empty_state_button_hovered();
}

bool StudioWorkspaceRenderer::is_shader_sandbox_interactive_point(
    float point_x, float point_y) const noexcept {
  return m_shader_sandbox_panel.is_visible() &&
         m_shader_sandbox_panel.is_interactive_point(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_scrollbar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_scrollbar_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_fold_margin_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_fold_margin_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_minimap_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_minimap_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_visible() &&
         layout.terminal_content_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_panel_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_visible() &&
         layout.terminal_panel_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tool_sidebar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tool_sidebar_interactive_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.is_interactive_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tool_sidebar_text_input_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.is_text_input_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_interactive_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_interactive_point(layout, point_x, point_y);
}

StudioWorkspaceRenderer::SplitterCornerKind
StudioWorkspaceRenderer::get_splitter_corner_at(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  const float scale = layout.dpi_scale;
  const float radius = std::max(12.0F * scale, 10.0F);

  // 1. Sidebar + Terminal corner (T-junction / 3-way intersection)
  if (m_tool_sidebar.is_visible() && m_terminal_panel.is_visible() &&
      !layout.tool_sidebar_bounds.is_empty() &&
      !layout.terminal_panel_bounds.is_empty()) {
    const bool on_right =
        layout.tool_sidebar_bounds.x > layout.editor_bounds.x;
    const float cx = on_right
        ? (layout.terminal_panel_bounds.right() + layout.tool_sidebar_bounds.x) * 0.5F
        : (layout.tool_sidebar_bounds.right() + layout.terminal_panel_bounds.x) * 0.5F;
    const float cy = layout.terminal_panel_bounds.y;
    if (point_x >= (cx - radius) && point_x <= (cx + radius) &&
        point_y >= (cy - radius) && point_y <= (cy + radius)) {
      return SplitterCornerKind::SidebarTerminal;
    }
  }

  // 2. Shader Sandbox + Terminal corner
  if (m_shader_sandbox_panel.is_visible() && m_terminal_panel.is_visible() &&
      !layout.shader_panel_bounds.is_empty() &&
      !layout.terminal_panel_bounds.is_empty()) {
    const float cx = layout.shader_panel_bounds.x;
    const float cy = layout.terminal_panel_bounds.y;
    if (point_x >= (cx - radius) && point_x <= (cx + radius) &&
        point_y >= (cy - radius) && point_y <= (cy + radius)) {
      return SplitterCornerKind::ShaderTerminal;
    }
  }

  // 3. Editor Split + Terminal corner
  if (m_text_editor.is_split_active() && m_terminal_panel.is_visible() &&
      !layout.editor_bounds.is_empty() &&
      !layout.terminal_panel_bounds.is_empty()) {
    const float splitter_x =
        layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) *
                                     m_text_editor.get_split_ratio();
    const float cy = layout.terminal_panel_bounds.y;
    if (point_x >= (splitter_x - radius) && point_x <= (splitter_x + radius) &&
        point_y >= (cy - radius) && point_y <= (cy + radius)) {
      return SplitterCornerKind::EditorSplitTerminal;
    }
  }

  return SplitterCornerKind::None;
}

bool StudioWorkspaceRenderer::is_corner_resize_handle_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return get_splitter_corner_at(layout, point_x, point_y) !=
         SplitterCornerKind::None;
}

bool StudioWorkspaceRenderer::is_terminal_resize_handle_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_resizing() const noexcept {
  return m_terminal_panel.is_resizing();
}

bool StudioWorkspaceRenderer::is_sidebar_resize_handle_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.is_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_sidebar_resizing() const noexcept {
  return m_tool_sidebar.is_resizing();
}

bool StudioWorkspaceRenderer::is_sidebar_dragging_item() const noexcept {
  return m_tool_sidebar.is_dragging_item();
}

bool StudioWorkspaceRenderer::is_sidebar_dragging_scrollbar() const noexcept {
  return m_tool_sidebar.is_dragging_scrollbar();
}

bool StudioWorkspaceRenderer::is_shader_sandbox_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_shader_sandbox_panel.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_shader_sandbox_resize_handle(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_shader_sandbox_panel.is_resize_handle_point(layout, point_x,
                                                       point_y);
}

bool StudioWorkspaceRenderer::is_shader_sandbox_resizing() const noexcept {
  return m_shader_sandbox_panel.is_resizing();
}

bool StudioWorkspaceRenderer::is_editor_split_resize_handle(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_split_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_split_resizing() const noexcept {
  return m_text_editor.is_split_resizing();
}

bool StudioWorkspaceRenderer::toggle_shader_sandbox() noexcept {
  const bool res = m_shader_sandbox_panel.toggle();
  if (res) {
    sync_shader_sandbox();
  }
  return res;
}

bool StudioWorkspaceRenderer::is_shader_sandbox_visible() const noexcept {
  return m_shader_sandbox_panel.is_visible();
}

bool StudioWorkspaceRenderer::toggle_terminal() noexcept {
  return m_terminal_panel.toggle();
}

bool StudioWorkspaceRenderer::is_terminal_visible() const noexcept {
  return m_terminal_panel.is_visible();
}

bool StudioWorkspaceRenderer::tick_animations() noexcept {
  const bool caret_changed = m_text_editor.tick_animations();
  const bool sidebar_changed = m_tool_sidebar.tick_animations();
  const bool terminal_changed = m_terminal_panel.poll();
  const bool terminal_anim_changed = m_terminal_panel.tick_animations();
  const bool shader_changed = m_shader_sandbox_panel.tick_animations();
  return caret_changed || sidebar_changed || terminal_changed ||
         terminal_anim_changed || shader_changed;
}

void StudioWorkspaceRenderer::shutdown() {
  m_terminal_panel.shutdown();
  m_svg_cache.clear();
  m_icon_asset_root.clear();
  m_minimap_font.reset();
  m_editor_font.reset();
  m_small_font.reset();
  m_ui_font.reset();
}

StudioWorkspaceRenderer::ModernCardGeometry
StudioWorkspaceRenderer::get_modern_card_geometry(
    int client_width, int client_height, float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  const float scale = layout.dpi_scale;
  const float card_radius = std::max(std::round(8.0F * scale), 8.0F);

  ModernCardGeometry geom;
  geom.card_radius = card_radius;

  // In modern mode, the sidebar sits seamlessly on the window canvas background
  geom.sidebar_card = UI::Rect{0.0F, 0.0F, 0.0F, 0.0F};

  if (!layout.editor_bounds.is_empty() || !layout.editor_header_bounds.is_empty()) {
    const float left = layout.editor_header_bounds.is_empty()
                           ? layout.gutter_bounds.x
                           : layout.editor_header_bounds.x;
    const float top = layout.editor_header_bounds.is_empty()
                          ? (layout.gutter_bounds.y - 4.0F * scale)
                          : layout.editor_header_bounds.y;
    const float right = layout.editor_header_bounds.is_empty()
                            ? layout.editor_bounds.right()
                            : layout.editor_header_bounds.right();
    const float bottom = layout.editor_bounds.bottom() + 6.0F * scale;
    geom.editor_card = UI::Rect{left, top, std::max(0.0F, right - left),
                                std::max(0.0F, bottom - top)};
  }

  if (m_terminal_panel.is_visible() && !layout.terminal_panel_bounds.is_empty()) {
    geom.terminal_card = layout.terminal_panel_bounds;
  }

  if (m_shader_sandbox_panel.is_visible() &&
      !layout.shader_panel_bounds.is_empty()) {
    geom.shader_card = layout.shader_panel_bounds;
  }

  return geom;
}

namespace {

void set_card_alpha_channel(uint32_t *pixels, int img_w, int img_h,
                            const UI::Rect &card, float radius) noexcept {
  if (card.is_empty() || !pixels || img_w <= 0 || img_h <= 0)
    return;
  const int x0 = std::clamp(static_cast<int>(std::round(card.x)), 0, img_w);
  const int y0 = std::clamp(static_cast<int>(std::round(card.y)), 0, img_h);
  const int x1 =
      std::clamp(static_cast<int>(std::round(card.right())), 0, img_w);
  const int y1 =
      std::clamp(static_cast<int>(std::round(card.bottom())), 0, img_h);
  const float r = std::min({radius, card.width * 0.5F, card.height * 0.5F});
  const float r2 = r * r;

  for (int y = y0; y < y1; ++y) {
    uint32_t *row = &pixels[y * img_w];
    const float cy = static_cast<float>(y) + 0.5F;
    const bool in_top_corner = (r > 0.0F && cy < card.y + r);
    const bool in_bottom_corner = (r > 0.0F && cy > card.bottom() - r);

    for (int x = x0; x < x1; ++x) {
      const float cx = static_cast<float>(x) + 0.5F;
      const bool in_left_corner = (r > 0.0F && cx < card.x + r);
      const bool in_right_corner = (r > 0.0F && cx > card.right() - r);

      if ((in_top_corner || in_bottom_corner) &&
          (in_left_corner || in_right_corner)) {
        const float corner_cx =
            in_left_corner ? (card.x + r) : (card.right() - r);
        const float corner_cy =
            in_top_corner ? (card.y + r) : (card.bottom() - r);
        const float dx = cx - corner_cx;
        const float dy = cy - corner_cy;
        if (dx * dx + dy * dy > r2) {
          continue; // outside rounded corner -> keep blurred backdrop
        }
      }
      row[x] |= 0xFF000000; // 100% solid opaque
    }
  }
}

} // namespace

void StudioWorkspaceRenderer::apply_solid_card_alpha(
    uint32_t *pixels, int client_width, int client_height,
    float content_top) const noexcept {
  if (!pixels || client_width <= 0 || client_height <= 0)
    return;

  const bool is_modern_style =
      m_palette.is_modern || m_theme.is_modern;

  if (!is_modern_style) {
    const int top_y = std::clamp(static_cast<int>(std::round(content_top)), 0,
                                 client_height);
    for (int y = top_y; y < client_height; ++y) {
      uint32_t *row = &pixels[y * client_width];
      for (int x = 0; x < client_width; ++x) {
        row[x] |= 0xFF000000;
      }
    }
    return;
  }

  const auto geom =
      get_modern_card_geometry(client_width, client_height, content_top);
  if (!geom.sidebar_card.is_empty()) {
    set_card_alpha_channel(pixels, client_width, client_height, geom.sidebar_card,
                           geom.card_radius);
  }
  // In modern blurred style, editor card floats transparently on the frosted glass blur in dark mode.
  // In light modern mode, editor card is an elevated solid card floating over the frosted glass acrylic blur
  // so dark code text and syntax highlighting retain 100% crisp contrast without additive washout.
  if (!m_theme.is_dark) {
    if (!geom.editor_card.is_empty()) {
      set_card_alpha_channel(pixels, client_width, client_height, geom.editor_card,
                             geom.card_radius);
    }
    const auto active_tab = m_text_editor.get_active_tab_bounds();
    if (active_tab.has_value() && !active_tab->is_empty()) {
      const UI::Editor::StudioEditorLayoutResult layout =
          calculate_layout(client_width, client_height, content_top);
      UI::Rect tab_pill = *active_tab;
      const float v_pad = std::round(4.0F * m_dpi_scale);
      const float h_pad = std::round(2.0F * m_dpi_scale);
      tab_pill.x += h_pad;
      tab_pill.y += v_pad;
      tab_pill.width = std::max(0.0F, tab_pill.width - h_pad * 2.0F);
      tab_pill.height = std::max(0.0F, tab_pill.height - v_pad * 2.0F);

      if (tab_pill.x < layout.tab_bar_bounds.x) {
        tab_pill.width =
            std::max(0.0F, tab_pill.right() - layout.tab_bar_bounds.x);
        tab_pill.x = layout.tab_bar_bounds.x;
      }
      if (tab_pill.right() > layout.tab_bar_bounds.right()) {
        tab_pill.width =
            std::max(0.0F, layout.tab_bar_bounds.right() - tab_pill.x);
      }
      if (!tab_pill.is_empty()) {
        set_card_alpha_channel(pixels, client_width, client_height, tab_pill,
                               std::max(std::round(6.0F * m_dpi_scale), 5.0F));
      }
    }
  }
  set_card_alpha_channel(pixels, client_width, client_height, geom.terminal_card,
                         geom.card_radius);
  set_card_alpha_channel(pixels, client_width, client_height, geom.shader_card,
                         geom.card_radius);

  if (m_tool_switcher_popup.is_visible()) {
    set_card_alpha_channel(pixels, client_width, client_height,
                           m_tool_switcher_popup.calculate_bounds(m_dpi_scale),
                           8.0F);
  }
}

void StudioWorkspaceRenderer::render(HDC device_context, int client_width,
                                     int client_height,
                                     float content_top) const {
  if (device_context == nullptr || m_ui_font == nullptr ||
      m_small_font == nullptr || m_editor_font == nullptr ||
      m_minimap_font == nullptr) {
    return;
  }

  auto &pm = Zenvra::Plugins::PluginManager::instance();
  auto active_tool = pm.get_active_tool_plugin();
  if (active_tool) {
    UI::Editor::set_active_tool_sidebar_item(active_tool->get_id(),
                                             active_tool->get_name());
  } else {
    UI::Editor::clear_active_tool_sidebar_item();
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (m_text_editor.is_media_fullscreen()) {
    fill_rectangle(device_context, layout.workspace_bounds,
                   UI::Theme::Color{0, 0, 0, 255});
    SetBkMode(device_context, TRANSPARENT);
    m_text_editor.render_overlays(*this, device_context, layout);
    return;
  }
  const bool is_modern_style =
      m_palette.is_modern || m_theme.is_modern;

  if (!is_modern_style) {
    fill_rectangle(device_context, layout.workspace_bounds,
                   m_palette.workspace_background);
    fill_rectangle(device_context, layout.activity_bar_bounds,
                   m_palette.sidebar_background);

    const bool tabs_are_in_titlebar =
        layout.tab_bar_bounds.bottom() <= layout.activity_bar_bounds.y;
    if (tabs_are_in_titlebar) {
      m_text_editor.draw_tab_strip(*this, device_context, layout);
    } else {
      fill_rectangle(device_context, layout.tab_bar_bounds,
                     m_palette.tab_background);
    }

    fill_rectangle(device_context, layout.tool_sidebar_bounds,
                   m_palette.sidebar_background);
    fill_rectangle(device_context, layout.editor_header_bounds,
                   m_palette.editor_background);
    fill_rectangle(device_context, layout.gutter_bounds,
                   m_palette.editor_background);
    fill_rectangle(device_context, layout.editor_bounds,
                   m_palette.editor_background);
    if (!layout.shader_splitter_bounds.is_empty()) {
      fill_rectangle(device_context, layout.shader_splitter_bounds,
                     m_palette.editor_background);
    }
    fill_rectangle(device_context, layout.status_bar_bounds,
                   m_palette.status_background);
    SetBkMode(device_context, TRANSPARENT);

    m_text_editor.render(*this, device_context, layout);
    m_terminal_panel.render(*this, device_context, layout);
    m_tool_sidebar.render(*this, device_context, layout);
    m_activity_sidebar.render(*this, device_context, layout);
    m_shader_sandbox_panel.render(*this, device_context, layout);
  } else {
    // ----------------- Modern Style (Seamless JetBrains-Style Minimalist UI)
    // -----------------
    const auto geom =
        get_modern_card_geometry(client_width, client_height, content_top);
    const float card_radius = geom.card_radius;

    // In modern mode, the unified window background (frosted glass blur when OS
    // blur is enabled, or window_background) seamlessly hosts titlebar,
    // sidebar, footer, and floating cards without dividing borders.
    const bool tabs_are_in_titlebar =
        layout.tab_bar_bounds.bottom() <= layout.activity_bar_bounds.y;
    if (tabs_are_in_titlebar) {
      m_text_editor.draw_tab_strip(*this, device_context, layout);
    } else if (!is_modern_style) {
      fill_rectangle(device_context, layout.tab_bar_bounds,
                     m_palette.tab_background);
    }
    SetBkMode(device_context, TRANSPARENT);

    // 2. Activity Sidebar (vertical strip)
    m_activity_sidebar.render(*this, device_context, layout);

    // 3. Tool Sidebar (Explorer) Card Wrapper
    if (!geom.sidebar_card.is_empty()) {
      // Solid wrapper background with rounded corners
      fill_rounded_rectangle(device_context, geom.sidebar_card,
                             m_palette.sidebar_background, card_radius);

      // Clip content inside rounded wrapper
      const int saved = SaveDC(device_context);
      HRGN rgn = CreateRoundRectRgn(
          round_to_int(geom.sidebar_card.x), round_to_int(geom.sidebar_card.y),
          round_to_int(geom.sidebar_card.right()) + 1,
          round_to_int(geom.sidebar_card.bottom()) + 1,
          round_to_int(card_radius * 2.0F), round_to_int(card_radius * 2.0F));
      SelectClipRgn(device_context, rgn);
      DeleteObject(rgn);

      m_tool_sidebar.render(*this, device_context, layout);

      RestoreDC(device_context, saved);

      // Subtle 1px rounded outline
      draw_rounded_rectangle(device_context, geom.sidebar_card, m_palette.border,
                             card_radius);
    } else {
      m_tool_sidebar.render(*this, device_context, layout);
    }

    // 4. Text Editor Card Wrapper
    if (!geom.editor_card.is_empty()) {
      fill_rounded_rectangle(device_context, geom.editor_card,
                             m_palette.editor_background, card_radius);

      const int saved = SaveDC(device_context);
      HRGN rgn = CreateRoundRectRgn(
          round_to_int(geom.editor_card.x), round_to_int(geom.editor_card.y),
          round_to_int(geom.editor_card.right()) + 1,
          round_to_int(geom.editor_card.bottom()) + 1,
          round_to_int(card_radius * 2.0F), round_to_int(card_radius * 2.0F));
      SelectClipRgn(device_context, rgn);
      DeleteObject(rgn);

      m_text_editor.render(*this, device_context, layout);

      RestoreDC(device_context, saved);

      draw_rounded_rectangle(device_context, geom.editor_card, m_palette.border,
                             card_radius);
    }

    // Terminal Panel Card (when visible)
    if (!geom.terminal_card.is_empty()) {
      fill_rounded_rectangle(device_context, geom.terminal_card,
                             m_palette.editor_background, card_radius);

      const int saved = SaveDC(device_context);
      HRGN rgn = CreateRoundRectRgn(
          round_to_int(geom.terminal_card.x), round_to_int(geom.terminal_card.y),
          round_to_int(geom.terminal_card.right()) + 1,
          round_to_int(geom.terminal_card.bottom()) + 1,
          round_to_int(card_radius * 2.0F), round_to_int(card_radius * 2.0F));
      SelectClipRgn(device_context, rgn);
      DeleteObject(rgn);

      m_terminal_panel.render(*this, device_context, layout);

      RestoreDC(device_context, saved);

      draw_rounded_rectangle(device_context, geom.terminal_card, m_palette.border,
                             card_radius);
    }

    // 5. Shader Sandbox Panel / Emulator Card Wrapper
    if (!geom.shader_card.is_empty()) {
      // Solid wrapper background with rounded corners for Emulator / Shader Sandbox
      fill_rounded_rectangle(device_context, geom.shader_card,
                             m_palette.sidebar_background, card_radius);

      const int saved = SaveDC(device_context);
      HRGN rgn = CreateRoundRectRgn(
          round_to_int(geom.shader_card.x), round_to_int(geom.shader_card.y),
          round_to_int(geom.shader_card.right()) + 1,
          round_to_int(geom.shader_card.bottom()) + 1,
          round_to_int(card_radius * 2.0F), round_to_int(card_radius * 2.0F));
      SelectClipRgn(device_context, rgn);
      DeleteObject(rgn);

      m_shader_sandbox_panel.render(*this, device_context, layout);

      RestoreDC(device_context, saved);

      draw_rounded_rectangle(device_context, geom.shader_card, m_palette.border,
                             card_radius);
    }
  }
  if (const UI::Editor::TextDocumentModel *document =
          m_text_editor.get_document()) {
    auto status = document->get_status();
    const bool show_indicator = Settings::SettingsService::instance().get<bool>(
        "vim.showModeIndicator");
    if (m_text_editor.get_input_router().is_vim_active() && show_indicator) {
      status.vim_mode = m_text_editor.get_input_router().get_mode_name();
    }
    m_footer_toolbar.render(*this, device_context, layout,
                            document->get_full_breadcrumbs(), status);
  } else {
    m_footer_toolbar.render(*this, device_context, layout, {},
                            UI::Editor::FooterEditorStatus{});
  }

  // Floating overlays (e.g. Action Dropdown Menu, Diagnostics, Tool Switcher)
  // rendered on top of everything
  m_text_editor.render_overlays(*this, device_context, layout);
  if (m_tool_switcher_popup.is_visible()) {
    m_tool_switcher_popup.render(*this, device_context, m_dpi_scale);
  }
}

void StudioWorkspaceRenderer::fill_rectangle(
    HDC device_context, const UI::Rect &rectangle,
    const UI::Theme::Color &color) const {
  if (rectangle.is_empty()) {
    return;
  }
  if (color.alpha < 255) {
    fill_rounded_rectangle(device_context, rectangle, color, 0.0f);
    return;
  }
  RECT native_rectangle = to_native_rect(rectangle);
  SetDCBrushColor(device_context, to_color_ref(color));
  FillRect(device_context, &native_rectangle,
           static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void StudioWorkspaceRenderer::fill_rounded_rectangle(
    HDC device_context, const UI::Rect &rectangle,
    const UI::Theme::Color &color, float radius) const {
  if (rectangle.is_empty()) {
    return;
  }
  int w = round_to_int(rectangle.width);
  int h = round_to_int(rectangle.height);
  if (w <= 0 || h <= 0) {
    return;
  }

  float r = std::min({radius, rectangle.width * 0.5f, rectangle.height * 0.5f});
  if (r <= 0.0f && color.alpha >= 255) {
    RECT bounds = to_native_rect(rectangle);
    SetDCBrushColor(device_context, to_color_ref(color));
    FillRect(device_context, &bounds,
             static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    return;
  }

  if (color.alpha >= 255 && r > 0.0f) {
    HRGN rgn = CreateRoundRectRgn(
        round_to_int(rectangle.x), round_to_int(rectangle.y),
        round_to_int(rectangle.right()) + 1,
        round_to_int(rectangle.bottom()) + 1,
        round_to_int(r * 2.0F), round_to_int(r * 2.0F));
    if (rgn) {
      HBRUSH brush = CreateSolidBrush(to_color_ref(color));
      FillRgn(device_context, rgn, brush);
      DeleteObject(brush);
      DeleteObject(rgn);
      return;
    }
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC memDC = CreateCompatibleDC(device_context);
  void *bits = nullptr;
  HBITMAP hBmp =
      CreateDIBSection(device_context, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (hBmp) {
    auto *pixels = static_cast<uint32_t *>(bits);
    const uint32_t col_r = color.red;
    const uint32_t col_g = color.green;
    const uint32_t col_b = color.blue;
    const uint32_t col_a = color.alpha;
    const int ri = static_cast<int>(r);
    const float w_minus_r = static_cast<float>(w) - r;
    const float h_minus_r = static_cast<float>(h) - r;

    std::memset(pixels, 0, static_cast<size_t>(w) * h * sizeof(uint32_t));
    const uint32_t base_pr = (col_r * col_a) / 255;
    const uint32_t base_pg = (col_g * col_a) / 255;
    const uint32_t base_pb = (col_b * col_a) / 255;
    const uint32_t opaque_pixel =
        (col_a << 24) | (base_pr << 16) | (base_pg << 8) | base_pb;

    for (int y = 0; y < h; ++y) {
      const float cy = static_cast<float>(y) + 0.5f;
      const bool in_corner_row = (y < ri) || (y >= h - ri);
      uint32_t *row = &pixels[(h - 1 - y) * w];

      if (!in_corner_row) {
        std::fill(row, row + w, opaque_pixel);
        continue;
      }

      const float dy = std::max({r - cy, 0.0f, cy - h_minus_r});
      for (int x = 0; x < w; ++x) {
        const float cx = static_cast<float>(x) + 0.5f;
        const bool in_corner_col = (x < ri) || (x >= w - ri);
        if (!in_corner_col) {
          row[x] = opaque_pixel;
          continue;
        }

        const float dx = std::max({r - cx, 0.0f, cx - w_minus_r});
        const float dist = std::sqrt(dx * dx + dy * dy);
        float alpha_f = std::clamp(0.5f - (dist - r), 0.0f, 1.0f);

        if (alpha_f > 0.0f) {
          uint32_t a =
              static_cast<uint32_t>(alpha_f * static_cast<float>(col_a));
          uint32_t pr = (col_r * a) / 255;
          uint32_t pg = (col_g * a) / 255;
          uint32_t pb = (col_b * a) / 255;
          row[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
        }
      }
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    AlphaBlend(device_context, round_to_int(rectangle.x),
               round_to_int(rectangle.y), w, h, memDC, 0, 0, w, h, bf);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
  }
  DeleteDC(memDC);
}

void StudioWorkspaceRenderer::draw_rectangle(
    HDC device_context, const UI::Rect &rectangle,
    const UI::Theme::Color &color) const {
  if (rectangle.is_empty()) {
    return;
  }
  const RECT bounds = to_native_rect(rectangle);
  HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(color));
  HGDIOBJ previous_pen = SelectObject(device_context, pen);
  HGDIOBJ previous_brush =
      SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
  Rectangle(device_context, bounds.left, bounds.top, bounds.right,
            bounds.bottom);
  SelectObject(device_context, previous_brush);
  SelectObject(device_context, previous_pen);
  DeleteObject(pen);
}

void StudioWorkspaceRenderer::draw_rounded_rectangle(
    HDC device_context, const UI::Rect &rectangle,
    const UI::Theme::Color &color, float radius) const {
  if (rectangle.is_empty()) {
    return;
  }
  const RECT bounds = to_native_rect(rectangle);
  HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(color));
  HGDIOBJ previous_pen = SelectObject(device_context, pen);
  HGDIOBJ previous_brush =
      SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
  const int d = round_to_int(radius * 2.0F);
  RoundRect(device_context, bounds.left, bounds.top, bounds.right,
            bounds.bottom, d, d);
  SelectObject(device_context, previous_brush);
  SelectObject(device_context, previous_pen);
  DeleteObject(pen);
}

void StudioWorkspaceRenderer::draw_smooth_shadow(
    HDC device_context, const UI::Rect &bounds, float radius, float scale,
    bool is_dark) const {
  if (device_context == nullptr || bounds.width <= 0.0F ||
      bounds.height <= 0.0F) {
    return;
  }

  const int blur_r = std::max(round_to_int(5.0F * scale), 4);
  const int dy = std::max(round_to_int(3.0F * scale), 2);
  const int pad = blur_r * 3;

  const int w = std::max(round_to_int(bounds.width), 1);
  const int h = std::max(round_to_int(bounds.height), 1);
  const int total_w = w + pad * 2;
  const int total_h = h + pad * 2;

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = total_w;
  bmi.bmiHeader.biHeight = -total_h; // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  uint32_t *dib_pixels = nullptr;
  HDC mem_dc = CreateCompatibleDC(device_context);
  if (!mem_dc) {
    return;
  }
  HBITMAP hbmp = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS,
                                  reinterpret_cast<void **>(&dib_pixels),
                                  nullptr, 0);
  if (!hbmp || !dib_pixels) {
    DeleteDC(mem_dc);
    return;
  }

  std::vector<uint8_t> grid_a(static_cast<std::size_t>(total_w * total_h), 0);
  std::vector<uint8_t> grid_b(static_cast<std::size_t>(total_w * total_h), 0);
  const uint8_t peak_alpha = is_dark ? 90 : 42;
  const float r = std::min({radius, static_cast<float>(w) * 0.5F, static_cast<float>(h) * 0.5F});
  const float r2 = r * r;

  for (int y = 0; y < h; ++y) {
    const float cy = static_cast<float>(y) + 0.5F;
    const bool in_top = (r > 0.0F && cy < r);
    const bool in_bottom = (r > 0.0F && cy > static_cast<float>(h) - r);
    const int row_offset = (y + pad) * total_w + pad;

    for (int x = 0; x < w; ++x) {
      const float cx = static_cast<float>(x) + 0.5F;
      const bool in_left = (r > 0.0F && cx < r);
      const bool in_right = (r > 0.0F && cx > static_cast<float>(w) - r);

      if ((in_top || in_bottom) && (in_left || in_right)) {
        const float corner_cx = in_left ? r : (static_cast<float>(w) - r);
        const float corner_cy = in_top ? r : (static_cast<float>(h) - r);
        const float dx_val = cx - corner_cx;
        const float dy_val = cy - corner_cy;
        if (dx_val * dx_val + dy_val * dy_val > r2) {
          continue;
        }
      }
      grid_a[row_offset + x] = peak_alpha;
    }
  }

  auto box_blur_h = [&](const std::vector<uint8_t> &src, std::vector<uint8_t> &dst, int r_val) {
    const int ksize = 2 * r_val + 1;
    for (int y = 0; y < total_h; ++y) {
      const int row = y * total_w;
      int sum = 0;
      for (int k = -r_val; k <= r_val; ++k) {
        sum += (k >= 0 && k < total_w) ? src[row + k] : 0;
      }
      for (int x = 0; x < total_w; ++x) {
        dst[row + x] = static_cast<uint8_t>(sum / ksize);
        const int rem_x = x - r_val;
        const int add_x = x + r_val + 1;
        sum -= (rem_x >= 0 && rem_x < total_w) ? src[row + rem_x] : 0;
        sum += (add_x >= 0 && add_x < total_w) ? src[row + add_x] : 0;
      }
    }
  };

  auto box_blur_v = [&](const std::vector<uint8_t> &src, std::vector<uint8_t> &dst, int r_val) {
    const int ksize = 2 * r_val + 1;
    for (int x = 0; x < total_w; ++x) {
      int sum = 0;
      for (int k = -r_val; k <= r_val; ++k) {
        sum += (k >= 0 && k < total_h) ? src[k * total_w + x] : 0;
      }
      for (int y = 0; y < total_h; ++y) {
        dst[y * total_w + x] = static_cast<uint8_t>(sum / ksize);
        const int rem_y = y - r_val;
        const int add_y = y + r_val + 1;
        sum -= (rem_y >= 0 && rem_y < total_h) ? src[rem_y * total_w + x] : 0;
        sum += (add_y >= 0 && add_y < total_h) ? src[add_y * total_w + x] : 0;
      }
    }
  };

  // Two separable box-blur passes approximate Gaussian with smooth quadratic B-spline falloff
  box_blur_h(grid_a, grid_b, blur_r);
  box_blur_v(grid_b, grid_a, blur_r);
  box_blur_h(grid_a, grid_b, blur_r);
  box_blur_v(grid_b, grid_a, blur_r);

  const std::size_t pixel_count = static_cast<std::size_t>(total_w * total_h);
  for (std::size_t i = 0; i < pixel_count; ++i) {
    const uint32_t a = grid_a[i];
    dib_pixels[i] = (a << 24U);
  }

  HGDIOBJ old_bmp = SelectObject(mem_dc, hbmp);
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.BlendFlags = 0;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;

  const int dest_x = round_to_int(bounds.x) - pad;
  const int dest_y = round_to_int(bounds.y) - pad + dy;

  int src_x = 0;
  int src_y = 0;
  int draw_w = total_w;
  int draw_h = total_h;
  int out_x = dest_x;
  int out_y = dest_y;

  if (out_x < 0) {
    src_x -= out_x;
    draw_w += out_x;
    out_x = 0;
  }
  if (out_y < 0) {
    src_y -= out_y;
    draw_h += out_y;
    out_y = 0;
  }
  if (draw_w > 0 && draw_h > 0) {
    AlphaBlend(device_context, out_x, out_y, draw_w, draw_h,
               mem_dc, src_x, src_y, draw_w, draw_h, blend);
  }

  SelectObject(mem_dc, old_bmp);
  DeleteObject(hbmp);
  DeleteDC(mem_dc);
}

void StudioWorkspaceRenderer::draw_line(HDC device_context, int from_x,
                                        int from_y, int to_x, int to_y,
                                        const UI::Theme::Color &color) const {
  HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(color));
  HGDIOBJ previous_pen = SelectObject(device_context, pen);
  MoveToEx(device_context, from_x, from_y, nullptr);
  LineTo(device_context, to_x, to_y);
  SelectObject(device_context, previous_pen);
  DeleteObject(pen);
}

void StudioWorkspaceRenderer::draw_text(HDC device_context,
                                        AntialiasedFont &font,
                                        std::string_view text, float point_x,
                                        float center_y,
                                        const UI::Theme::Color &color) const {
  if (text.empty()) {
    return;
  }
  const int baseline =
      round_to_int(center_y -
                   static_cast<float>(font.getAscent(device_context) +
                                      font.getDescent(device_context)) *
                       0.5F +
                   static_cast<float>(font.getAscent(device_context)));
  font.drawString(device_context, to_font_color(color), round_to_int(point_x),
                  baseline, std::string{text});
}

void StudioWorkspaceRenderer::draw_scaled_text(
    HDC device_context, AntialiasedFont &font, std::string_view text,
    float point_x, float center_y, float scale,
    const UI::Theme::Color &color) const {
  if (text.empty() || scale <= 0.0f) {
    return;
  }

  const int baseline =
      round_to_int(center_y -
                   static_cast<float>(font.getAscent(device_context) +
                                      font.getDescent(device_context)) *
                       0.5F +
                   static_cast<float>(font.getAscent(device_context)));

  // Calculate center of the text for scaling
  const float text_width =
      static_cast<float>(font.getTextWidth(device_context, std::string{text}));
  const float text_center_x = point_x + text_width * 0.5f;
  const float text_center_y = center_y;

  // Enable advanced graphics mode for world transform
  int old_graphics_mode = SetGraphicsMode(device_context, GM_ADVANCED);

  // Get the previous transform to restore later
  XFORM old_xform;
  GetWorldTransform(device_context, &old_xform);

  // Setup scaling transform around the center point
  XFORM xform;
  xform.eM11 = scale;
  xform.eM12 = 0.0f;
  xform.eM21 = 0.0f;
  xform.eM22 = scale;
  xform.eDx = text_center_x * (1.0f - scale);
  xform.eDy = text_center_y * (1.0f - scale);

  // Apply the scaling transform relative to any existing transform
  ModifyWorldTransform(device_context, &xform, MWT_RIGHTMULTIPLY);

  // Draw the text (it will be scaled by the transform)
  font.drawString(device_context, to_font_color(color), round_to_int(point_x),
                  baseline, std::string{text});

  // Restore previous transform and graphics mode
  SetWorldTransform(device_context, &old_xform);
  SetGraphicsMode(device_context, old_graphics_mode);
}

void StudioWorkspaceRenderer::draw_svg_icon(
    HDC device_context, std::string_view asset_name, int center_x, int center_y,
    int size, const UI::Theme::Color &color, const UI::Theme::Color &background,
    bool preserve_source_colors) const {
  (void)background;
  if (device_context == nullptr || size <= 0 || asset_name.empty()) {
    return;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{asset_name};
  if (resolved_path.is_relative() && !m_icon_asset_root.empty()) {
    std::string rel_str = resolved_path.string();
    if (rel_str.starts_with("Assets/icons/") ||
        rel_str.starts_with("Assets\\icons\\")) {
      rel_str = rel_str.substr(13);
    } else if (rel_str.starts_with("Resources/icons/") ||
               rel_str.starts_with("Resources\\icons\\")) {
      rel_str = rel_str.substr(16);
    } else if (rel_str.starts_with("Assets/") ||
               rel_str.starts_with("Assets\\")) {
      rel_str = rel_str.substr(7);
    } else if (rel_str.starts_with("Resources/") ||
               rel_str.starts_with("Resources\\")) {
      rel_str = rel_str.substr(10);
    } else if (rel_str.starts_with("vscode-codicons/icons/")) {
      // Submodule layout: codicon SVGs live under src/icons/
      rel_str = "vscode-codicons/src/icons/" + rel_str.substr(22);
    }

    const std::filesystem::path filename = resolved_path.filename();
    const std::filesystem::path direct_path = m_icon_asset_root / rel_str;
    const std::filesystem::path symbol_file_1 =
        m_icon_asset_root / "vscode-symbols" / "icons" / "files" / filename;
    const std::filesystem::path symbol_folder_1 =
        m_icon_asset_root / "vscode-symbols" / "icons" / "folders" / filename;
    const std::filesystem::path symbol_file_src = m_icon_asset_root /
                                                  "vscode-symbols" / "src" /
                                                  "icons" / "files" / filename;
    const std::filesystem::path symbol_folder_src =
        m_icon_asset_root / "vscode-symbols" / "src" / "icons" / "folders" /
        filename;
    const std::filesystem::path symbol_file_2 =
        m_icon_asset_root / "vscode-symbols" / "files" / filename;
    const std::filesystem::path symbol_folder_2 =
        m_icon_asset_root / "vscode-symbols" / "folders" / filename;
    const std::filesystem::path codicon_direct =
        m_icon_asset_root / "vscode-codicons" / "icons" / rel_str;
    const std::filesystem::path codicon_file =
        m_icon_asset_root / "vscode-codicons" / "icons" / filename;
    const std::filesystem::path codicon_src =
        m_icon_asset_root / "vscode-codicons" / "src" / "icons" / filename;
    const std::filesystem::path vsicon_file =
        m_icon_asset_root / "vscode-icons" / "icons" / filename;
    const std::filesystem::path material_file =
        m_icon_asset_root / "material-icon-theme" / filename;

    if (std::filesystem::is_regular_file(direct_path, path_error)) {
      resolved_path = direct_path;
    } else if (std::filesystem::is_regular_file(symbol_file_1, path_error)) {
      resolved_path = symbol_file_1;
    } else if (std::filesystem::is_regular_file(symbol_folder_1, path_error)) {
      resolved_path = symbol_folder_1;
    } else if (std::filesystem::is_regular_file(symbol_file_src, path_error)) {
      resolved_path = symbol_file_src;
    } else if (std::filesystem::is_regular_file(symbol_folder_src,
                                                path_error)) {
      resolved_path = symbol_folder_src;
    } else if (std::filesystem::is_regular_file(symbol_file_2, path_error)) {
      resolved_path = symbol_file_2;
    } else if (std::filesystem::is_regular_file(symbol_folder_2, path_error)) {
      resolved_path = symbol_folder_2;
    } else if (std::filesystem::is_regular_file(codicon_direct, path_error)) {
      resolved_path = codicon_direct;
    } else if (std::filesystem::is_regular_file(codicon_file, path_error)) {
      resolved_path = codicon_file;
    } else if (std::filesystem::is_regular_file(codicon_src, path_error)) {
      resolved_path = codicon_src;
    } else if (std::filesystem::is_regular_file(vsicon_file, path_error)) {
      resolved_path = vsicon_file;
    } else if (std::filesystem::is_regular_file(material_file, path_error)) {
      resolved_path = material_file;
    } else {
      const std::filesystem::path themed_path =
          m_icon_asset_root / resolved_path;
      if (std::filesystem::is_regular_file(themed_path, path_error)) {
        resolved_path = themed_path;
      } else {
        const std::filesystem::path legacy_path = m_icon_asset_root / filename;
        if (std::filesystem::is_regular_file(legacy_path, path_error)) {
          resolved_path = legacy_path;
        }
      }
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    return;
  }
  preserve_source_colors =
      preserve_source_colors &&
      (resolved_path.parent_path().filename() == "material-icon-theme" ||
       resolved_path.string().find("vscode-symbols") != std::string::npos ||
       resolved_path.string().find("vscode-icons") != std::string::npos);

  const std::string resolved_string = resolved_path.string();
  const std::string cache_key = resolved_string + "@" + std::to_string(size) +
                                "#" + to_font_color(color) +
                                (preserve_source_colors ? "_p" : "");
  auto cached = m_svg_cache.find(cache_key);
  if (cached == m_svg_cache.end()) {
    if (m_svg_cache.size() >= 128) {
      m_svg_cache.clear();
    }
    auto document = lunasvg::Document::loadFromFile(resolved_string);
    if (!document) {
      return;
    }
    auto bitmap = document->renderToBitmap(static_cast<std::uint32_t>(size),
                                           static_cast<std::uint32_t>(size));
    if (bitmap.isNull()) {
      return;
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(size) *
                                      static_cast<std::size_t>(size));
    const auto *source = reinterpret_cast<const std::uint32_t *>(bitmap.data());
    for (std::size_t index = 0; index < pixels.size(); ++index) {
      const std::uint32_t alpha = (source[index] >> 24U) & 0xFFU;
      if (alpha == 0) {
        pixels[index] = 0;
        continue;
      }
      const std::uint32_t source_red = (source[index] >> 16U) & 0xFFU;
      const std::uint32_t source_green = (source[index] >> 8U) & 0xFFU;
      const std::uint32_t source_blue = source[index] & 0xFFU;

      const std::uint32_t red =
          preserve_source_colors
              ? source_red
              : (static_cast<std::uint32_t>(color.red) * alpha) / 255U;
      const std::uint32_t green =
          preserve_source_colors
              ? source_green
              : (static_cast<std::uint32_t>(color.green) * alpha) / 255U;
      const std::uint32_t blue =
          preserve_source_colors
              ? source_blue
              : (static_cast<std::uint32_t>(color.blue) * alpha) / 255U;

      pixels[index] = (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
    }
    cached = m_svg_cache.emplace(cache_key, std::move(pixels)).first;
  }

  HDC mem_dc = CreateCompatibleDC(device_context);
  if (!mem_dc) {
    return;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = size;
  bitmap_info.bmiHeader.biHeight = -size; // top-down
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void *dib_bits = nullptr;
  HBITMAP hbmp = CreateDIBSection(mem_dc, &bitmap_info, DIB_RGB_COLORS,
                                  &dib_bits, nullptr, 0);
  if (hbmp && dib_bits) {
    std::memcpy(dib_bits, cached->second.data(),
                static_cast<std::size_t>(size) *
                    static_cast<std::size_t>(size) * sizeof(std::uint32_t));
    HGDIOBJ old_bmp = SelectObject(mem_dc, hbmp);

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = color.alpha;
    blend.AlphaFormat = AC_SRC_ALPHA;

    const int half = size / 2;
    AlphaBlend(device_context, center_x - half, center_y - half, size, size,
               mem_dc, 0, 0, size, size, blend);

    SelectObject(mem_dc, old_bmp);
    DeleteObject(hbmp);
  }
  DeleteDC(mem_dc);
}

void StudioWorkspaceRenderer::draw_png_icon(
    HDC device_context, const std::string &asset_path, int center_x,
    int center_y, int max_size, const UI::Theme::Color &background) const {
  if (device_context == nullptr || max_size <= 0 || asset_path.empty()) {
    return;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{asset_path};
  if (resolved_path.is_relative() && !m_icon_asset_root.empty()) {
    const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
    if (std::filesystem::is_regular_file(themed_path, path_error)) {
      resolved_path = themed_path;
    } else {
      const std::filesystem::path legacy_path =
          m_icon_asset_root / resolved_path.filename();
      if (std::filesystem::is_regular_file(legacy_path, path_error)) {
        resolved_path = legacy_path;
      } else {
        const std::filesystem::path root_path =
            m_icon_asset_root.parent_path().parent_path() / resolved_path;
        if (std::filesystem::is_regular_file(root_path, path_error)) {
          resolved_path = root_path;
        }
      }
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    auto search_dir = std::filesystem::current_path(path_error);
    for (int i = 0; i < 6 && !search_dir.empty(); ++i) {
      const auto candidate = search_dir / "Assets" / "icons" /
                             std::filesystem::path(asset_path).filename();
      if (std::filesystem::is_regular_file(candidate, path_error)) {
        resolved_path = candidate;
        break;
      }
      const auto candidate_direct = search_dir / asset_path;
      if (std::filesystem::is_regular_file(candidate_direct, path_error)) {
        resolved_path = candidate_direct;
        break;
      }
      if (search_dir == search_dir.parent_path())
        break;
      search_dir = search_dir.parent_path();
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    return;
  }

  const std::string resolved_string = resolved_path.string();
  const std::string cache_key = resolved_string + "@png#" +
                                std::to_string(max_size) + "/" +
                                to_font_color(background);

  auto it = m_svg_cache.find(cache_key);
  if (it == m_svg_cache.end()) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data =
        stbi_load(resolved_string.c_str(), &width, &height, &channels, 4);
    if (!data) {
      return;
    }

    int draw_w = width;
    int draw_h = height;
    if (width > max_size || height > max_size) {
      float aspect = static_cast<float>(width) / static_cast<float>(height);
      if (width > height) {
        draw_w = max_size;
        draw_h = std::max(1, static_cast<int>(max_size / aspect));
      } else {
        draw_h = max_size;
        draw_w = std::max(1, static_cast<int>(max_size * aspect));
      }
    }

    // High quality area-averaging supersampling to eliminate pixelation and
    // jagged edges
    auto resampled = Utility::AntialiasedImage::resample_area_average(
        data, width, height, draw_w, draw_h);

    stbi_image_free(data);

    const float bg_lum =
        (0.2126F * background.red + 0.7152F * background.green +
         0.0722F * background.blue) /
        255.0F;
    const bool is_light_theme = (bg_lum > 0.5F);
    if (is_light_theme &&
        (asset_path.find("zenvra_logo") != std::string::npos ||
         asset_path.find("mascot") != std::string::npos ||
         asset_path.find("logo") != std::string::npos)) {
      for (std::size_t i = 0; i + 3 < resampled.size(); i += 4) {
        if (resampled[i + 3] > 10) {
          resampled[i] = static_cast<std::uint8_t>(255 - resampled[i]);
          resampled[i + 1] = static_cast<std::uint8_t>(255 - resampled[i + 1]);
          resampled[i + 2] = static_cast<std::uint8_t>(255 - resampled[i + 2]);
        }
      }
    }

    Utility::ColorRGBA bg_color{background.red, background.green,
                                background.blue, 255};
    auto bmp_data = Utility::AntialiasedImage::composite_to_dib(
        resampled, draw_w, draw_h, max_size, bg_color);

    auto emplaced = m_svg_cache.emplace(cache_key, std::move(bmp_data));
    it = emplaced.first;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = max_size;
  bitmap_info.bmiHeader.biHeight = max_size;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  const int half = max_size / 2;
  SetDIBitsToDevice(device_context, center_x - half, center_y - half,
                    static_cast<DWORD>(max_size), static_cast<DWORD>(max_size),
                    0, 0, 0, static_cast<UINT>(max_size), it->second.data(),
                    &bitmap_info, DIB_RGB_COLORS);
}

int StudioWorkspaceRenderer::get_text_width(HDC device_context,
                                            AntialiasedFont &font,
                                            std::string_view text) const {
  return font.getTextWidth(device_context, std::string{text});
}

std::optional<std::filesystem::path>
StudioWorkspaceRenderer::handle_right_click(float point_x, float point_y,
                                            int client_width, int client_height,
                                            float content_top) {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.handle_right_click(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_prompt_modal_visible() const noexcept {
  return m_prompt_modal.is_visible();
}

void StudioWorkspaceRenderer::render_prompt_modal(HDC device_context,
                                                  int client_width,
                                                  int client_height) const {
  if (!m_prompt_modal.is_visible()) {
    return;
  }

  const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width),
                          static_cast<float>(client_height)};
  const auto layout = m_prompt_modal.calculate_layout(viewport, m_dpi_scale);

  // 1. Subtle semi-transparent backdrop overlay (No solid pitch-black)
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 40;
  blend.AlphaFormat = 0;

  HDC mem_dc = CreateCompatibleDC(device_context);
  HBITMAP mem_bm = CreateCompatibleBitmap(device_context, 1, 1);
  HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);
  SetPixel(mem_dc, 0, 0, RGB(0, 0, 0));
  AlphaBlend(device_context,
             static_cast<int>(layout.base_layout.backdrop_bounds.x),
             static_cast<int>(layout.base_layout.backdrop_bounds.y),
             static_cast<int>(layout.base_layout.backdrop_bounds.width),
             static_cast<int>(layout.base_layout.backdrop_bounds.height),
             mem_dc, 0, 0, 1, 1, blend);
  SelectObject(mem_dc, old_bm);
  DeleteObject(mem_bm);
  DeleteDC(mem_dc);

  // 2. Dialog Container (sleek minimalist card with smooth elevation shadow)
  const UI::Theme::Color dialog_bg{28, 29, 34, 255};
  const UI::Theme::Color border_col{58, 60, 68, 255};

  draw_smooth_shadow(device_context, layout.base_layout.dialog_bounds,
                     10.0F * m_dpi_scale, m_dpi_scale, m_palette.is_dark);

  fill_rounded_rectangle(device_context, layout.base_layout.dialog_bounds,
                         dialog_bg, 10.0F * m_dpi_scale);
  draw_rectangle(device_context, layout.base_layout.dialog_bounds, border_col);

  // 3. Title & Subtitle (macOS centered bold typography)
  const std::string title_str = m_prompt_modal.get_title();
  const int title_tw = get_text_width(device_context, *m_ui_font, title_str);
  const float title_x =
      layout.base_layout.dialog_bounds.x +
      (layout.base_layout.dialog_bounds.width - static_cast<float>(title_tw)) *
          0.5F;
  draw_text(device_context, *m_ui_font, title_str, title_x,
            layout.title_bounds.y + layout.title_bounds.height * 0.5F,
            UI::Theme::Color{235, 238, 242, 255});

  if (m_prompt_modal.get_mode() == UI::Components::PromptMode::ConfirmDelete ||
      m_prompt_modal.get_mode() == UI::Components::PromptMode::InstallLspServer) {
    const std::string sub_str = m_prompt_modal.get_subtitle();
    const int sub_tw = get_text_width(device_context, *m_small_font, sub_str);
    const float sub_x =
        layout.base_layout.dialog_bounds.x +
        (layout.base_layout.dialog_bounds.width - static_cast<float>(sub_tw)) *
            0.5F;
    draw_text(device_context, *m_small_font, sub_str, sub_x,
              layout.subtitle_bounds.y + layout.subtitle_bounds.height * 0.5F,
              UI::Theme::Color{145, 150, 160, 255});
  }

  // 4. Close (X) button
  const auto close_bg = m_prompt_modal.is_close_hovered()
                            ? UI::Theme::Color{255, 255, 255, 25}
                            : dialog_bg;
  if (m_prompt_modal.is_close_hovered()) {
    fill_rounded_rectangle(device_context, layout.close_button_bounds, close_bg,
                           4.0F * m_dpi_scale);
  }
  const int prompt_cx = round_to_int(layout.close_button_bounds.x +
                                     layout.close_button_bounds.width * 0.5F);
  const int prompt_cy = round_to_int(layout.close_button_bounds.y +
                                     layout.close_button_bounds.height * 0.5F);
  const int prompt_icon_sz = std::max(round_to_int(12.0F * m_dpi_scale), 10);
  draw_svg_icon(device_context, "diagnostic-error.svg", prompt_cx, prompt_cy,
                prompt_icon_sz,
                m_prompt_modal.is_close_hovered()
                    ? UI::Theme::Color{255, 255, 255, 255}
                    : UI::Theme::Color{180, 185, 195, 255},
                close_bg);

  // 5. Pure Transparent Borderless Input Field (macOS Cocoa standard)
  if (m_prompt_modal.get_mode() != UI::Components::PromptMode::ConfirmDelete &&
      m_prompt_modal.get_mode() != UI::Components::PromptMode::InstallLspServer) {
    const auto &input = m_prompt_modal.get_input();
    const std::string &text = input.get_text();
    const float text_x = layout.input_bounds.x + 4.0F * m_dpi_scale;
    const float text_y =
        layout.input_bounds.y + layout.input_bounds.height * 0.5F;

    if (text.empty()) {
      draw_text(device_context, *m_ui_font, input.get_placeholder(), text_x,
                text_y, UI::Theme::Color{110, 115, 130, 255});
    } else {
      draw_text(device_context, *m_editor_font, text, text_x, text_y,
                UI::Theme::Color{240, 242, 245, 255});
    }

    // Draw Caret
    const int text_w = get_text_width(device_context, *m_editor_font, text);
    const float caret_x = text_x + static_cast<float>(text_w);
    draw_line(device_context, round_to_int(caret_x),
              round_to_int(layout.input_bounds.y + 6.0F * m_dpi_scale),
              round_to_int(caret_x),
              round_to_int(layout.input_bounds.bottom() - 6.0F * m_dpi_scale),
              UI::Theme::Color{255, 255, 255, 230});
  }

  // 6. Action Buttons (Only rendered when defined, e.g. for ConfirmDelete or InstallLspServer)
  if (!layout.cancel_button_bounds.is_empty()) {
    const auto cancel_bg = m_prompt_modal.is_cancel_hovered()
                               ? UI::Theme::Color{58, 61, 68, 255}
                               : UI::Theme::Color{45, 47, 52, 255};
    fill_rounded_rectangle(device_context, layout.cancel_button_bounds,
                           cancel_bg, 6.0F * m_dpi_scale);
    const std::string cancel_label = m_prompt_modal.get_cancel_label();
    const int cancel_tw = get_text_width(device_context, *m_ui_font, cancel_label);
    const float cancel_tx =
        layout.cancel_button_bounds.x +
        (layout.cancel_button_bounds.width - static_cast<float>(cancel_tw)) *
            0.5F;
    draw_text(device_context, *m_ui_font, cancel_label, cancel_tx,
              layout.cancel_button_bounds.y +
                  layout.cancel_button_bounds.height * 0.5F,
              UI::Theme::Color{204, 204, 204, 255});
  }

  if (!layout.ok_button_bounds.is_empty()) {
    const UI::Theme::Color ok_bg =
        (m_prompt_modal.get_mode() == UI::Components::PromptMode::ConfirmDelete)
            ? (m_prompt_modal.is_ok_hovered()
                   ? UI::Theme::Color{235, 65, 70, 255}
                   : UI::Theme::Color{218, 45, 50, 255})
            : (m_prompt_modal.is_ok_hovered()
                   ? UI::Theme::Color{66, 148, 246, 255}
                   : UI::Theme::Color{53, 132, 228, 255});
    fill_rounded_rectangle(device_context, layout.ok_button_bounds, ok_bg,
                           6.0F * m_dpi_scale);
    const std::string confirm_label = m_prompt_modal.get_confirm_label();
    const int ok_tw = get_text_width(device_context, *m_ui_font, confirm_label);
    const float ok_tx =
        layout.ok_button_bounds.x +
        (layout.ok_button_bounds.width - static_cast<float>(ok_tw)) * 0.5F;
    draw_text(device_context, *m_ui_font, confirm_label, ok_tx,
              layout.ok_button_bounds.y + layout.ok_button_bounds.height * 0.5F,
              UI::Theme::Color{255, 255, 255, 255});
  }
}

void StudioWorkspaceRenderer::render_add_item_dialog(
    HDC /*device_context*/, int /*client_width*/, int /*client_height*/,
    const UI::Theme::StudioTheme & /*theme*/) const {
  // AddNewItemDialog renders as a standalone native Win32 window (HWND)
}

bool StudioWorkspaceRenderer::is_settings_window_visible() const noexcept {
  return m_settings_window.is_visible();
}

void StudioWorkspaceRenderer::render_settings_window(
    HDC /*device_context*/, int /*client_width*/, int /*client_height*/,
    const UI::Theme::StudioTheme &theme) const {
  const_cast<UI::Settings::SettingsWindow &>(m_settings_window)
      .set_theme(theme);
}

std::string StudioWorkspaceRenderer::resolve_font_family_name(
    const std::string &font_spec) noexcept {
  ensure_bundled_fonts_loaded();
  if (!g_fonts_enumerated) {
    refresh_available_fonts();
  }

  std::vector<std::string> candidates;
  if (!font_spec.empty()) {
    std::stringstream ss(font_spec);
    std::string item;
    while (std::getline(ss, item, ',')) {
      const size_t start = item.find_first_not_of(" \t\r\n'\"");
      const size_t end = item.find_last_not_of(" \t\r\n'\"");
      if (start == std::string::npos)
        continue;
      std::string name = item.substr(start, end - start + 1);
      if (!name.empty()) {
        candidates.push_back(std::move(name));
      }
    }
  }

  // 1. Check each candidate in order (CSS cascade)
  for (const auto &name : candidates) {
    if (name == "monospace" || name == "sans-serif" || name == "serif" ||
        name == "cursive" || name == "fantasy" || name == "system-ui") {
      continue;
    }

    // 1a. Exact case-insensitive match in all available fonts (OS + bundled)
    for (const auto &fam : g_all_available_fonts) {
      if (_stricmp(fam.c_str(), name.c_str()) == 0) {
        return fam;
      }
    }

    // 1b. Normalized match (e.g. "JetBrainsMono" -> "JetBrains Mono")
    const std::string norm = normalize_font_name(name);
    for (const auto &fam : g_all_available_fonts) {
      if (normalize_font_name(fam) == norm) {
        return fam;
      }
    }

    // 1c. Alias resolution for bundled / OS JetBrains Mono / Nerd Fonts
    if (norm.find("jetbrainsmono") != std::string::npos ||
        norm.find("jetbrains") != std::string::npos) {
      static const char *jb_variants[] = {
          "JetBrainsMono Nerd Font",
          "JetBrains Mono",
          "JetBrainsMonoNL Nerd Font",
          "JetBrainsMono Nerd Font Mono",
          "JetBrainsMonoNL Nerd Font Mono",
          "JetBrainsMono NFM",
          "JetBrainsMono NF",
          "JetBrainsMono"};
      for (const char *v : jb_variants) {
        for (const auto &fam : g_all_available_fonts) {
          if (_stricmp(fam.c_str(), v) == 0) {
            return fam;
          }
        }
      }
      for (const auto &fam : g_all_available_fonts) {
        if (normalize_font_name(fam).find("jetbrainsmono") != std::string::npos ||
            normalize_font_name(fam).find("jetbrains") != std::string::npos) {
          return fam;
        }
      }
    }

    // 1d. General prefix match for other programming fonts (e.g. "Fira Code", "Cascadia Code")
    if (norm.find("firacode") != std::string::npos || norm.find("fira") != std::string::npos) {
      for (const auto &fam : g_all_available_fonts) {
        if (normalize_font_name(fam).find("firacode") != std::string::npos) {
          return fam;
        }
      }
    }
    if (norm.find("cascadiacode") != std::string::npos || norm.find("cascadia") != std::string::npos) {
      for (const auto &fam : g_all_available_fonts) {
        if (normalize_font_name(fam).find("cascadia") != std::string::npos) {
          return fam;
        }
      }
    }

    // 1e. Direct GDI verification for installed system font
    if (is_font_available(name)) {
      return name;
    }
  }

  // 2. If no candidate strictly matched the catalog, return the first
  // non-generic candidate typed by the user directly so GDI's font mapper
  // can attempt to load it.
  for (const auto &name : candidates) {
    if (name != "monospace" && name != "sans-serif" && name != "serif") {
      return name;
    }
  }

  // 3. Fallback cascade: prioritize bundled JetBrains Mono, then Cascadia Code, Consolas
  for (const char *fb : {"JetBrainsMono Nerd Font", "JetBrains Mono", "Cascadia Code", "Consolas"}) {
    if (is_font_available(fb)) {
      return fb;
    }
  }

  return "Consolas";
}

float StudioWorkspaceRenderer::get_editor_line_height(
    HDC device_context) const noexcept {
  auto &settings = Settings::SettingsService::instance();
  const int custom_lh = settings.get<int>("editor.lineHeight");
  if (custom_lh >= 10 && custom_lh <= 60) {
    return static_cast<float>(custom_lh) * m_dpi_scale;
  }
  if (m_editor_font) {
    return std::max(
        static_cast<float>(m_editor_font->getHeight(device_context)) +
            4.0F * m_dpi_scale,
        16.0F * m_dpi_scale);
  }
  const int font_sz = settings.get<int>("editor.fontSize");
  const int eff_sz = (font_sz >= 8 && font_sz <= 72) ? font_sz : 14;
  return std::max(static_cast<float>(eff_sz) * 1.4F * m_dpi_scale,
                  16.0F * m_dpi_scale);
}

void StudioWorkspaceRenderer::reload_editor_font() {
  ensure_bundled_fonts_loaded();
  auto &settings = Settings::SettingsService::instance();
  const int configured_size = settings.get<int>("editor.fontSize");
  const int effective_size =
      (configured_size >= 8 && configured_size <= 72) ? configured_size : 14;

  const std::string configured_font =
      settings.get<std::string>("editor.fontFamily");
  if (!configured_font.empty()) {
    m_editor_font_name = resolve_font_family_name(configured_font);
  }

  m_editor_font = std::make_unique<AntialiasedFont>(
      m_editor_font_name,
      std::max(round_to_int(static_cast<float>(effective_size) * m_dpi_scale),
               10));
  const bool ligatures_enabled =
      settings.has("editor.fontLigatures")
          ? settings.get<bool>("editor.fontLigatures")
          : true;
  m_editor_font->setLigaturesEnabled(ligatures_enabled);
  m_minimap_font = std::make_unique<AntialiasedFont>(
      m_editor_font_name, std::max(round_to_int(3.0F * m_dpi_scale), 3));

  if (m_window_handle) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

void StudioWorkspaceRenderer::reload_terminal_font() {
  ensure_bundled_fonts_loaded();
  auto &settings = Settings::SettingsService::instance();
  const int configured_size = settings.get<int>("terminal.fontSize");
  const int effective_size =
      (configured_size >= 8 && configured_size <= 72) ? configured_size : 14;

  const std::string configured_font =
      settings.get<std::string>("terminal.fontFamily");
  if (!configured_font.empty()) {
    m_terminal_font_name = resolve_font_family_name(configured_font);
  } else {
    m_terminal_font_name = m_editor_font_name;
  }

  m_terminal_font = std::make_unique<AntialiasedFont>(
      m_terminal_font_name,
      std::max(round_to_int(static_cast<float>(effective_size) * m_dpi_scale),
               10));
  const bool term_ligatures =
      settings.has("terminal.fontLigatures")
          ? settings.get<bool>("terminal.fontLigatures")
          : true;
  m_terminal_font->setLigaturesEnabled(term_ligatures);

  if (m_window_handle) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

void StudioWorkspaceRenderer::reload_ui_font() {
  auto &settings = Settings::SettingsService::instance();
  const int ui_size = settings.get<int>("ui.fontSize");
  const int effective_ui_size = (ui_size >= 8 && ui_size <= 48) ? ui_size : 12;

  const std::string configured_font =
      settings.get<std::string>("ui.fontFamily");
  if (!configured_font.empty() && is_font_available(configured_font)) {
    m_ui_font_name = configured_font;
  } else {
    m_ui_font_name = is_font_available("Open Sans") ? "Open Sans" : "Segoe UI";
  }

  m_ui_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(static_cast<float>(effective_ui_size) * m_dpi_scale), 9));
  m_small_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(static_cast<float>(effective_ui_size) * m_dpi_scale), 9));
  m_large_font = std::make_unique<AntialiasedFont>(
      m_ui_font_name, std::max(round_to_int(24.0F * m_dpi_scale), 18), FW_BOLD);

  if (m_window_handle) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

void StudioWorkspaceRenderer::update_theme(
    const UI::Theme::StudioTheme &theme) {
  m_theme = theme;
  m_palette = UI::Editor::StudioEditorPalette::from_theme(theme);
  m_settings_window.set_theme(theme);
}

void StudioWorkspaceRenderer::show_missing_lsp_dialog(
    const std::string &language_id,
    const std::string &language_name,
    const std::string &server_name) {
  // If the LSP executable exists in plugins/lsp, do not prompt
  const auto existing = Language::Registry::ServerRegistry::instance().find_executable_in_system(server_name);
  if (!existing.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_lsp_install_mutex);
    if (m_installing_lsp_languages.contains(language_id) ||
        m_installing_lsp_languages.contains(server_name)) {
      return;
    }
    // If the server does not exist on disk, ensure any previous dismissal is reset
    m_dismissed_lsp_languages.erase(language_id);
    m_dismissed_lsp_languages.erase(server_name);
  }

  if (m_prompt_modal.is_visible()) {
    return;
  }

  m_prompt_modal.open_lsp_install(
      language_name,
      server_name,
      [this, language_id, server_name]() {
        {
          std::lock_guard<std::mutex> lock(m_lsp_install_mutex);
          m_installing_lsp_languages.insert(language_id);
          m_installing_lsp_languages.insert(server_name);
        }
        install_language_server(language_id, server_name);
      },
      [this, language_id, server_name]() {
        // User dismissed / Later
        std::lock_guard<std::mutex> lock(m_lsp_install_mutex);
        m_dismissed_lsp_languages.insert(language_id);
        m_dismissed_lsp_languages.insert(server_name);
      });
  if (m_window_handle) {
    InvalidateRect(m_window_handle, nullptr, FALSE);
  }
}

void StudioWorkspaceRenderer::install_language_server(
    const std::string &language_id,
    const std::string &server_name) {
  std::thread([this, language_id, server_name]() {
    bool installed = false;
    std::error_code ec;

    // 1. Resolve ZDE software plugins directory for LSP
    std::filesystem::path lsp_root;
    const auto cur = std::filesystem::current_path(ec);
    if (!ec && std::filesystem::is_directory(cur / "plugins", ec)) {
      lsp_root = cur / "plugins" / "lsp";
    } else {
#if defined(_WIN32)
      std::array<wchar_t, 32768> exe_buf{};
      const DWORD len = GetModuleFileNameW(nullptr, exe_buf.data(), static_cast<DWORD>(exe_buf.size()));
      if (len > 0) {
        std::filesystem::path app_dir = std::filesystem::path(exe_buf.data()).parent_path();
        std::filesystem::path p = app_dir;
        for (int i = 0; i < 6 && !p.empty(); ++i) {
          if (std::filesystem::is_directory(p / "plugins", ec)) {
            lsp_root = p / "plugins" / "lsp";
            break;
          }
          if (p == p.parent_path()) break;
          p = p.parent_path();
        }
        if (lsp_root.empty()) {
          lsp_root = app_dir / "plugins" / "lsp";
        }
      }
#endif
    }

    if (lsp_root.empty()) {
#if defined(_WIN32)
      const char* appdata = std::getenv("APPDATA");
      if (appdata) {
        lsp_root = std::filesystem::path(appdata) / "ZDE" / "plugins" / "lsp";
      } else {
        lsp_root = cur / "plugins" / "lsp";
      }
#else
      const char* home = std::getenv("HOME");
      if (home) {
        lsp_root = std::filesystem::path(home) / ".zde" / "plugins" / "lsp";
      } else {
        lsp_root = cur / "plugins" / "lsp";
      }
#endif
    }

    std::filesystem::create_directories(lsp_root, ec);

    const std::string effective_server = server_name.empty() ? language_id : server_name;
    const auto target_dir = lsp_root / effective_server;
    std::filesystem::create_directories(target_dir, ec);

#if defined(_WIN32)
    const std::string default_ext = (effective_server == "jdtls" || language_id == "java") ? ".bat" : ".exe";
    const std::string exe_name = (effective_server.ends_with(".exe") || effective_server.ends_with(".bat") || effective_server.ends_with(".cmd"))
        ? effective_server
        : (effective_server + default_ext);
    const auto target_exe = target_dir / exe_name;

    // Helper: find an existing executable on the host system PATH
    auto find_host_binary = [](const std::string& name) -> std::filesystem::path {
      std::error_code ec_find;
      std::wstring name_w(name.begin(), name.end());
      std::array<wchar_t, 32768> resolved{};
      const DWORD length = SearchPathW(nullptr, name_w.c_str(), nullptr,
                                       static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
      if (length > 0 && length < resolved.size()) {
        std::filesystem::path p(resolved.data());
        if (std::filesystem::exists(p, ec_find) && std::filesystem::is_regular_file(p, ec_find)) {
          return p;
        }
      }

      const std::filesystem::path prog_candidates[] = {
        "C:\\Program Files\\LLVM\\bin\\" + name,
        "C:\\Program Files (x86)\\LLVM\\bin\\" + name,
        "C:\\LLVM\\bin\\" + name
      };
      for (const auto& c : prog_candidates) {
        if (std::filesystem::exists(c, ec_find) && std::filesystem::is_regular_file(c, ec_find)) {
          return c;
        }
      }

      const char* userprofile = std::getenv("USERPROFILE");
      if (userprofile) {
        const std::filesystem::path up(userprofile);
        const std::filesystem::path candidates[] = {
          up / ".cargo" / "bin" / name,
          up / "scoop" / "apps" / "rustup" / "current" / ".cargo" / "bin" / name,
          up / "scoop" / "shims" / name,
          up / "scoop" / "apps" / "llvm" / "current" / "bin" / name,
          up / "AppData" / "Local" / "Microsoft" / "WinGet" / "Links" / name
        };
        for (const auto& c : candidates) {
          if (std::filesystem::exists(c, ec_find) && std::filesystem::is_regular_file(c, ec_find)) {
            return c;
          }
        }
      }

      return {};
    };

    auto run_cmd_hidden = [](const std::string& cmd_line, DWORD timeout_ms) -> bool {
      STARTUPINFOA si{};
      PROCESS_INFORMATION pi{};
      si.cb = sizeof(si);
      si.dwFlags = STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_HIDE;
      std::string mutable_cmd = cmd_line;
      if (CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        DWORD wait_res = WaitForSingleObject(pi.hProcess, timeout_ms);
        DWORD exit_code = 1;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (wait_res == WAIT_OBJECT_0 && exit_code == 0);
      }
      return false;
    };

    auto find_curl_binary = []() -> std::filesystem::path {
      std::error_code ec_c;
      std::array<wchar_t, 32768> resolved{};
      const DWORD length = SearchPathW(nullptr, L"curl.exe", nullptr,
                                       static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
      if (length > 0 && length < resolved.size()) {
        std::filesystem::path p(resolved.data());
        if (std::filesystem::exists(p, ec_c) && std::filesystem::is_regular_file(p, ec_c)) {
          return p;
        }
      }
      const std::filesystem::path git_candidates[] = {
        "C:\\Windows\\System32\\curl.exe",
        "C:\\Program Files\\Git\\mingw64\\bin\\curl.exe",
        "C:\\Program Files\\Git\\usr\\bin\\curl.exe",
        "C:\\Program Files (x86)\\Git\\bin\\curl.exe"
      };
      for (const auto& c : git_candidates) {
        if (std::filesystem::exists(c, ec_c) && std::filesystem::is_regular_file(c, ec_c)) {
          return c;
        }
      }
      const char* up = std::getenv("USERPROFILE");
      if (up) {
        std::filesystem::path up_p(up);
        const std::filesystem::path up_candidates[] = {
          up_p / "scoop" / "shims" / "curl.exe",
          up_p / "scoop" / "apps" / "curl" / "current" / "bin" / "curl.exe",
          up_p / "AppData" / "Local" / "Microsoft" / "WinGet" / "Links" / "curl.exe"
        };
        for (const auto& c : up_candidates) {
          if (std::filesystem::exists(c, ec_c) && std::filesystem::is_regular_file(c, ec_c)) {
            return c;
          }
        }
      }
      return "curl.exe";
    };

    auto find_tar_binary = []() -> std::filesystem::path {
      std::error_code ec_t;
      std::array<wchar_t, 32768> resolved{};
      const DWORD length = SearchPathW(nullptr, L"tar.exe", nullptr,
                                       static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
      if (length > 0 && length < resolved.size()) {
        std::filesystem::path p(resolved.data());
        if (std::filesystem::exists(p, ec_t) && std::filesystem::is_regular_file(p, ec_t)) {
          return p;
        }
      }
      const std::filesystem::path candidates[] = {
        "C:\\Windows\\System32\\tar.exe",
        "C:\\Program Files\\Git\\usr\\bin\\tar.exe",
        "C:\\Program Files (x86)\\Git\\usr\\bin\\tar.exe",
        "C:\\Program Files\\Git\\bin\\tar.exe",
        "C:\\msys64\\usr\\bin\\tar.exe"
      };
      for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec_t) && std::filesystem::is_regular_file(c, ec_t)) {
          return c;
        }
      }
      const char* up = std::getenv("USERPROFILE");
      if (up) {
        const std::filesystem::path up_p(up);
        const std::filesystem::path up_candidates[] = {
          up_p / "scoop" / "shims" / "tar.exe",
          up_p / "scoop" / "apps" / "git" / "current" / "usr" / "bin" / "tar.exe"
        };
        for (const auto& c : up_candidates) {
          if (std::filesystem::exists(c, ec_t) && std::filesystem::is_regular_file(c, ec_t)) {
            return c;
          }
        }
      }
      return {};
    };

    auto find_7z_binary = []() -> std::filesystem::path {
      std::error_code ec_7;
      std::array<wchar_t, 32768> resolved{};
      const DWORD length = SearchPathW(nullptr, L"7z.exe", nullptr,
                                       static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
      if (length > 0 && length < resolved.size()) {
        std::filesystem::path p(resolved.data());
        if (std::filesystem::exists(p, ec_7) && std::filesystem::is_regular_file(p, ec_7)) {
          return p;
        }
      }
      const std::filesystem::path candidates[] = {
        "C:\\Program Files\\7-Zip\\7z.exe",
        "C:\\Program Files (x86)\\7-Zip\\7z.exe"
      };
      for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec_7) && std::filesystem::is_regular_file(c, ec_7)) {
          return c;
        }
      }
      const char* up = std::getenv("USERPROFILE");
      if (up) {
        const std::filesystem::path up_p(up);
        const std::filesystem::path up_candidates[] = {
          up_p / "scoop" / "shims" / "7z.exe",
          up_p / "scoop" / "apps" / "7zip" / "current" / "7z.exe"
        };
        for (const auto& c : up_candidates) {
          if (std::filesystem::exists(c, ec_7) && std::filesystem::is_regular_file(c, ec_7)) {
            return c;
          }
        }
      }
      return {};
    };

    auto find_python_binary = []() -> std::filesystem::path {
      std::error_code ec_py;
      std::array<wchar_t, 32768> resolved{};
      DWORD length = SearchPathW(nullptr, L"python.exe", nullptr,
                                 static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
      if (length > 0 && length < resolved.size()) {
        std::filesystem::path p(resolved.data());
        if (std::filesystem::exists(p, ec_py) && std::filesystem::is_regular_file(p, ec_py)) {
          return p;
        }
      }
      length = SearchPathW(nullptr, L"py.exe", nullptr,
                           static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
      if (length > 0 && length < resolved.size()) {
        std::filesystem::path p(resolved.data());
        if (std::filesystem::exists(p, ec_py) && std::filesystem::is_regular_file(p, ec_py)) {
          return p;
        }
      }
      const char* up = std::getenv("USERPROFILE");
      if (up) {
        const std::filesystem::path up_p(up);
        const std::filesystem::path up_candidates[] = {
          up_p / "scoop" / "shims" / "python.exe",
          up_p / "scoop" / "apps" / "python" / "current" / "python.exe",
          up_p / "AppData" / "Local" / "Programs" / "Python" / "Python312" / "python.exe",
          up_p / "AppData" / "Local" / "Programs" / "Python" / "Python311" / "python.exe",
          up_p / "AppData" / "Local" / "Programs" / "Python" / "Python310" / "python.exe"
        };
        for (const auto& c : up_candidates) {
          if (std::filesystem::exists(c, ec_py) && std::filesystem::is_regular_file(c, ec_py)) {
            return c;
          }
        }
      }
      return {};
    };

    auto download_file = [&](const std::string& url, const std::filesystem::path& dest) -> bool {
      std::error_code ec_dl;
      std::filesystem::remove(dest, ec_dl);
      const auto curl_bin = find_curl_binary();
      std::string curl_cmd = "\"" + curl_bin.string() + "\" -L -k -s -f --retry 2 -A \"ZDE/1.0\" -o \"" + dest.string() + "\" \"" + url + "\"";
      if (run_cmd_hidden(curl_cmd, 300000) && std::filesystem::exists(dest, ec_dl) && std::filesystem::file_size(dest, ec_dl) > 1000) {
        return true;
      }
      std::string ps_cmd = "powershell.exe -NoProfile -NonInteractive -Command \"try { [System.Net.ServicePointManager]::SecurityProtocol = 3072 } catch {}; $wc = New-Object System.Net.WebClient; $wc.Headers.Add('User-Agent', 'Mozilla/5.0'); $wc.DownloadFile('" + url + "', '" + dest.string() + "')\"";
      if (run_cmd_hidden(ps_cmd, 300000) && std::filesystem::exists(dest, ec_dl) && std::filesystem::file_size(dest, ec_dl) > 1000) {
        return true;
      }
      std::string bits_cmd = "bitsadmin.exe /transfer zde_dl /download /priority foreground \"" + url + "\" \"" + dest.string() + "\"";
      if (run_cmd_hidden(bits_cmd, 300000) && std::filesystem::exists(dest, ec_dl) && std::filesystem::file_size(dest, ec_dl) > 1000) {
        return true;
      }
      return false;
    };

    auto extract_archive = [&](const std::filesystem::path& arc_path, const std::filesystem::path& out_dir) -> bool {
      std::string fn = arc_path.filename().string();
      const bool is_targz = (fn.ends_with(".tar.gz") || fn.ends_with(".tgz"));

      const auto tar_bin = find_tar_binary();
      if (!tar_bin.empty()) {
        std::string tar_cmd = is_targz
            ? ("\"" + tar_bin.string() + "\" -xzf \"" + arc_path.string() + "\" -C \"" + out_dir.string() + "\"")
            : ("\"" + tar_bin.string() + "\" -xf \"" + arc_path.string() + "\" -C \"" + out_dir.string() + "\"");
        if (run_cmd_hidden(tar_cmd, 180000)) {
          return true;
        }
      }

      const auto seven_zip = find_7z_binary();
      if (!seven_zip.empty()) {
        std::error_code ec_7z;
        if (is_targz) {
          std::filesystem::path temp_tar_dir = std::filesystem::temp_directory_path() / "zde_tar_extract";
          std::filesystem::create_directories(temp_tar_dir, ec_7z);
          std::string cmd1 = "\"" + seven_zip.string() + "\" x \"" + arc_path.string() + "\" -y -o\"" + temp_tar_dir.string() + "\"";
          if (run_cmd_hidden(cmd1, 180000)) {
            for (const auto& entry : std::filesystem::directory_iterator(temp_tar_dir, ec_7z)) {
              if (entry.path().extension() == ".tar") {
                std::string cmd2 = "\"" + seven_zip.string() + "\" x \"" + entry.path().string() + "\" -y -o\"" + out_dir.string() + "\"";
                run_cmd_hidden(cmd2, 180000);
                std::filesystem::remove(entry.path(), ec_7z);
                break;
              }
            }
            std::filesystem::remove_all(temp_tar_dir, ec_7z);
            return true;
          }
        } else {
          std::string cmd = "\"" + seven_zip.string() + "\" x \"" + arc_path.string() + "\" -y -o\"" + out_dir.string() + "\"";
          if (run_cmd_hidden(cmd, 180000)) {
            return true;
          }
        }
      }

      const auto py_bin = find_python_binary();
      if (!py_bin.empty()) {
        std::string py_cmd = is_targz
            ? ("\"" + py_bin.string() + "\" -c \"import tarfile, sys; tarfile.open(sys.argv[1], 'r:*').extractall(sys.argv[2])\" \"" + arc_path.string() + "\" \"" + out_dir.string() + "\"")
            : ("\"" + py_bin.string() + "\" -c \"import zipfile, sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])\" \"" + arc_path.string() + "\" \"" + out_dir.string() + "\"");
        if (run_cmd_hidden(py_cmd, 180000)) {
          return true;
        }
      }

      // PowerShell fallbacks
      std::error_code ec_ps;
      if (is_targz) {
        std::filesystem::path temp_tar = std::filesystem::temp_directory_path() / (fn + ".tar");
        std::string ps_decompress = "powershell.exe -NoProfile -NonInteractive -Command \""
            "$in = [System.IO.File]::OpenRead('" + arc_path.string() + "'); "
            "$gz = New-Object System.IO.Compression.GZipStream($in, [System.IO.Compression.CompressionMode]::Decompress); "
            "$out = [System.IO.File]::Create('" + temp_tar.string() + "'); "
            "$buf = New-Object byte[] 65536; "
            "while (($read = $gz.Read($buf, 0, $buf.Length)) -gt 0) { $out.Write($buf, 0, $read); } "
            "$out.Close(); $gz.Close(); $in.Close()\"";
        if (run_cmd_hidden(ps_decompress, 180000) && std::filesystem::exists(temp_tar, ec_ps)) {
          if (!tar_bin.empty()) {
            std::string tar_cmd2 = "\"" + tar_bin.string() + "\" -xf \"" + temp_tar.string() + "\" -C \"" + out_dir.string() + "\"";
            run_cmd_hidden(tar_cmd2, 180000);
          } else if (!seven_zip.empty()) {
            std::string cmd2 = "\"" + seven_zip.string() + "\" x \"" + temp_tar.string() + "\" -y -o\"" + out_dir.string() + "\"";
            run_cmd_hidden(cmd2, 180000);
          }
          std::filesystem::remove(temp_tar, ec_ps);
          return true;
        }
      } else {
        std::string ps_expand = "powershell.exe -NoProfile -NonInteractive -Command \"Expand-Archive -Path '" + arc_path.string() + "' -DestinationPath '" + out_dir.string() + "' -Force\"";
        if (run_cmd_hidden(ps_expand, 180000)) {
          return true;
        }
        std::string ps_extract = "powershell.exe -NoProfile -NonInteractive -Command \"$shell = New-Object -ComObject Shell.Application; $zip = $shell.NameSpace('" + arc_path.string() + "'); $dest = $shell.NameSpace('" + out_dir.string() + "'); if ($zip -and $dest) { $dest.CopyHere($zip.Items(), 16) }\"";
        if (run_cmd_hidden(ps_extract, 180000)) {
          return true;
        }
      }
      return false;
    };

    // Check Catalog for registered server release information
    std::string catalog_url;
    if (auto cat = Services::Installation::Catalog::CatalogLoader::instance().find_for_language(language_id)) {
      if (auto art = cat->find_artifact_for_current_platform()) {
        catalog_url = art->url_or_package;
      }
    } else if (auto cat_srv = Services::Installation::Catalog::CatalogLoader::instance().find_entry(effective_server)) {
      if (auto art = cat_srv->find_artifact_for_current_platform()) {
        catalog_url = art->url_or_package;
      }
    }

    std::string download_url;
    std::string archive_ext = ".zip";
    std::string primary_exe = exe_name;

    if (language_id == "cpp" || language_id == "c" || effective_server == "clangd") {
      download_url = "https://github.com/clangd/clangd/releases/download/18.1.3/clangd-windows-18.1.3.zip";
      archive_ext = ".zip";
      primary_exe = "clangd.exe";
    } else if (language_id == "rust" || effective_server == "rust-analyzer") {
      download_url = "https://github.com/rust-lang/rust-analyzer/releases/latest/download/rust-analyzer-x86_64-pc-windows-msvc.zip";
      archive_ext = ".zip";
      primary_exe = "rust-analyzer.exe";
    } else if (language_id == "java" || effective_server == "jdtls" || effective_server == "eclipse.jdtls") {
      download_url = "https://download.eclipse.org/jdtls/snapshots/jdt-language-server-latest.tar.gz";
      archive_ext = ".tar.gz";
      primary_exe = "jdtls.bat";
    } else if (language_id == "cmake" || effective_server == "cmakels" || effective_server == "cmake-language-server" || effective_server == "neocmakelsp") {
      download_url = "https://github.com/neocmakelsp/neocmakelsp/releases/latest/download/neocmakelsp-x86_64-pc-windows-msvc.zip";
      archive_ext = ".zip";
      primary_exe = "neocmakelsp.exe";
    } else if (language_id == "zig" || effective_server == "zls") {
      download_url = "https://github.com/zigtools/zls/releases/latest/download/zls-x86_64-windows.zip";
      archive_ext = ".zip";
      primary_exe = "zls.exe";
    } else if (language_id == "lua" || effective_server == "lua-language-server") {
      download_url = "https://github.com/LuaLS/lua-language-server/releases/download/3.9.3/lua-language-server-3.9.3-win32-x64.zip";
      archive_ext = ".zip";
      primary_exe = "lua-language-server.exe";
    } else if (language_id == "csharp" || effective_server == "csharp-ls" || effective_server == "omnisharp") {
      download_url = "https://github.com/OmniSharp/omnisharp-roslyn/releases/latest/download/omnisharp-win-x64.zip";
      archive_ext = ".zip";
      primary_exe = "OmniSharp.exe";
    }

    if (!catalog_url.empty()) {
      if (catalog_url.starts_with("http://") || catalog_url.starts_with("https://")) {
        download_url = catalog_url;
      }
    }

    // 1. Try host binary copy if present on system PATH
    std::filesystem::path host_bin = find_host_binary(exe_name);
    if (host_bin.empty() && primary_exe != exe_name) {
      host_bin = find_host_binary(primary_exe);
    }
    if (!host_bin.empty()) {
      installed = Compat::copy_file(host_bin, target_exe, true);
    }

    // 2. If not on host, download from GitHub/official source via curl
    if (!installed && !download_url.empty()) {
      std::filesystem::path temp_arc = std::filesystem::temp_directory_path() / (effective_server + archive_ext);
      if (download_file(download_url, temp_arc)) {
        extract_archive(temp_arc, target_dir);
        std::filesystem::remove(temp_arc, ec);

        // Flatten single nested directory if present (e.g. clangd_18.1.3/bin/clangd.exe -> bin/clangd.exe)
        std::error_code ec_flat;
        for (const auto& sub : std::filesystem::directory_iterator(target_dir, ec_flat)) {
          if (sub.is_directory(ec_flat)) {
            const auto sub_bin = sub.path() / "bin";
            if (std::filesystem::exists(sub_bin, ec_flat)) {
              for (const auto& item : std::filesystem::directory_iterator(sub.path(), ec_flat)) {
                const auto dest = target_dir / item.path().filename();
                if (!std::filesystem::exists(dest, ec_flat)) {
                  std::filesystem::copy(item.path(), dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec_flat);
                }
              }
              break;
            }
          }
        }

        installed = std::filesystem::exists(target_exe, ec);

        // If not directly in target_dir, search target_dir recursively
        if (!installed) {
          for (const auto& entry : std::filesystem::recursive_directory_iterator(target_dir, ec)) {
            if (entry.is_regular_file(ec)) {
              const std::string fn = entry.path().filename().string();
              if (fn == exe_name || fn == primary_exe) {
                Compat::copy_file(entry.path(), target_exe, true);
                installed = std::filesystem::exists(target_exe, ec);
                break;
              }
            }
          }
        }

        // Special handling for cmake: copy neocmakelsp.exe to cmake-language-server.exe and cmakels.exe
        if (language_id == "cmake" || effective_server == "cmakels" || effective_server == "cmake-language-server" || effective_server == "neocmakelsp") {
          for (const auto& entry : std::filesystem::recursive_directory_iterator(target_dir, ec)) {
            if (entry.is_regular_file(ec)) {
              const std::string fn = entry.path().filename().string();
              if (fn == "neocmakelsp.exe" || fn == "cmakels.exe" || fn == "cmake-language-server.exe" || fn == "cmakels-win64.exe") {
                Compat::copy_file(entry.path(), target_dir / "cmake-language-server.exe", true);
                Compat::copy_file(entry.path(), target_dir / "cmakels.exe", true);
                Compat::copy_file(entry.path(), target_dir / "neocmakelsp.exe", true);
                installed = true;
                break;
              }
            }
          }
        }

        // Special handling for clangd: ensure root target_dir / "clangd.exe" exists
        if (language_id == "cpp" || language_id == "c" || effective_server == "clangd") {
          const auto bin_clangd = target_dir / "bin" / "clangd.exe";
          if (std::filesystem::exists(bin_clangd, ec)) {
            Compat::copy_file(bin_clangd, target_dir / "clangd.exe", true);
            installed = true;
          }
        }

        // Special handling for jdtls: check bin/jdtls.bat or bin/jdtls.cmd
        if (!installed && (language_id == "java" || effective_server == "jdtls" || effective_server == "eclipse.jdtls")) {
          const auto bin_bat = target_dir / "bin" / "jdtls.bat";
          const auto bin_cmd = target_dir / "bin" / "jdtls.cmd";
          if (std::filesystem::exists(bin_bat, ec) || std::filesystem::exists(bin_cmd, ec)) {
            const auto root_bat = target_dir / "jdtls.bat";
            std::ofstream ofs(root_bat);
            if (ofs) {
              ofs << "@echo off\r\ncall \"%~dp0bin\\jdtls.bat\" %*\r\n";
            }
            installed = true;
          }
        }

        // Special handling for lua: copy bin/lua-language-server.exe to root
        if (language_id == "lua" || effective_server == "lua-language-server") {
          const auto bin_lua = target_dir / "bin" / "lua-language-server.exe";
          if (std::filesystem::exists(bin_lua, ec)) {
            Compat::copy_file(bin_lua, target_dir / "lua-language-server.exe", true);
            installed = true;
          }
        }

        // Special handling for csharp / omnisharp
        if (language_id == "csharp" || effective_server == "omnisharp" || effective_server == "csharp-ls") {
          const auto omni = target_dir / "OmniSharp.exe";
          if (std::filesystem::exists(omni, ec)) {
            Compat::copy_file(omni, target_dir / "csharp-ls.exe", true);
            installed = true;
          }
        }

        // Fallback: check any .exe or .bat matching server
        if (!installed) {
          for (const auto& entry : std::filesystem::recursive_directory_iterator(target_dir, ec)) {
            if (entry.is_regular_file(ec)) {
              const std::string ext = entry.path().extension().string();
              if (ext == ".exe" || ext == ".bat" || ext == ".cmd") {
                Compat::copy_file(entry.path(), target_exe, true);
                installed = std::filesystem::exists(target_exe, ec);
                if (installed) break;
              }
            }
          }
        }
      }
    }

    if (!installed) {
      if (std::filesystem::exists(target_dir, ec) && std::filesystem::is_empty(target_dir, ec)) {
        std::filesystem::remove(target_dir, ec);
      }
    }
#else
    const auto target_exe = target_dir / effective_server;
    if (const char* path_env = std::getenv("PATH")) {
      std::stringstream ss(path_env);
      std::string dir;
      while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        std::filesystem::path c = std::filesystem::path(dir) / effective_server;
        if (std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec)) {
          std::filesystem::copy_file(c, target_exe, std::filesystem::copy_options::overwrite_existing, ec);
          installed = (!ec && std::filesystem::exists(target_exe, ec));
          break;
        }
      }
    }
#endif

    {
      std::lock_guard<std::mutex> lock(m_lsp_install_mutex);
      m_installing_lsp_languages.erase(language_id);
      m_installing_lsp_languages.erase(server_name);
      if (!installed) {
        m_dismissed_lsp_languages.insert(language_id);
        m_dismissed_lsp_languages.insert(server_name);
      }
    }

    if (installed) {
      Language::LanguageServerManager::instance().clear_unavailable_language(language_id);
      Language::Registry::ServerRegistry::instance().clear_cache();
      m_text_editor.sync_lsp_active_document();
    }
    if (m_window_handle) {
      InvalidateRect(m_window_handle, nullptr, FALSE);
    }
  }).detach();
}

} // namespace Zenvra::Platform::Win32::Components
