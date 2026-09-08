#include "UI/Settings/SettingsWindow.h"
#include "Utility/Ascii/AsciiMascotRenderer.h"
#include "Utility/TextEncoding.h"
#include "Utility/stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

#include <lunasvg.h>

#if defined(_WIN32)

#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

namespace Zenvra::UI::Settings {

namespace {

static constexpr const wchar_t *settings_class_name = L"ZDE_SettingsWindow";

COLORREF to_color_ref(const Theme::Color &c) noexcept {
  return RGB(c.red, c.green, c.blue);
}

RECT to_native_rect(const Rect &r) noexcept {
  return RECT{static_cast<LONG>(r.x), static_cast<LONG>(r.y),
              static_cast<LONG>(r.right()), static_cast<LONG>(r.bottom())};
}

void draw_rect_solid(HDC hdc, const Rect &r, COLORREF color) {
  if (r.is_empty())
    return;
  const RECT rc = to_native_rect(r);
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(hdc, &rc, brush);
  DeleteObject(brush);
}

void draw_rounded_rect(HDC hdc, const Rect &r, COLORREF fill, COLORREF border,
                       float radius) {
  if (r.is_empty())
    return;
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ old_brush = SelectObject(hdc, brush);
  HGDIOBJ old_pen = SelectObject(hdc, pen);

  const int d = static_cast<int>(radius * 2.0F);
  RoundRect(hdc, static_cast<int>(r.x), static_cast<int>(r.y),
            static_cast<int>(r.right()), static_cast<int>(r.bottom()), d, d);

  SelectObject(hdc, old_pen);
  SelectObject(hdc, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);
}

std::filesystem::path resolve_asset_path(const std::string &rel_path) {
  std::error_code ec;
  std::vector<std::string> path_variants;
  path_variants.push_back(rel_path);

  std::string normalized = rel_path;
  for (char &c : normalized) {
    if (c == '\\')
      c = '/';
  }
  path_variants.push_back(normalized);

  std::string clean = normalized;
  if (clean.starts_with("Assets/icons/"))
    clean = clean.substr(13);
  else if (clean.starts_with("Assets/"))
    clean = clean.substr(7);
  else if (clean.starts_with("Resources/icons/"))
    clean = clean.substr(16);
  else if (clean.starts_with("Resources/"))
    clean = clean.substr(10);
  else if (clean.starts_with("icons/"))
    clean = clean.substr(6);

  auto add_variants = [&](const std::string &sub) {
    path_variants.push_back("Assets/icons/" + sub);
    path_variants.push_back("Resources/icons/" + sub);
    path_variants.push_back("Resources/" + sub);
    path_variants.push_back("icons/" + sub);
    path_variants.push_back(sub);
  };

  add_variants(clean);

  // 1. Try executable directory first
  std::array<wchar_t, 4096> exe_buf{};
  DWORD len = GetModuleFileNameW(nullptr, exe_buf.data(),
                                 static_cast<DWORD>(exe_buf.size()));
  if (len > 0) {
    std::filesystem::path exe_dir =
        std::filesystem::path(exe_buf.data()).parent_path();
    for (int i = 0; i < 6 && !exe_dir.empty(); ++i) {
      for (const auto &var : path_variants) {
        std::filesystem::path candidate = exe_dir / var;
        if (std::filesystem::exists(candidate, ec))
          return candidate;
      }
      if (!exe_dir.has_parent_path() || exe_dir == exe_dir.parent_path())
        break;
      exe_dir = exe_dir.parent_path();
    }
  }

  // 2. Try current working directory
  std::filesystem::path cur = std::filesystem::current_path(ec);
  for (int i = 0; i < 6 && !cur.empty(); ++i) {
    for (const auto &var : path_variants) {
      std::filesystem::path candidate = cur / var;
      if (std::filesystem::exists(candidate, ec))
        return candidate;
    }
    if (!cur.has_parent_path() || cur == cur.parent_path())
      break;
    cur = cur.parent_path();
  }

  return std::filesystem::path(rel_path);
}

} // namespace

SettingsWindow::SettingsWindow() {
  m_search_input.set_placeholder("Search settings");
  m_active_category = "Commonly Used";
  m_category_expanded["Text Editor"] = true;
  m_category_expanded["Workbench"] = false;
  m_category_expanded["Window"] = false;
  m_category_expanded["Features"] = false;
  m_category_expanded["Application"] = false;
  m_category_expanded["Security"] = false;
}

SettingsWindow::~SettingsWindow() {
  close();
  if (m_regular_font) {
    DeleteObject(m_regular_font);
    m_regular_font = nullptr;
  }
  if (m_semibold_font) {
    DeleteObject(m_semibold_font);
    m_semibold_font = nullptr;
  }
  if (m_small_font) {
    DeleteObject(m_small_font);
    m_small_font = nullptr;
  }
  if (m_title_font) {
    DeleteObject(m_title_font);
    m_title_font = nullptr;
  }
  if (m_header_large_font) {
    DeleteObject(m_header_large_font);
    m_header_large_font = nullptr;
  }
  for (auto &[key, entry] : m_icon_cache) {
    if (entry.bitmap) {
      DeleteObject(entry.bitmap);
      entry.bitmap = nullptr;
    }
  }
  m_icon_cache.clear();
}

void SettingsWindow::refresh_fonts() {
  if (m_regular_font)
    DeleteObject(m_regular_font);
  if (m_semibold_font)
    DeleteObject(m_semibold_font);
  if (m_small_font)
    DeleteObject(m_small_font);
  if (m_title_font)
    DeleteObject(m_title_font);
  if (m_header_large_font)
    DeleteObject(m_header_large_font);

  const int font_size_16pt = -MulDiv(16, static_cast<int>(m_dpi), 72);
  const int font_size_11pt = -MulDiv(11, static_cast<int>(m_dpi), 72);
  const int font_size_9pt = -MulDiv(9, static_cast<int>(m_dpi), 72);
  const int font_size_8pt = -MulDiv(8, static_cast<int>(m_dpi), 72);

  m_header_large_font = CreateFontW(
      font_size_16pt, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
  if (!m_header_large_font) {
    m_header_large_font = CreateFontW(
        font_size_16pt, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  }

  m_title_font = CreateFontW(font_size_11pt, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
  if (!m_title_font) {
    m_title_font = CreateFontW(font_size_11pt, 0, 0, 0, FW_BOLD, FALSE, FALSE,
                               FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  }

  m_regular_font =
      CreateFontW(font_size_9pt, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

  m_semibold_font = CreateFontW(
      font_size_9pt, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
  if (!m_semibold_font) {
    m_semibold_font = CreateFontW(
        font_size_9pt, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  }

  m_small_font =
      CreateFontW(font_size_8pt, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void SettingsWindow::draw_icon(HDC dc, const std::string &icon_rel_path, int x,
                               int y, int size,
                               std::optional<COLORREF> tint_color) const {
  if (icon_rel_path.empty() || size <= 0)
    return;

  std::filesystem::path full_path = resolve_asset_path(icon_rel_path);
  std::error_code ec;
  if (!std::filesystem::exists(full_path, ec)) {
    return;
  }

  const std::string cache_key =
      full_path.string() + "@" + std::to_string(size) +
      (tint_color.has_value() ? ("#" + std::to_string(tint_color.value()))
                              : "");
  auto it = m_icon_cache.find(cache_key);
  HBITMAP icon_bm = nullptr;

  if (it != m_icon_cache.end() && it->second.bitmap != nullptr) {
    icon_bm = it->second.bitmap;
  } else {
    auto doc = lunasvg::Document::loadFromFile(full_path.string());
    if (!doc)
      return;

    auto bitmap = doc->renderToBitmap(static_cast<std::uint32_t>(size),
                                      static_cast<std::uint32_t>(size));
    if (bitmap.isNull())
      return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC temp_dc = CreateCompatibleDC(dc);
    icon_bm =
        CreateDIBSection(temp_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    DeleteDC(temp_dc);

    if (!icon_bm || !bits)
      return;

    const auto *src = reinterpret_cast<const std::uint32_t *>(bitmap.data());
    auto *dst = reinterpret_cast<std::uint32_t *>(bits);
    const std::size_t pixel_count =
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size);

    if (tint_color.has_value()) {
      const std::uint32_t tr = GetRValue(*tint_color);
      const std::uint32_t tg = GetGValue(*tint_color);
      const std::uint32_t tb = GetBValue(*tint_color);
      for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::uint32_t a = (src[i] >> 24U) & 0xFFU;
        if (a == 0) {
          dst[i] = 0;
          continue;
        }
        const std::uint32_t red = (tr * a) / 255U;
        const std::uint32_t green = (tg * a) / 255U;
        const std::uint32_t blue = (tb * a) / 255U;
        dst[i] = (a << 24U) | (red << 16U) | (green << 8U) | blue;
      }
    } else {
      std::memcpy(dst, src, pixel_count * sizeof(std::uint32_t));
    }

    m_icon_cache[cache_key] = CachedBitmap{icon_bm, size, size};
  }

  HDC mem_dc = CreateCompatibleDC(dc);
  HGDIOBJ old_bm = SelectObject(mem_dc, icon_bm);

  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.BlendFlags = 0;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;

  AlphaBlend(dc, x, y, size, size, mem_dc, 0, 0, size, size, blend);

  SelectObject(mem_dc, old_bm);
  DeleteDC(mem_dc);
}

void SettingsWindow::draw_mascot_thumbnail(
    HDC dc, const std::string &image_path, const Rect &bounds,
    const Theme::StudioTheme &theme, float dpi_scale) const {
  if (bounds.is_empty())
    return;

  const COLORREF box_bg = to_color_ref(theme.command_center_background);
  const COLORREF box_border = to_color_ref(theme.command_center_border);
  draw_rounded_rect(dc, bounds, box_bg, box_border, 3.0F * dpi_scale);

  std::filesystem::path target_path{image_path};
  std::error_code ec;
  if (!std::filesystem::is_regular_file(target_path, ec)) {
    target_path = resolve_asset_path(image_path);
    if (!std::filesystem::is_regular_file(target_path, ec)) {
      target_path = resolve_asset_path("Assets/icons/zenvra_logo.png");
      if (!std::filesystem::is_regular_file(target_path, ec)) {
        target_path = resolve_asset_path("zenvra_logo.png");
        if (!std::filesystem::is_regular_file(target_path, ec)) {
          target_path = Utility::Ascii::AsciiArtConverter::resolve_image_path(image_path);
          if (!std::filesystem::is_regular_file(target_path, ec)) {
            target_path = Utility::Ascii::AsciiArtConverter::resolve_image_path("zenvra_logo.png");
          }
        }
      }
    }
  }

  if (!std::filesystem::is_regular_file(target_path, ec)) {
    SelectObject(dc, m_small_font);
    SetTextColor(dc, to_color_ref(theme.text_secondary));
    RECT r = to_native_rect(bounds);
    DrawTextW(dc, L"IMG", -1, &r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return;
  }

  const int thumb_w = static_cast<int>(bounds.width - 6.0F * dpi_scale);
  const int thumb_h = static_cast<int>(bounds.height - 6.0F * dpi_scale);
  if (thumb_w <= 0 || thumb_h <= 0)
    return;

  auto &service = Zenvra::Settings::SettingsService::instance();
  const std::string render_mode =
      service.get_schema().has_setting("workbench.mascot.renderMode")
          ? service.get<std::string>("workbench.mascot.renderMode")
          : "default";
  if (render_mode == "ascii") {
    HBITMAP ascii_bm =
        Utility::Ascii::AsciiMascotRenderer::create_ascii_thumbnail_bitmap(
            dc, target_path.string(), thumb_w, thumb_h, box_bg);
    if (ascii_bm) {
      const int draw_x =
          static_cast<int>(bounds.x + (bounds.width - thumb_w) * 0.5F);
      const int draw_y =
          static_cast<int>(bounds.y + (bounds.height - thumb_h) * 0.5F);

      HDC mem_dc = CreateCompatibleDC(dc);
      HGDIOBJ prev_bm = SelectObject(mem_dc, ascii_bm);
      BitBlt(dc, draw_x, draw_y, thumb_w, thumb_h, mem_dc, 0, 0, SRCCOPY);
      SelectObject(mem_dc, prev_bm);
      DeleteDC(mem_dc);
      return;
    }
  }

  const std::string cache_key = target_path.string() + "@thumb#" +
                                std::to_string(thumb_w) + "x" +
                                std::to_string(thumb_h);
  HBITMAP thumb_bm = nullptr;
  auto it = m_icon_cache.find(cache_key);
  if (it != m_icon_cache.end() && it->second.bitmap != nullptr) {
    thumb_bm = it->second.bitmap;
  } else {
    int img_w = 0, img_h = 0, channels = 0;
    unsigned char *data =
        stbi_load(target_path.string().c_str(), &img_w, &img_h, &channels, 4);
    if (data) {
      int fit_w = thumb_w;
      int fit_h = thumb_h;
      if (img_w > 0 && img_h > 0) {
        const float aspect = static_cast<float>(img_w) / static_cast<float>(img_h);
        if (aspect > 1.0F) {
          fit_w = thumb_w;
          fit_h = std::max(1, static_cast<int>(thumb_w / aspect));
        } else {
          fit_h = thumb_h;
          fit_w = std::max(1, static_cast<int>(thumb_h * aspect));
        }
      }

      BITMAPINFO bmi{};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = fit_w;
      bmi.bmiHeader.biHeight = -fit_h;
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      void *bits = nullptr;
      HDC temp_dc = CreateCompatibleDC(dc);
      thumb_bm =
          CreateDIBSection(temp_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
      DeleteDC(temp_dc);

      if (thumb_bm && bits) {
        auto *dst = reinterpret_cast<uint32_t *>(bits);
        const COLORREF bg = to_color_ref(theme.command_center_background);
        const uint8_t bg_r = GetRValue(bg);
        const uint8_t bg_g = GetGValue(bg);
        const uint8_t bg_b = GetBValue(bg);

        for (int y = 0; y < fit_h; ++y) {
          const int src_y = (y * img_h) / fit_h;
          for (int x = 0; x < fit_w; ++x) {
            const int src_x = (x * img_w) / fit_w;
            const int src_idx = (src_y * img_w + src_x) * 4;
            const uint8_t r = data[src_idx];
            const uint8_t g = data[src_idx + 1];
            const uint8_t b = data[src_idx + 2];
            const uint8_t a = data[src_idx + 3];

            const uint8_t out_r = static_cast<uint8_t>((r * a + bg_r * (255 - a)) / 255);
            const uint8_t out_g = static_cast<uint8_t>((g * a + bg_g * (255 - a)) / 255);
            const uint8_t out_b = static_cast<uint8_t>((b * a + bg_b * (255 - a)) / 255);

            dst[y * fit_w + x] = (out_r << 16) | (out_g << 8) | out_b;
          }
        }
        m_icon_cache[cache_key] = CachedBitmap{thumb_bm, fit_w, fit_h};
      }
      stbi_image_free(data);
    }
  }

  if (thumb_bm) {
    auto entry = m_icon_cache[cache_key];
    const int fit_w = entry.width;
    const int fit_h = entry.height;
    const int draw_x = static_cast<int>(bounds.x + (bounds.width - fit_w) * 0.5F);
    const int draw_y = static_cast<int>(bounds.y + (bounds.height - fit_h) * 0.5F);

    HDC mem_dc = CreateCompatibleDC(dc);
    HGDIOBJ prev_bm = SelectObject(mem_dc, thumb_bm);
    BitBlt(dc, draw_x, draw_y, fit_w, fit_h, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, prev_bm);
    DeleteDC(mem_dc);
  }
}

bool SettingsWindow::browse_mascot_image() {
  wchar_t file_name[MAX_PATH] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(OPENFILENAMEW);
  ofn.hwndOwner = m_hwnd;
  ofn.lpstrFilter =
      L"Supported Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0"
      L"PNG Images (*.png)\0*.png\0"
      L"JPEG Images (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
      L"Bitmap Images (*.bmp)\0*.bmp\0"
      L"All Files (*.*)\0*.*\0";
  ofn.lpstrFile = file_name;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Select Mascot Image";
  ofn.Flags =
      OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING;

  if (GetOpenFileNameW(&ofn)) {
    const std::string chosen_path =
        Utility::wide_to_utf8(file_name).value_or("");
    if (!chosen_path.empty()) {
      Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
      Utility::Ascii::AsciiArtConverter::clear_cache();
      auto &service = Zenvra::Settings::SettingsService::instance();
      service.set("workbench.mascot.image", chosen_path, m_active_scope);
      if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        UpdateWindow(m_hwnd);
      }
      if (m_parent_hwnd && IsWindow(m_parent_hwnd)) {
        InvalidateRect(m_parent_hwnd, nullptr, FALSE);
        UpdateWindow(m_parent_hwnd);
      }
      return true;
    }
  }
  return false;
}

void SettingsWindow::open(HWND parent_hwnd) {
  m_parent_hwnd = parent_hwnd;

  if (m_hwnd != nullptr && IsWindow(m_hwnd)) {
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    return;
  }

  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = dialog_proc;
  wc.cbWndExtra = sizeof(SettingsWindow *);
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = settings_class_name;

  RegisterClassExW(&wc);

  m_dpi = parent_hwnd ? GetDpiForWindow(parent_hwnd) : 96;
  if (m_dpi == 0)
    m_dpi = 96;
  refresh_fonts();
  const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;

  const int width = static_cast<int>(880.0F * dpi_scale);
  const int height = static_cast<int>(620.0F * dpi_scale);

  int pos_x = CW_USEDEFAULT;
  int pos_y = CW_USEDEFAULT;

  if (parent_hwnd && IsWindow(parent_hwnd)) {
    RECT parent_rc{};
    GetWindowRect(parent_hwnd, &parent_rc);
    pos_x = parent_rc.left + (parent_rc.right - parent_rc.left - width) / 2;
    pos_y = parent_rc.top + (parent_rc.bottom - parent_rc.top - height) / 2;
  }

  m_hwnd =
      CreateWindowExW(WS_EX_APPWINDOW, settings_class_name, L"Settings",
                      WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME, pos_x, pos_y,
                      width, height, parent_hwnd, nullptr, instance, this);

  if (m_hwnd != nullptr) {
    // 1. Enable immersive dark mode
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                          sizeof(dark));

    // 2. Rounded corners preference
    constexpr DWORD dwm_corner_preference_attr = 33;
    constexpr DWORD dwm_corner_round = 2;
    DwmSetWindowAttribute(m_hwnd, dwm_corner_preference_attr, &dwm_corner_round,
                          sizeof(dwm_corner_round));

    // 3. Extend frame margins into client area for hardware dropshadow
    const MARGINS frame_margins{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(m_hwnd, &frame_margins);

    // 4. Subtle native border color on Windows 11
    COLORREF border_color = to_color_ref(m_theme.titlebar_border);
    DwmSetWindowAttribute(m_hwnd, 34 /* DWMWA_BORDER_COLOR */, &border_color,
                          sizeof(border_color));

    // 5. Accept file drag and drop for mascot image upload
    DragAcceptFiles(m_hwnd, TRUE);

    m_search_input.set_focused(true);
    m_caret_visible = true;

    SetTimer(m_hwnd, 1, 500, nullptr);
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
  }
}

void SettingsWindow::close() {
  if (m_hwnd != nullptr && IsWindow(m_hwnd)) {
    KillTimer(m_hwnd, 1);
    HWND hwnd = m_hwnd;
    m_hwnd = nullptr;
    DestroyWindow(hwnd);
  }

  if (m_parent_hwnd != nullptr && IsWindow(m_parent_hwnd)) {
    SetForegroundWindow(m_parent_hwnd);
    SetFocus(m_parent_hwnd);
    SetActiveWindow(m_parent_hwnd);
    InvalidateRect(m_parent_hwnd, nullptr, FALSE);
  }

  m_close_hovered = false;
  m_user_tab_hovered = false;
  m_workspace_tab_hovered = false;
  m_hovered_category.clear();
  m_scrollbar_thumb_hovered = false;
  m_search_clear_hovered = false;
  m_is_dragging_scrollbar = false;
}

void SettingsWindow::toggle(HWND parent_hwnd) {
  if (is_visible()) {
    if (m_hwnd != nullptr && GetForegroundWindow() != m_hwnd) {
      SetForegroundWindow(m_hwnd);
      SetFocus(m_hwnd);
      return;
    }
    close();
  } else {
    open(parent_hwnd);
  }
}

LRESULT CALLBACK SettingsWindow::dialog_proc(HWND hwnd, UINT message,
                                             WPARAM w_param, LPARAM l_param) {
  SettingsWindow *self = nullptr;
  if (message == WM_NCCREATE) {
    auto *cs = reinterpret_cast<CREATESTRUCTW *>(l_param);
    self = reinterpret_cast<SettingsWindow *>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<SettingsWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self) {
    return self->handle_message(hwnd, message, w_param, l_param);
  }

  return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT SettingsWindow::handle_message(HWND hwnd, UINT message, WPARAM w_param,
                                       LPARAM l_param) {
  switch (message) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;

    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bm =
        CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
    HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);

    const auto layout = calculate_layout(width, height, dpi_scale);
    render(mem_dc, layout, m_theme, dpi_scale);

    BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem_dc, 0, 0,
           SRCCOPY);

    SelectObject(mem_dc, old_bm);
    DeleteObject(mem_bm);
    DeleteDC(mem_dc);
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_ERASEBKGND:
    return 1;

  case WM_DROPFILES: {
    const HDROP drop = reinterpret_cast<HDROP>(w_param);
    if (drop != nullptr) {
      const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
      if (count > 0) {
        wchar_t dropped_buf[MAX_PATH] = L"";
        if (DragQueryFileW(drop, 0, dropped_buf, MAX_PATH) > 0) {
          const std::filesystem::path dropped{dropped_buf};
          const std::string ext = dropped.extension().string();
          std::string lower_ext;
          for (char c : ext) {
            lower_ext +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          }
          if (lower_ext == ".png" || lower_ext == ".jpg" ||
              lower_ext == ".jpeg" || lower_ext == ".bmp") {
            Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
            Utility::Ascii::AsciiArtConverter::clear_cache();
            auto &service = Zenvra::Settings::SettingsService::instance();
            service.set("workbench.mascot.image", dropped.string(),
                        m_active_scope);
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            if (m_parent_hwnd && IsWindow(m_parent_hwnd)) {
              InvalidateRect(m_parent_hwnd, nullptr, FALSE);
              UpdateWindow(m_parent_hwnd);
            }
          }
        }
      }
      DragFinish(drop);
    }
    return 0;
  }

  case WM_TIMER:
    if (w_param == 1) {
      m_caret_visible = !m_caret_visible;
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;

  case WM_NCCALCSIZE:
    return 0;

  case WM_NCPAINT:
    return 0;

  case WM_NCACTIVATE:
    InvalidateRect(hwnd, nullptr, FALSE);
    return TRUE;

  case WM_GETMINMAXINFO: {
    auto *mmi = reinterpret_cast<MINMAXINFO *>(l_param);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    mmi->ptMinTrackSize.x = static_cast<LONG>(640.0F * dpi_scale);
    mmi->ptMinTrackSize.y = static_cast<LONG>(420.0F * dpi_scale);
    return 0;
  }

  case WM_NCHITTEST: {
    POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    ScreenToClient(hwnd, &pt);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;

    // Border resizing
    const int border = static_cast<int>(6.0F * dpi_scale);
    const bool on_left = pt.x < border;
    const bool on_right = pt.x >= rc.right - border;
    const bool on_top = pt.y < border;
    const bool on_bottom = pt.y >= rc.bottom - border;

    if (on_top && on_left)
      return HTTOPLEFT;
    if (on_top && on_right)
      return HTTOPRIGHT;
    if (on_bottom && on_left)
      return HTBOTTOMLEFT;
    if (on_bottom && on_right)
      return HTBOTTOMRIGHT;
    if (on_left)
      return HTLEFT;
    if (on_right)
      return HTRIGHT;
    if (on_top)
      return HTTOP;
    if (on_bottom)
      return HTBOTTOM;

    const auto layout = calculate_layout(width, height, dpi_scale);

    // Close button handles client click
    if (layout.close_btn_bounds.contains(static_cast<float>(pt.x),
                                         static_cast<float>(pt.y))) {
      return HTCLIENT;
    }

    // Scope tabs handle client click
    if (layout.user_tab_bounds.contains(static_cast<float>(pt.x),
                                        static_cast<float>(pt.y)) ||
        layout.workspace_tab_bounds.contains(static_cast<float>(pt.x),
                                             static_cast<float>(pt.y))) {
      return HTCLIENT;
    }

    // Titlebar area is draggable
    if (layout.header_bounds.contains(static_cast<float>(pt.x),
                                      static_cast<float>(pt.y))) {
      return HTCAPTION;
    }

    return HTCLIENT;
  }

  case WM_MOUSEMOVE: {
    const float x = static_cast<float>(GET_X_LPARAM(l_param));
    const float y = static_cast<float>(GET_Y_LPARAM(l_param));
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout =
        calculate_layout(static_cast<float>(rc.right - rc.left),
                         static_cast<float>(rc.bottom - rc.top), dpi_scale);

    bool need_redraw = false;
    if (m_is_dragging_font_scrollbar && m_font_dropdown_max_scroll > 0.0F) {
      const float track_h = m_font_dropdown_scrollbar_track.height;
      const float thumb_h = m_font_dropdown_scrollbar_thumb.height;
      const float travel = track_h - thumb_h;
      if (travel > 0.0F) {
        const float delta_y = y - m_drag_start_font_y;
        const float delta_scroll =
            (delta_y / travel) * m_font_dropdown_max_scroll;
        m_font_dropdown_scroll =
            std::clamp(m_drag_start_font_scroll + delta_scroll, 0.0F,
                       m_font_dropdown_max_scroll);
        need_redraw = true;
      }
    } else if (m_is_dragging_sidebar_scrollbar && m_sidebar_max_scroll > 0.0F) {
      const float track_h = layout.sidebar_scrollbar_track.height;
      const float thumb_h = layout.sidebar_scrollbar_thumb.height;
      const float travel = track_h - thumb_h;
      if (travel > 0.0F) {
        const float delta_y = y - m_drag_start_sidebar_y;
        const float delta_scroll = (delta_y / travel) * m_sidebar_max_scroll;
        m_sidebar_scroll_offset =
            std::clamp(m_drag_start_sidebar_scroll_offset + delta_scroll, 0.0F,
                       m_sidebar_max_scroll);
        need_redraw = true;
      }
    } else if (m_is_dragging_scrollbar && m_max_scroll > 0.0F) {
      const float track_h = layout.scrollbar_track.height;
      const float thumb_h = layout.scrollbar_thumb.height;
      const float travel = track_h - thumb_h;
      if (travel > 0.0F) {
        const float delta_y = y - m_drag_start_y;
        const float delta_scroll = (delta_y / travel) * m_max_scroll;
        m_scroll_offset = std::clamp(m_drag_start_scroll_offset + delta_scroll,
                                     0.0F, m_max_scroll);
        need_redraw = true;
      }
    } else {
      need_redraw = handle_pointer_move(x, y, layout);
    }

    if (need_redraw) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }

    TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }

  case WM_MOUSELEAVE: {
    m_close_hovered = false;
    m_user_tab_hovered = false;
    m_workspace_tab_hovered = false;
    m_hovered_category.clear();
    m_scrollbar_thumb_hovered = false;
    m_sidebar_scrollbar_thumb_hovered = false;
    m_search_clear_hovered = false;
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  }

  case WM_SETCURSOR: {
    if (LOWORD(l_param) == HTCLIENT) {
      POINT cursor_pos{};
      GetCursorPos(&cursor_pos);
      ScreenToClient(hwnd, &cursor_pos);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
      const auto layout =
          calculate_layout(static_cast<float>(rc.right - rc.left),
                           static_cast<float>(rc.bottom - rc.top), dpi_scale);
      const float cur_x = static_cast<float>(cursor_pos.x);
      const float cur_y = static_cast<float>(cursor_pos.y);

      if (m_font_dropdown_open &&
          m_font_dropdown_bounds.contains(cur_x, cur_y)) {
        if (!m_font_dropdown_scrollbar_track.is_empty() &&
            m_font_dropdown_scrollbar_track.contains(cur_x, cur_y)) {
          SetCursor(LoadCursor(nullptr, IDC_ARROW));
        } else {
          SetCursor(LoadCursor(nullptr, IDC_HAND));
        }
        return TRUE;
      }
      if (layout.search_clear_btn_bounds.contains(cur_x, cur_y)) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
      }
      if (layout.search_bar_bounds.contains(cur_x, cur_y)) {
        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
        return TRUE;
      }
      if (layout.close_btn_bounds.contains(cur_x, cur_y) ||
          layout.user_tab_bounds.contains(cur_x, cur_y) ||
          layout.workspace_tab_bounds.contains(cur_x, cur_y)) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
      }
      if (!layout.sidebar_scrollbar_thumb.is_empty() &&
          layout.sidebar_scrollbar_thumb.contains(cur_x, cur_y)) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
      }
      if (layout.sidebar_bounds.contains(cur_x, cur_y)) {
        for (const auto &item : layout.category_items) {
          if (item.bounds.contains(cur_x, cur_y)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
          }
        }
      }
      for (const auto &row : layout.rows) {
        if (layout.content_bounds.contains(cur_x, cur_y)) {
          if (!row.input_bounds.is_empty() &&
              row.input_bounds.contains(cur_x, cur_y) && row.def &&
              (row.def->type == Zenvra::Settings::SettingType::Integer ||
               row.def->type == Zenvra::Settings::SettingType::Float ||
               row.def->type == Zenvra::Settings::SettingType::String) &&
              row.def->id != "editor.fontFamily" &&
              row.def->id != "workbench.mascot.image") {
            SetCursor(LoadCursor(nullptr, IDC_IBEAM));
            return TRUE;
          }
          if (row.is_interactive_point(cur_x, cur_y)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
          }
        }
      }
      if (!layout.scrollbar_thumb.is_empty() &&
          layout.scrollbar_thumb.contains(cur_x, cur_y)) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
      }
      SetCursor(LoadCursor(nullptr, IDC_ARROW));
      return TRUE;
    }
    break;
  }

  case WM_LBUTTONDOWN: {
    SetFocus(hwnd);
    const float x = static_cast<float>(GET_X_LPARAM(l_param));
    const float y = static_cast<float>(GET_Y_LPARAM(l_param));
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout =
        calculate_layout(static_cast<float>(rc.right - rc.left),
                         static_cast<float>(rc.bottom - rc.top), dpi_scale);

    if (!layout.sidebar_scrollbar_thumb.is_empty() &&
        layout.sidebar_scrollbar_thumb.contains(x, y)) {
      SetCapture(hwnd);
    } else if (!layout.scrollbar_thumb.is_empty() &&
               layout.scrollbar_thumb.contains(x, y)) {
      SetCapture(hwnd);
    } else if (m_font_dropdown_open &&
               !m_font_dropdown_scrollbar_thumb.is_empty() &&
               m_font_dropdown_scrollbar_thumb.contains(x, y)) {
      SetCapture(hwnd);
    }

    if (handle_pointer_press(x, y, layout)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_LBUTTONUP: {
    const float x = static_cast<float>(GET_X_LPARAM(l_param));
    const float y = static_cast<float>(GET_Y_LPARAM(l_param));
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout =
        calculate_layout(static_cast<float>(rc.right - rc.left),
                         static_cast<float>(rc.bottom - rc.top), dpi_scale);

    if (m_is_dragging_scrollbar || m_is_dragging_sidebar_scrollbar ||
        m_is_dragging_font_scrollbar) {
      ReleaseCapture();
      handle_pointer_release(x, y, layout);
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_MOUSEWHEEL: {
    const short delta = GET_WHEEL_DELTA_WPARAM(w_param);
    POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    ScreenToClient(hwnd, &pt);

    RECT rc;
    GetClientRect(hwnd, &rc);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout =
        calculate_layout(static_cast<float>(rc.right - rc.left),
                         static_cast<float>(rc.bottom - rc.top), dpi_scale);

    const float delta_y = static_cast<float>(delta) / WHEEL_DELTA;
    if (handle_scroll(delta_y, layout, static_cast<float>(pt.x),
                      static_cast<float>(pt.y))) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_CHAR: {
    char ch = static_cast<char>(w_param);
    if (!m_editing_setting_id.empty()) {
      auto &service = Zenvra::Settings::SettingsService::instance();
      const auto *def = service.get_schema().get_setting(m_editing_setting_id);
      if (def && def->type == Zenvra::Settings::SettingType::String) {
        if (ch >= 32 && ch != 127) {
          m_editing_text.push_back(ch);
          service.set(m_editing_setting_id, m_editing_text, m_active_scope);
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
      } else if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-') {
        m_editing_text.push_back(ch);
        try {
          if (def) {
            if (def->type == Zenvra::Settings::SettingType::Integer) {
              service.set(m_editing_setting_id, std::stoll(m_editing_text),
                          m_active_scope);
            } else if (def->type == Zenvra::Settings::SettingType::Float) {
              service.set(m_editing_setting_id, std::stod(m_editing_text),
                          m_active_scope);
            }
          }
        } catch (...) {
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
    }

    if (ch >= 32 && ch != 127) {
      m_font_dropdown_open = false;
      std::string s(1, ch);
      if (m_search_input.handle_text_input(s)) {
        m_scroll_offset = 0.0F;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
    }
    return 0;
  }

  case WM_KEYDOWN: {
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;

    if (w_param == VK_ESCAPE) {
      if (m_font_dropdown_open) {
        m_font_dropdown_open = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (!m_editing_setting_id.empty()) {
        m_editing_setting_id.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      handle_escape();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    if (w_param == VK_RETURN) {
      if (m_font_dropdown_open) {
        m_font_dropdown_open = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (!m_editing_setting_id.empty()) {
        m_editing_setting_id.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) && (w_param == 'V' || w_param == 'v')) {
      if (!m_editing_setting_id.empty()) {
        if (OpenClipboard(hwnd)) {
          HANDLE h_data = GetClipboardData(CF_UNICODETEXT);
          if (h_data) {
            const wchar_t *psz_text = static_cast<const wchar_t *>(GlobalLock(h_data));
            if (psz_text) {
              const std::string clip_utf8 = Utility::wide_to_utf8(psz_text).value_or("");
              GlobalUnlock(h_data);
              auto &service = Zenvra::Settings::SettingsService::instance();
              const auto *def = service.get_schema().get_setting(m_editing_setting_id);
              if (def && def->type == Zenvra::Settings::SettingType::String) {
                m_editing_text += clip_utf8;
                service.set(m_editing_setting_id, m_editing_text, m_active_scope);
              }
            }
          }
          CloseClipboard();
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
      }
    }

    if (w_param == VK_BACK) {
      if (!m_editing_setting_id.empty()) {
        auto &service = Zenvra::Settings::SettingsService::instance();
        const auto *def =
            service.get_schema().get_setting(m_editing_setting_id);
        if (!m_editing_text.empty()) {
          m_editing_text.pop_back();
          try {
            if (def) {
              if (def->type == Zenvra::Settings::SettingType::Integer) {
                if (!m_editing_text.empty() && m_editing_text != "-") {
                  service.set(m_editing_setting_id, std::stoll(m_editing_text),
                              m_active_scope);
                }
              } else if (def->type == Zenvra::Settings::SettingType::Float) {
                if (!m_editing_text.empty() && m_editing_text != "-") {
                  service.set(m_editing_setting_id, std::stod(m_editing_text),
                              m_active_scope);
                }
              } else if (def->type == Zenvra::Settings::SettingType::String) {
                service.set(m_editing_setting_id, m_editing_text,
                            m_active_scope);
              }
            }
          } catch (...) {
          }
        } else {
          if (def && def->type == Zenvra::Settings::SettingType::String) {
            service.set(m_editing_setting_id, std::string{}, m_active_scope);
          }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }

      handle_backspace();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    if (w_param == VK_UP) {
      if (m_font_dropdown_open && m_font_dropdown_max_scroll > 0.0F) {
        m_font_dropdown_scroll =
            std::max(0.0F, m_font_dropdown_scroll - 38.0F * dpi_scale);
      } else {
        m_scroll_offset = std::max(0.0F, m_scroll_offset - 44.0F * dpi_scale);
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    if (w_param == VK_DOWN) {
      if (m_font_dropdown_open && m_font_dropdown_max_scroll > 0.0F) {
        m_font_dropdown_scroll =
            std::min(m_font_dropdown_max_scroll,
                     m_font_dropdown_scroll + 38.0F * dpi_scale);
      } else {
        m_scroll_offset =
            std::min(m_max_scroll, m_scroll_offset + 44.0F * dpi_scale);
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    if (w_param == VK_PRIOR) {
      m_scroll_offset = std::max(0.0F, m_scroll_offset - 200.0F * dpi_scale);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    if (w_param == VK_NEXT) {
      m_scroll_offset =
          std::min(m_max_scroll, m_scroll_offset + 200.0F * dpi_scale);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    return 0;
  }

  case WM_CLOSE:
    close();
    return 0;

  case WM_DESTROY:
    KillTimer(hwnd, 1);
    m_hwnd = nullptr;
    if (m_parent_hwnd != nullptr && IsWindow(m_parent_hwnd)) {
      SetForegroundWindow(m_parent_hwnd);
      SetFocus(m_parent_hwnd);
      SetActiveWindow(m_parent_hwnd);
      InvalidateRect(m_parent_hwnd, nullptr, FALSE);
    }
    return 0;

  default:
    break;
  }

  return DefWindowProcW(hwnd, message, w_param, l_param);
}

namespace {

struct CodingFontOption {
  std::string primary_name;
  std::string name;
  std::string tag;
};

static std::string get_primary_font_name(std::string_view font_spec) {
  size_t comma = font_spec.find(',');
  std::string_view primary = (comma != std::string_view::npos)
                                 ? font_spec.substr(0, comma)
                                 : font_spec;
  size_t start = primary.find_first_not_of(" \t\r\n'\"");
  size_t end = primary.find_last_not_of(" \t\r\n'\"");
  if (start != std::string_view::npos && end != std::string_view::npos &&
      end >= start) {
    return std::string(primary.substr(start, end - start + 1));
  }
  return std::string(primary);
}

static int CALLBACK EnumFontFamCheckProc(const LOGFONTW * /*lpelfe*/,
                                         const TEXTMETRICW * /*lpntme*/,
                                         DWORD /*FontType*/, LPARAM lParam) {
  *reinterpret_cast<bool *>(lParam) = true;
  return 0; // stop enumeration on first match
}

static bool is_font_installed_on_os(const std::string &font_name) {
  if (font_name.empty())
    return false;
  HDC dc = GetDC(nullptr);
  if (!dc)
    return false;
  LOGFONTW lf{};
  int wlen = MultiByteToWideChar(CP_UTF8, 0, font_name.c_str(),
                                 static_cast<int>(font_name.length()),
                                 lf.lfFaceName, 31);
  lf.lfFaceName[wlen] = L'\0';
  lf.lfCharSet = DEFAULT_CHARSET;
  bool found = false;
  EnumFontFamiliesExW(dc, &lf, EnumFontFamCheckProc,
                      reinterpret_cast<LPARAM>(&found), 0);
  ReleaseDC(nullptr, dc);
  return found;
}

static int CALLBACK EnumFixedPitchFontsProc(const LOGFONTW *lpelfe,
                                            const TEXTMETRICW *lpntme,
                                            DWORD /*FontType*/, LPARAM lParam) {
  if (!lpelfe || lpelfe->lfFaceName[0] == L'@')
    return 1;
  auto *list = reinterpret_cast<std::vector<std::string> *>(lParam);
  const bool is_fixed =
      (lpntme && !(lpntme->tmPitchAndFamily & TMPF_FIXED_PITCH)) ||
      ((lpelfe->lfPitchAndFamily & 0x03) == FIXED_PITCH);
  if (is_fixed) {
    std::string utf8_name =
        Utility::wide_to_utf8(lpelfe->lfFaceName).value_or("");
    if (!utf8_name.empty()) {
      if (std::find(list->begin(), list->end(), utf8_name) == list->end()) {
        list->push_back(utf8_name);
      }
    }
  }
  return 1;
}

static const std::vector<CodingFontOption> &get_installed_coding_fonts() {
  static std::vector<CodingFontOption> installed_fonts;
  if (!installed_fonts.empty()) {
    return installed_fonts;
  }

  struct Candidate {
    std::string primary;
    std::string spec;
    std::string tag;
  };

  static const Candidate candidates[] = {
      {"Cascadia Code", "Cascadia Code, Consolas, monospace",
       "Modern Windows Terminal"},
      {"Cascadia Mono", "Cascadia Mono, Consolas, monospace",
       "Windows Terminal Monospace"},
      {"Consolas", "Consolas, 'Courier New', monospace",
       "VS Code Default (Windows)"},
      {"JetBrains Mono", "JetBrains Mono, monospace",
       "JetBrains Developer Font"},
      {"Fira Code", "Fira Code, Consolas, monospace", "Programming Ligatures"},
      {"Source Code Pro", "Source Code Pro, monospace", "Adobe Monospace"},
      {"Hack", "Hack, monospace", "Clean Bitstream Monospace"},
      {"Iosevka", "Iosevka, monospace", "Compact Programming Font"},
      {"MesloLGS NF", "'MesloLGS NF', monospace", "Powerlevel10k / Nerd Font"},
      {"DejaVu Sans Mono", "'DejaVu Sans Mono', monospace",
       "Open Source Monospace"},
      {"Lucida Console", "'Lucida Console', monospace",
       "Classic Windows Console"},
      {"Courier New", "'Courier New', monospace",
       "Standard Monospace Fallback"}};

  std::vector<std::string> added_names;

  for (const auto &cand : candidates) {
    bool available = is_font_installed_on_os(cand.primary);
    if (!available && cand.primary == "JetBrains Mono") {
      available = is_font_installed_on_os("JetBrainsMono Nerd Font") ||
                  is_font_installed_on_os("JetBrainsMonoNL Nerd Font") ||
                  is_font_installed_on_os("JetBrainsMono");
    }
    if (available) {
      installed_fonts.push_back({cand.primary, cand.spec, cand.tag});
      added_names.push_back(cand.primary);
    }
  }

  // Discover any other installed fixed-pitch monospace fonts from the OS
  HDC dc = GetDC(nullptr);
  if (dc) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    std::vector<std::string> discovered;
    EnumFontFamiliesExW(dc, &lf, EnumFixedPitchFontsProc,
                        reinterpret_cast<LPARAM>(&discovered), 0);
    ReleaseDC(nullptr, dc);

    for (const auto &name : discovered) {
      if (std::find(added_names.begin(), added_names.end(), name) ==
          added_names.end()) {
        installed_fonts.push_back(
            {name, "'" + name + "', monospace", "Installed System Monospace"});
        added_names.push_back(name);
      }
    }
  }

  if (installed_fonts.empty()) {
    installed_fonts.push_back({"Consolas", "Consolas, 'Courier New', monospace",
                               "VS Code Default (Windows)"});
  }

  return installed_fonts;
}

struct SettingsSectionDef {
  std::string id;
  std::string title;
  std::vector<std::string> setting_ids;
};

std::vector<SettingsSectionDef> get_all_sections() {
  std::vector<SettingsSectionDef> sections;

  sections.push_back(
      {"Commonly Used",
       "Commonly Used",
       {"editor.fontSize", "editor.fontFamily", "workbench.app.title",
        "workbench.mascot.image", "workbench.mascot.renderMode", "editor.tabSize", "editor.renderWhitespace",
        "editor.cursorStyle", "editor.wordWrap", "editor.lineHeight",
        "editor.minimap.enabled", "theme.current", "workbench.sidebar.position",
        "terminal.fontSize"}});

  sections.push_back(
      {"Text Editor",
       "Text Editor",
       {"editor.fontSize", "editor.fontFamily", "editor.tabSize",
        "editor.lineHeight", "editor.cursorStyle", "editor.wordWrap",
        "editor.renderWhitespace", "editor.minimap.enabled"}});

  sections.push_back(
      {"Workbench",
       "Workbench",
       {"workbench.app.title", "workbench.mascot.image", "workbench.mascot.renderMode", "theme.current",
        "ui.fontSize", "ui.fontFamily", "ui.scale", "workbench.sidebar.position",
        "workbench.panel.position", "workbench.activityBar.visible"}});

  sections.push_back(
      {"Window", "Window", {"window.zoomLevel", "window.fullscreen"}});

  sections.push_back(
      {"Features",
       "Features",
       {"terminal.fontSize", "terminal.fontFamily", "shader.preview.enabled",
        "shader.preview.backend", "shader.preview.fpsLimit",
        "shader.preview.vsync", "shader.preview.autoReload",
        "browser.defaultZoom", "browser.devtools.enabled"}});

  sections.push_back(
      {"Application",
       "Application",
       {"application.update.mode", "application.telemetry.enabled",
        "application.proxy.support"}});

  sections.push_back(
      {"Security", "Security", {"security.workspace.trust.enabled"}});

  sections.push_back({"Extensions", "Extensions", {}});

  return sections;
}

} // namespace

SettingsWindowLayoutResult
SettingsWindow::calculate_layout(float width, float height,
                                 float dpi_scale) const noexcept {
  SettingsWindowLayoutResult layout{};
  layout.overlay_bounds = Rect{0.0F, 0.0F, width, height};
  layout.dialog_bounds = Rect{0.0F, 0.0F, width, height};

  // 1. Header (Minimal Titlebar)
  const float header_h = 36.0F * dpi_scale;
  layout.header_bounds = Rect{0.0F, 0.0F, width, header_h};
  layout.title_bounds =
      Rect{36.0F * dpi_scale, 0.0F, 100.0F * dpi_scale, header_h};

  const float close_w = 44.0F * dpi_scale;
  layout.close_btn_bounds = Rect{width - close_w, 0.0F, close_w, header_h};

  // 2. Search bar
  const float pad_x = 20.0F * dpi_scale;
  const float search_h = 30.0F * dpi_scale;
  layout.search_bar_bounds =
      Rect{pad_x, layout.header_bounds.bottom() + 6.0F * dpi_scale,
           width - pad_x * 2.0F, search_h};
  m_search_input.set_bounds(layout.search_bar_bounds);

  if (!m_search_input.get_text().empty()) {
    const float clear_size = 18.0F * dpi_scale;
    layout.search_clear_btn_bounds =
        Rect{layout.search_bar_bounds.right() - clear_size - 8.0F * dpi_scale,
             layout.search_bar_bounds.y + (search_h - clear_size) * 0.5F,
             clear_size, clear_size};
  }

  // 3. Scope Tabs (Directly below Search Bar: User | Workspace | Antigravity
  // IDE Settings)
  const float tabs_y = layout.search_bar_bounds.bottom() + 8.0F * dpi_scale;
  const float tabs_h = 26.0F * dpi_scale;
  layout.tabs_bar_bounds = Rect{pad_x, tabs_y, width - pad_x * 2.0F, tabs_h};

  layout.user_tab_bounds =
      Rect{pad_x, tabs_y, 38.0F * dpi_scale, tabs_h - 2.0F * dpi_scale};
  layout.workspace_tab_bounds =
      Rect{layout.user_tab_bounds.right() + 18.0F * dpi_scale, tabs_y,
           74.0F * dpi_scale, tabs_h - 2.0F * dpi_scale};

  if (m_active_scope == Zenvra::Settings::SettingsScope::User) {
    layout.tab_underline_bounds =
        Rect{layout.user_tab_bounds.x,
             layout.tabs_bar_bounds.bottom() - 2.0F * dpi_scale,
             layout.user_tab_bounds.width, 2.0F * dpi_scale};
  } else if (m_active_scope == Zenvra::Settings::SettingsScope::Workspace) {
    layout.tab_underline_bounds =
        Rect{layout.workspace_tab_bounds.x,
             layout.tabs_bar_bounds.bottom() - 2.0F * dpi_scale,
             layout.workspace_tab_bounds.width, 2.0F * dpi_scale};
  }

  // 3.5. Layout Borders: Header separator & Sidebar divider
  layout.header_separator_bounds =
      Rect{0.0F, layout.tabs_bar_bounds.bottom() - 1.0F * dpi_scale, width,
           1.0F * dpi_scale};

  const float sidebar_divider_x = 210.0F * dpi_scale;
  layout.sidebar_divider_bounds =
      Rect{sidebar_divider_x, layout.tabs_bar_bounds.bottom(), 1.0F * dpi_scale,
           height - layout.tabs_bar_bounds.bottom()};

  // 4. Body layout: Sidebar + Content
  const float body_y = layout.tabs_bar_bounds.bottom() + 6.0F * dpi_scale;
  const float body_h = height - body_y - 8.0F * dpi_scale;

  layout.sidebar_bounds = Rect{0.0F, body_y, sidebar_divider_x, body_h};

  // Category Tree in Sidebar
  struct CatDef {
    std::string id;
    std::string name;
    int depth;
    bool has_children;
    std::vector<std::pair<std::string, std::string>> children;
  };

  const std::vector<CatDef> cat_tree = {
      {"Commonly Used", "Commonly Used", 0, false, {}},
      {"Text Editor",
       "Text Editor",
       0,
       true,
       {{"Editor:Cursor", "Cursor"},
        {"Editor:Find", "Find"},
        {"Editor:Font", "Font"},
        {"Editor:Formatting", "Formatting"},
        {"Editor:DiffEditor", "Diff Editor"},
        {"Editor:MultiFileDiffEditor", "Multi-File Diff Editor"},
        {"Editor:Minimap", "Minimap"},
        {"Editor:Suggestions", "Suggestions"},
        {"Editor:Files", "Files"}}},
      {"Workbench",
       "Workbench",
       0,
       true,
       {{"Workbench:Appearance", "Appearance"},
        {"Workbench:Breadcrumbs", "Breadcrumbs"},
        {"Workbench:EditorManagement", "Editor Management"},
        {"Workbench:SettingsEditor", "Settings Editor"},
        {"Workbench:ZenMode", "Zen Mode"},
        {"Workbench:ScreencastMode", "Screencast Mode"}}},
      {"Window", "Window", 0, true, {{"Window:NewWindow", "New Window"}}},
      {"Features",
       "Features",
       0,
       true,
       {{"Features:AccessibilitySignals", "Accessibility Signals"},
        {"Features:Accessibility", "Accessibility"},
        {"Features:Explorer", "Explorer"},
        {"Features:Search", "Search"},
        {"Features:Debug", "Debug"},
        {"Features:Testing", "Testing"},
        {"Features:SourceControl", "Source Control"},
        {"Features:Extensions", "Extensions"},
        {"Features:Terminal", "Terminal"},
        {"Features:Task", "Task"},
        {"Features:Problems", "Problems"},
        {"Features:Output", "Output"},
        {"Features:Comments", "Comments"},
        {"Features:Remote", "Remote"},
        {"Features:Timeline", "Timeline"},
        {"Features:Notebook", "Notebook"},
        {"Features:MergeEditor", "Merge Editor"}}},
      {"Application",
       "Application",
       0,
       true,
       {{"Application:Proxy", "Proxy"},
        {"Application:Keyboard", "Keyboard"},
        {"Application:Update", "Update"},
        {"Application:Telemetry", "Telemetry"}}},
      {"Security", "Security", 0, true, {{"Security:Workspace", "Workspace"}}},
      {"Extensions", "Extensions", 0, false, {}}};

  const float item_margin_left = 10.0F * dpi_scale;
  const float item_margin_right = 8.0F * dpi_scale;
  const float item_w = sidebar_divider_x - item_margin_left - item_margin_right;
  const float item_h = 24.0F * dpi_scale;

  float total_cat_height = 8.0F * dpi_scale;
  for (const auto &c : cat_tree) {
    total_cat_height += item_h + 2.0F * dpi_scale;
    const bool is_exp =
        m_category_expanded.contains(c.name) && m_category_expanded.at(c.name);
    if (c.has_children && is_exp) {
      total_cat_height +=
          (item_h + 2.0F * dpi_scale) * static_cast<float>(c.children.size());
    }
  }

  m_sidebar_max_scroll =
      std::max(0.0F, total_cat_height - layout.sidebar_bounds.height);
  const float effective_sidebar_scroll =
      std::clamp(m_sidebar_scroll_offset, 0.0F, m_sidebar_max_scroll);
  const_cast<SettingsWindow *>(this)->m_sidebar_scroll_offset =
      effective_sidebar_scroll;

  if (m_sidebar_max_scroll > 0.0F) {
    const float sb_track_w = 4.0F * dpi_scale;
    const float sb_track_x = sidebar_divider_x - sb_track_w - 2.0F * dpi_scale;
    const float sb_track_y = layout.sidebar_bounds.y;
    const float sb_track_h = layout.sidebar_bounds.height;
    layout.sidebar_scrollbar_track =
        Rect{sb_track_x, sb_track_y, sb_track_w, sb_track_h};

    const float min_thumb_h = 24.0F * dpi_scale;
    const float sb_thumb_h =
        std::max(min_thumb_h, (sb_track_h / total_cat_height) * sb_track_h);
    const float sb_travel = sb_track_h - sb_thumb_h;
    const float sb_thumb_y =
        sb_track_y +
        (m_sidebar_max_scroll > 0.0F
             ? (effective_sidebar_scroll / m_sidebar_max_scroll) * sb_travel
             : 0.0F);
    layout.sidebar_scrollbar_thumb =
        Rect{sb_track_x, sb_thumb_y, sb_track_w, sb_thumb_h};
  }

  float cat_y =
      layout.sidebar_bounds.y + 4.0F * dpi_scale - effective_sidebar_scroll;

  for (const auto &c : cat_tree) {
    const bool is_exp =
        m_category_expanded.contains(c.name) && m_category_expanded.at(c.name);
    SettingsCategoryItem item{};
    item.id = c.id;
    item.name = c.name;
    item.depth = c.depth;
    item.has_children = c.has_children;
    item.is_expanded = is_exp;
    item.bounds = Rect{item_margin_left, cat_y, item_w, item_h};
    if (c.has_children) {
      item.chevron_bounds =
          Rect{item.bounds.x + 4.0F * dpi_scale,
               item.bounds.y + (item_h - 14.0F * dpi_scale) * 0.5F,
               14.0F * dpi_scale, 14.0F * dpi_scale};
    }
    layout.category_items.push_back(item);
    cat_y += item_h + 2.0F * dpi_scale;

    if (c.has_children && is_exp) {
      for (const auto &[child_id, child_name] : c.children) {
        SettingsCategoryItem sub{};
        sub.id = child_id;
        sub.name = child_name;
        sub.depth = 1;
        sub.has_children = false;
        sub.is_expanded = false;
        sub.bounds = Rect{item_margin_left, cat_y, item_w, item_h};
        layout.category_items.push_back(sub);
        cat_y += item_h + 2.0F * dpi_scale;
      }
    }
  }

  // 5. Content Area
  const float content_x = sidebar_divider_x + 24.0F * dpi_scale;
  const float content_w = width - content_x - 16.0F * dpi_scale;
  layout.content_bounds = Rect{
      content_x, layout.tabs_bar_bounds.bottom() + 8.0F * dpi_scale, content_w,
      height - (layout.tabs_bar_bounds.bottom() + 8.0F * dpi_scale) -
          8.0F * dpi_scale};

  auto &service = Zenvra::Settings::SettingsService::instance();
  const auto &schema = service.get_schema();
  const std::string query = m_search_input.get_text();

  const float gutter_w = 26.0F * dpi_scale; // space for gear icon
  const float max_row_card_w = 680.0F * dpi_scale;
  const float avail_w =
      std::max(100.0F * dpi_scale,
               layout.content_bounds.width - gutter_w - 20.0F * dpi_scale);
  const float row_card_w = std::min(avail_w, max_row_card_w);
  const float row_h = 88.0F * dpi_scale;
  const float row_spacing = 14.0F * dpi_scale;
  const float sec_hdr_h = 34.0F * dpi_scale;

  float current_unscrolled_y = layout.content_bounds.y;

  if (!query.empty()) {
    // Search Results Section
    SettingsSectionHeader hdr{};
    hdr.category_id = "search";
    hdr.title = "Search results for \"" + query + "\"";
    hdr.content_y = current_unscrolled_y - layout.content_bounds.y;
    hdr.bounds =
        Rect{content_x + gutter_w, current_unscrolled_y - m_scroll_offset,
             row_card_w, sec_hdr_h};
    layout.section_headers.push_back(hdr);
    current_unscrolled_y += sec_hdr_h + 12.0F * dpi_scale;

    const auto matches = schema.search(query);
    for (const auto *def : matches) {
      SettingRowLayout row{};
      row.def = def;
      const float this_row_h = (def->id == "workbench.mascot.image")
                                   ? (118.0F * dpi_scale)
                                   : row_h;
      row.bounds = Rect{content_x, current_unscrolled_y - m_scroll_offset,
                        gutter_w + row_card_w, this_row_h};
      row.focus_box_bounds =
          Rect{content_x + gutter_w, current_unscrolled_y - m_scroll_offset,
               row_card_w, this_row_h};
      row.gear_btn_bounds = Rect{content_x + 4.0F * dpi_scale,
                                 row.focus_box_bounds.y + 10.0F * dpi_scale,
                                 18.0F * dpi_scale, 18.0F * dpi_scale};
      row.is_focused = (def->id == m_focused_setting_id);
      row.is_modified = service.is_modified(def->id, m_active_scope);

      const float pad_inner = 14.0F * dpi_scale;
      row.label_bounds = Rect{row.focus_box_bounds.x + pad_inner,
                              row.focus_box_bounds.y + 10.0F * dpi_scale,
                              row.focus_box_bounds.width - pad_inner * 2.0F,
                              20.0F * dpi_scale};
      row.description_bounds = Rect{
          row.focus_box_bounds.x + pad_inner,
          row.label_bounds.bottom() + 3.0F * dpi_scale,
          row.focus_box_bounds.width - pad_inner * 2.0F, 18.0F * dpi_scale};
      const float ctrl_y = row.description_bounds.bottom() + 8.0F * dpi_scale;

      if (def->id == "workbench.mascot.image") {
        const float thumb_sz = 56.0F * dpi_scale;
        row.thumbnail_bounds = Rect{row.focus_box_bounds.x + pad_inner, ctrl_y,
                                    thumb_sz, thumb_sz};
        const float slot_x = row.thumbnail_bounds.right() + 12.0F * dpi_scale;
        const float slot_max_w =
            std::max(120.0F * dpi_scale,
                     row.focus_box_bounds.right() - pad_inner - slot_x);
        const float path_box_w = std::min(slot_max_w, 360.0F * dpi_scale);
        row.input_bounds = Rect{slot_x, ctrl_y + 2.0F * dpi_scale, path_box_w,
                                24.0F * dpi_scale};

        const float btn_y = ctrl_y + 30.0F * dpi_scale;
        const float btn_h = 24.0F * dpi_scale;
        const float browse_w = 84.0F * dpi_scale;
        row.browse_btn_bounds = Rect{slot_x, btn_y, browse_w, btn_h};

        const float reset_w = 64.0F * dpi_scale;
        row.reset_btn_bounds =
            Rect{row.browse_btn_bounds.right() + 8.0F * dpi_scale, btn_y,
                 reset_w, btn_h};

        row.control_bounds = Rect{
            row.thumbnail_bounds.x, ctrl_y,
            (row.reset_btn_bounds.right() - row.thumbnail_bounds.x),
            thumb_sz};
      } else if (def->id == "workbench.mascot.renderMode") {
        const float sw_def_w = 124.0F * dpi_scale;
        const float sw_asc_w = 96.0F * dpi_scale;
        const float btn_h = 26.0F * dpi_scale;
        row.switch_default_btn_bounds =
            Rect{row.focus_box_bounds.x + pad_inner, ctrl_y, sw_def_w, btn_h};
        row.switch_ascii_btn_bounds =
            Rect{row.switch_default_btn_bounds.right() + 4.0F * dpi_scale, ctrl_y,
                 sw_asc_w, btn_h};
        row.control_bounds = Rect{
            row.switch_default_btn_bounds.x, ctrl_y,
            (row.switch_ascii_btn_bounds.right() - row.switch_default_btn_bounds.x),
            btn_h};
      } else if (def->type == Zenvra::Settings::SettingType::Boolean) {
        row.checkbox_bounds =
            Rect{row.focus_box_bounds.x + pad_inner, ctrl_y + 2.0F * dpi_scale,
                 18.0F * dpi_scale, 18.0F * dpi_scale};
        row.toggle_bounds = row.checkbox_bounds;
        row.control_bounds = row.checkbox_bounds;
      } else if (def->id == "editor.fontFamily") {
        const float font_w = std::min(
            340.0F * dpi_scale, row.focus_box_bounds.width - pad_inner * 2.0F);
        row.input_bounds = Rect{row.focus_box_bounds.x + pad_inner, ctrl_y,
                                font_w, 28.0F * dpi_scale};
        row.control_bounds = row.input_bounds;
        row.dropdown_btn_bounds =
            Rect{row.input_bounds.right() - 28.0F * dpi_scale, ctrl_y,
                 28.0F * dpi_scale, 28.0F * dpi_scale};
        row.option_btn_bounds = row.input_bounds;
      } else if (def->type == Zenvra::Settings::SettingType::Integer ||
                 def->type == Zenvra::Settings::SettingType::Float ||
                 def->type == Zenvra::Settings::SettingType::String) {
        const float num_w =
            (def->type == Zenvra::Settings::SettingType::String)
                ? std::min(340.0F * dpi_scale,
                           row.focus_box_bounds.width - pad_inner * 2.0F)
                : 180.0F * dpi_scale;
        row.input_bounds = Rect{row.focus_box_bounds.x + pad_inner, ctrl_y,
                                num_w, 28.0F * dpi_scale};
        row.control_bounds = row.input_bounds;
        if (def->type == Zenvra::Settings::SettingType::Integer ||
            def->type == Zenvra::Settings::SettingType::Float) {
          row.minus_btn_bounds = Rect{row.input_bounds.x, ctrl_y,
                                      28.0F * dpi_scale, 28.0F * dpi_scale};
          row.value_label_bounds =
              Rect{row.minus_btn_bounds.right(), ctrl_y,
                   num_w - 56.0F * dpi_scale, 28.0F * dpi_scale};
          row.plus_btn_bounds = Rect{row.value_label_bounds.right(), ctrl_y,
                                     28.0F * dpi_scale, 28.0F * dpi_scale};
        }
      } else if (def->type == Zenvra::Settings::SettingType::Enum) {
        const float enum_w = std::min(
            280.0F * dpi_scale, row.focus_box_bounds.width - pad_inner * 2.0F);
        row.option_btn_bounds = Rect{row.focus_box_bounds.x + pad_inner, ctrl_y,
                                     enum_w, 28.0F * dpi_scale};
        row.control_bounds = row.option_btn_bounds;
        row.dropdown_btn_bounds =
            Rect{row.option_btn_bounds.right() - 28.0F * dpi_scale, ctrl_y,
                 28.0F * dpi_scale, 28.0F * dpi_scale};
      }

      layout.rows.push_back(row);
      current_unscrolled_y += this_row_h + row_spacing;
    }
  } else {
    // Continuous Multi-Category Page
    const auto all_secs = get_all_sections();
    for (const auto &sec : all_secs) {
      SettingsSectionHeader hdr{};
      hdr.category_id = sec.id;
      hdr.title = sec.title;
      hdr.content_y = current_unscrolled_y - layout.content_bounds.y;
      hdr.bounds =
          Rect{content_x + gutter_w, current_unscrolled_y - m_scroll_offset,
               row_card_w, sec_hdr_h};
      layout.section_headers.push_back(hdr);
      current_unscrolled_y += sec_hdr_h + 12.0F * dpi_scale;

      for (const auto &id : sec.setting_ids) {
        const auto *def = schema.get_setting(id);
        if (!def)
          continue;

        SettingRowLayout row{};
        row.def = def;
        const float this_row_h = (def->id == "workbench.mascot.image")
                                     ? (118.0F * dpi_scale)
                                     : row_h;
        row.bounds = Rect{content_x, current_unscrolled_y - m_scroll_offset,
                          gutter_w + row_card_w, this_row_h};
        row.focus_box_bounds =
            Rect{content_x + gutter_w, current_unscrolled_y - m_scroll_offset,
                 row_card_w, this_row_h};
        row.gear_btn_bounds = Rect{content_x + 4.0F * dpi_scale,
                                   row.focus_box_bounds.y + 10.0F * dpi_scale,
                                   18.0F * dpi_scale, 18.0F * dpi_scale};
        row.is_focused = (def->id == m_focused_setting_id);
        row.is_modified = service.is_modified(def->id, m_active_scope);

        const float pad_inner = 14.0F * dpi_scale;
        row.label_bounds = Rect{row.focus_box_bounds.x + pad_inner,
                                row.focus_box_bounds.y + 10.0F * dpi_scale,
                                row.focus_box_bounds.width - pad_inner * 2.0F,
                                20.0F * dpi_scale};
        row.description_bounds = Rect{
            row.focus_box_bounds.x + pad_inner,
            row.label_bounds.bottom() + 3.0F * dpi_scale,
            row.focus_box_bounds.width - pad_inner * 2.0F, 18.0F * dpi_scale};
        const float ctrl_y = row.description_bounds.bottom() + 8.0F * dpi_scale;

        if (def->id == "workbench.mascot.image") {
          const float thumb_sz = 56.0F * dpi_scale;
          row.thumbnail_bounds = Rect{row.focus_box_bounds.x + pad_inner,
                                      ctrl_y, thumb_sz, thumb_sz};
          const float slot_x = row.thumbnail_bounds.right() + 12.0F * dpi_scale;
          const float slot_max_w =
              std::max(120.0F * dpi_scale,
                       row.focus_box_bounds.right() - pad_inner - slot_x);
          const float path_box_w = std::min(slot_max_w, 360.0F * dpi_scale);
          row.input_bounds = Rect{slot_x, ctrl_y + 2.0F * dpi_scale, path_box_w,
                                  24.0F * dpi_scale};

          const float btn_y = ctrl_y + 30.0F * dpi_scale;
          const float btn_h = 24.0F * dpi_scale;
          const float browse_w = 84.0F * dpi_scale;
          row.browse_btn_bounds = Rect{slot_x, btn_y, browse_w, btn_h};

          const float reset_w = 64.0F * dpi_scale;
          row.reset_btn_bounds =
              Rect{row.browse_btn_bounds.right() + 8.0F * dpi_scale, btn_y,
                   reset_w, btn_h};

          row.control_bounds = Rect{
              row.thumbnail_bounds.x, ctrl_y,
              (row.reset_btn_bounds.right() - row.thumbnail_bounds.x),
              thumb_sz};
        } else if (def->id == "workbench.mascot.renderMode") {
          const float sw_def_w = 124.0F * dpi_scale;
          const float sw_asc_w = 96.0F * dpi_scale;
          const float btn_h = 26.0F * dpi_scale;
          row.switch_default_btn_bounds =
              Rect{row.focus_box_bounds.x + pad_inner, ctrl_y, sw_def_w, btn_h};
          row.switch_ascii_btn_bounds =
              Rect{row.switch_default_btn_bounds.right() + 4.0F * dpi_scale, ctrl_y,
                   sw_asc_w, btn_h};
          row.control_bounds = Rect{
              row.switch_default_btn_bounds.x, ctrl_y,
              (row.switch_ascii_btn_bounds.right() - row.switch_default_btn_bounds.x),
              btn_h};
        } else if (def->type == Zenvra::Settings::SettingType::Boolean) {
          row.checkbox_bounds = Rect{row.focus_box_bounds.x + pad_inner,
                                     ctrl_y + 2.0F * dpi_scale,
                                     18.0F * dpi_scale, 18.0F * dpi_scale};
          row.toggle_bounds = row.checkbox_bounds;
          row.control_bounds = row.checkbox_bounds;
        } else if (def->id == "editor.fontFamily") {
          const float font_w =
              std::min(340.0F * dpi_scale,
                       row.focus_box_bounds.width - pad_inner * 2.0F);
          row.input_bounds = Rect{row.focus_box_bounds.x + pad_inner, ctrl_y,
                                  font_w, 28.0F * dpi_scale};
          row.control_bounds = row.input_bounds;
          row.dropdown_btn_bounds =
              Rect{row.input_bounds.right() - 28.0F * dpi_scale, ctrl_y,
                   28.0F * dpi_scale, 28.0F * dpi_scale};
          row.option_btn_bounds = row.input_bounds;
        } else if (def->type == Zenvra::Settings::SettingType::Integer ||
                   def->type == Zenvra::Settings::SettingType::Float ||
                   def->type == Zenvra::Settings::SettingType::String) {
          const float num_w =
              (def->type == Zenvra::Settings::SettingType::String)
                  ? std::min(340.0F * dpi_scale,
                             row.focus_box_bounds.width - pad_inner * 2.0F)
                  : 180.0F * dpi_scale;
          row.input_bounds = Rect{row.focus_box_bounds.x + pad_inner, ctrl_y,
                                  num_w, 28.0F * dpi_scale};
          row.control_bounds = row.input_bounds;
          if (def->type == Zenvra::Settings::SettingType::Integer ||
              def->type == Zenvra::Settings::SettingType::Float) {
            row.minus_btn_bounds = Rect{row.input_bounds.x, ctrl_y,
                                        28.0F * dpi_scale, 28.0F * dpi_scale};
            row.value_label_bounds =
                Rect{row.minus_btn_bounds.right(), ctrl_y,
                     num_w - 56.0F * dpi_scale, 28.0F * dpi_scale};
            row.plus_btn_bounds = Rect{row.value_label_bounds.right(), ctrl_y,
                                       28.0F * dpi_scale, 28.0F * dpi_scale};
          }
        } else if (def->type == Zenvra::Settings::SettingType::Enum) {
          const float enum_w =
              std::min(280.0F * dpi_scale,
                       row.focus_box_bounds.width - pad_inner * 2.0F);
          row.option_btn_bounds = Rect{row.focus_box_bounds.x + pad_inner,
                                       ctrl_y, enum_w, 28.0F * dpi_scale};
          row.control_bounds = row.option_btn_bounds;
          row.dropdown_btn_bounds =
              Rect{row.option_btn_bounds.right() - 28.0F * dpi_scale, ctrl_y,
                   28.0F * dpi_scale, 28.0F * dpi_scale};
        }

        layout.rows.push_back(row);
        current_unscrolled_y += this_row_h + row_spacing;
      }

      current_unscrolled_y += 20.0F * dpi_scale;
    }
  }

  const float total_content_h = current_unscrolled_y - layout.content_bounds.y;
  m_max_scroll = std::max(0.0F, total_content_h - layout.content_bounds.height);
  const_cast<SettingsWindow *>(this)->m_scroll_offset =
      std::clamp(m_scroll_offset, 0.0F, m_max_scroll);

  // Font breakdown dropdown layout
  if (m_font_dropdown_open) {
    for (const auto &row : layout.rows) {
      if (row.def && row.def->id == "editor.fontFamily") {
        const auto &coding_fonts = get_installed_coding_fonts();
        const float dd_w = row.input_bounds.width;
        const float item_h = 38.0F * dpi_scale;
        const float total_items_h =
            static_cast<float>(coding_fonts.size()) * item_h + 8.0F * dpi_scale;
        // Clamp dropdown height so it shows ~5.5 items comfortably with a
        // scrollbar
        const float max_dd_h = std::min(total_items_h, 228.0F * dpi_scale);

        // Available space below and above the font family input box
        const float space_below =
            (layout.dialog_bounds.bottom() - 10.0F * dpi_scale) -
            (row.input_bounds.bottom() + 2.0F * dpi_scale);
        const float space_above = (row.input_bounds.y - 2.0F * dpi_scale) -
                                  (layout.dialog_bounds.y + 40.0F * dpi_scale);

        float dd_y = row.input_bounds.bottom() + 2.0F * dpi_scale;
        float actual_dd_h = max_dd_h;

        if (space_below < max_dd_h && space_above > space_below) {
          actual_dd_h = std::min(max_dd_h, space_above);
          dd_y = row.input_bounds.y - 2.0F * dpi_scale - actual_dd_h;
        } else if (space_below < max_dd_h) {
          actual_dd_h = std::max(120.0F * dpi_scale, space_below);
        }

        m_font_dropdown_bounds =
            Rect{row.input_bounds.x, dd_y, dd_w, actual_dd_h};

        m_font_dropdown_max_scroll =
            std::max(0.0F, total_items_h - actual_dd_h);
        const_cast<SettingsWindow *>(this)->m_font_dropdown_scroll = std::clamp(
            m_font_dropdown_scroll, 0.0F, m_font_dropdown_max_scroll);

        const float item_w = (m_font_dropdown_max_scroll > 0.0F)
                                 ? (dd_w - 18.0F * dpi_scale)
                                 : (dd_w - 8.0F * dpi_scale);

        m_font_dropdown_items.clear();
        float opt_y = m_font_dropdown_bounds.y + 4.0F * dpi_scale -
                      m_font_dropdown_scroll;
        for (const auto &opt : coding_fonts) {
          m_font_dropdown_items.push_back(
              {opt.name, Rect{m_font_dropdown_bounds.x + 4.0F * dpi_scale,
                              opt_y, item_w, item_h}});
          opt_y += item_h;
        }

        // Scrollbar for font dropdown
        if (m_font_dropdown_max_scroll > 0.0F) {
          const float s_track_w = 6.0F * dpi_scale;
          m_font_dropdown_scrollbar_track = Rect{
              m_font_dropdown_bounds.right() - s_track_w - 4.0F * dpi_scale,
              m_font_dropdown_bounds.y + 4.0F * dpi_scale, s_track_w,
              m_font_dropdown_bounds.height - 8.0F * dpi_scale};
          const float view_ratio =
              m_font_dropdown_bounds.height / total_items_h;
          const float thumb_h =
              std::max(22.0F * dpi_scale,
                       m_font_dropdown_scrollbar_track.height * view_ratio);
          const float travel = m_font_dropdown_scrollbar_track.height - thumb_h;
          const float thumb_y =
              m_font_dropdown_scrollbar_track.y +
              (m_font_dropdown_scroll / m_font_dropdown_max_scroll) * travel;
          m_font_dropdown_scrollbar_thumb = Rect{
              m_font_dropdown_scrollbar_track.x, thumb_y, s_track_w, thumb_h};
        } else {
          m_font_dropdown_scrollbar_track = Rect{};
          m_font_dropdown_scrollbar_thumb = Rect{};
        }
        break;
      }
    }
  }

  // Scrollbar layout
  const float scroll_track_w = 6.0F * dpi_scale;
  layout.scrollbar_track = Rect{
      layout.dialog_bounds.right() - scroll_track_w - 6.0F * dpi_scale,
      layout.content_bounds.y, scroll_track_w, layout.content_bounds.height};

  if (m_max_scroll > 0.0F && total_content_h > 0.0F) {
    const float view_ratio = layout.content_bounds.height / total_content_h;
    const float thumb_h =
        std::max(24.0F * dpi_scale, layout.content_bounds.height * view_ratio);
    const float travel = layout.scrollbar_track.height - thumb_h;
    const float thumb_y =
        layout.scrollbar_track.y + (m_scroll_offset / m_max_scroll) * travel;

    layout.scrollbar_thumb =
        Rect{layout.scrollbar_track.x, thumb_y, scroll_track_w, thumb_h};
  }

  layout.close_hovered = m_close_hovered;
  layout.user_tab_hovered = m_user_tab_hovered;
  layout.workspace_tab_hovered = m_workspace_tab_hovered;
  layout.hovered_category = m_hovered_category;
  layout.scrollbar_thumb_hovered = m_scrollbar_thumb_hovered;
  layout.sidebar_scrollbar_thumb_hovered = m_sidebar_scrollbar_thumb_hovered;
  layout.search_clear_hovered = m_search_clear_hovered;

  return layout;
}

SettingsWindowLayoutResult
SettingsWindow::calculate_layout(const Rect &viewport_bounds,
                                 float dpi_scale) const noexcept {
  return calculate_layout(viewport_bounds.width, viewport_bounds.height,
                          dpi_scale);
}

bool SettingsWindow::is_interactive_point(
    float x, float y, const SettingsWindowLayoutResult &layout) const noexcept {
  if (m_font_dropdown_open && m_font_dropdown_bounds.contains(x, y))
    return true;
  if (layout.close_btn_bounds.contains(x, y))
    return true;
  if (layout.user_tab_bounds.contains(x, y))
    return true;
  if (layout.workspace_tab_bounds.contains(x, y))
    return true;
  if (layout.search_clear_btn_bounds.contains(x, y))
    return true;
  if (layout.search_bar_bounds.contains(x, y))
    return true;

  if (!layout.sidebar_scrollbar_thumb.is_empty() &&
      layout.sidebar_scrollbar_thumb.contains(x, y)) {
    return true;
  }

  if (layout.sidebar_bounds.contains(x, y)) {
    for (const auto &item : layout.category_items) {
      if (item.bounds.contains(x, y))
        return true;
    }
  }

  if (!layout.scrollbar_thumb.is_empty() &&
      layout.scrollbar_thumb.contains(x, y)) {
    return true;
  }

  for (const auto &row : layout.rows) {
    if (layout.content_bounds.contains(x, y) &&
        row.is_interactive_point(x, y)) {
      return true;
    }
  }

  return false;
}

bool SettingsWindow::handle_pointer_press(
    float x, float y, const SettingsWindowLayoutResult &layout) noexcept {
  // 1. Font dropdown interaction
  if (m_font_dropdown_open) {
    if (m_font_dropdown_bounds.contains(x, y)) {
      // Check if clicking on font dropdown scrollbar
      if (!m_font_dropdown_scrollbar_thumb.is_empty() &&
          m_font_dropdown_scrollbar_thumb.contains(x, y)) {
        m_is_dragging_font_scrollbar = true;
        m_drag_start_font_y = y;
        m_drag_start_font_scroll = m_font_dropdown_scroll;
        if (m_hwnd)
          SetCapture(m_hwnd);
        return true;
      }
      if (!m_font_dropdown_scrollbar_track.is_empty() &&
          m_font_dropdown_scrollbar_track.contains(x, y)) {
        if (y < m_font_dropdown_scrollbar_thumb.y) {
          m_font_dropdown_scroll =
              std::max(0.0F, m_font_dropdown_scroll - 80.0F);
        } else {
          m_font_dropdown_scroll = std::min(m_font_dropdown_max_scroll,
                                            m_font_dropdown_scroll + 80.0F);
        }
        return true;
      }

      for (const auto &[font_name, item_bounds] : m_font_dropdown_items) {
        if (item_bounds.bottom() > m_font_dropdown_bounds.y &&
            item_bounds.y < m_font_dropdown_bounds.bottom() &&
            item_bounds.contains(x, y)) {
          auto &service = Zenvra::Settings::SettingsService::instance();
          service.set("editor.fontFamily", font_name, m_active_scope);
          m_font_dropdown_open = false;
          return true;
        }
      }
      return true;
    }

    // If clicking on the input box or chevron that toggled it, simply close it
    for (const auto &row : layout.rows) {
      if (row.def && row.def->id == "editor.fontFamily") {
        if (row.input_bounds.contains(x, y) ||
            row.dropdown_btn_bounds.contains(x, y)) {
          m_font_dropdown_open = false;
          return true;
        }
        break;
      }
    }
    m_font_dropdown_open = false;
  }

  // 2. Close button
  if (layout.close_btn_bounds.contains(x, y)) {
    close();
    return true;
  }

  // 3. User scope tab
  if (layout.user_tab_bounds.contains(x, y)) {
    m_active_scope = Zenvra::Settings::SettingsScope::User;
    return true;
  }

  // 4. Workspace scope tab
  if (layout.workspace_tab_bounds.contains(x, y)) {
    m_active_scope = Zenvra::Settings::SettingsScope::Workspace;
    return true;
  }

  // 5. Search clear button
  if (!layout.search_clear_btn_bounds.is_empty() &&
      layout.search_clear_btn_bounds.contains(x, y)) {
    m_search_input.set_text("");
    m_scroll_offset = 0.0F;
    return true;
  }

  // 6. Search bar focus
  if (layout.search_bar_bounds.contains(x, y)) {
    m_editing_setting_id.clear();
    m_search_input.set_focused(true);
    static_cast<void>(m_search_input.handle_pointer_press(x, y));
    return true;
  } else {
    m_search_input.set_focused(false);
  }

  // 6.5. Sidebar scrollbar thumb drag start
  if (!layout.sidebar_scrollbar_thumb.is_empty() &&
      layout.sidebar_scrollbar_thumb.contains(x, y)) {
    m_is_dragging_sidebar_scrollbar = true;
    m_drag_start_sidebar_y = y;
    m_drag_start_sidebar_scroll_offset = m_sidebar_scroll_offset;
    return true;
  }

  // 7. Category click in sidebar -> scroll to category header!
  if (layout.sidebar_bounds.contains(x, y)) {
    for (const auto &item : layout.category_items) {
      if (item.has_children && item.chevron_bounds.contains(x, y)) {
        m_category_expanded[item.name] = !m_category_expanded[item.name];
        return true;
      }
      if (item.bounds.contains(x, y)) {
        if (item.has_children && !m_category_expanded[item.name]) {
          m_category_expanded[item.name] = true;
        }
        m_active_category = item.id;

        // Find matching section header in continuous scroll view
        for (const auto &hdr : layout.section_headers) {
          if (hdr.category_id == item.id || hdr.title == item.name) {
            m_scroll_offset = std::clamp(hdr.content_y, 0.0F, m_max_scroll);
            return true;
          }
        }

        // If subcategory clicked (e.g. Editor:Font), find first row
        for (const auto &row : layout.rows) {
          if (row.def) {
            const std::string sub_key =
                row.def->category + ":" + row.def->subcategory;
            if (item.id == sub_key || item.name == row.def->subcategory) {
              const float row_unscrolled =
                  row.bounds.y + m_scroll_offset - layout.content_bounds.y;
              m_scroll_offset = std::clamp(row_unscrolled, 0.0F, m_max_scroll);
              return true;
            }
          }
        }
        return true;
      }
    }
  }

  // 8. Scrollbar thumb drag start
  if (!layout.scrollbar_thumb.is_empty() &&
      layout.scrollbar_thumb.contains(x, y)) {
    m_is_dragging_scrollbar = true;
    m_drag_start_y = y;
    m_drag_start_scroll_offset = m_scroll_offset;
    return true;
  }

  // 9. Setting row interactions
  if (layout.content_bounds.contains(x, y)) {
    auto &service = Zenvra::Settings::SettingsService::instance();
    for (const auto &row : layout.rows) {
      if (!row.def)
        continue;
      if (row.bounds.contains(x, y)) {
        m_focused_setting_id = row.def->id;

        // Reset / Gear button
        if (row.gear_btn_bounds.contains(x, y) ||
            (row.is_modified && row.reset_btn_bounds.contains(x, y) &&
             row.def->id != "workbench.mascot.image")) {
          service.reset(row.def->id, m_active_scope);
          return true;
        }

        // Mascot Image Asset Slot interactions
        if (row.def->id == "workbench.mascot.image") {
          if (row.browse_btn_bounds.contains(x, y) ||
              row.thumbnail_bounds.contains(x, y) ||
              row.input_bounds.contains(x, y)) {
            browse_mascot_image();
            return true;
          }
          if (row.reset_btn_bounds.contains(x, y)) {
            Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
            Utility::Ascii::AsciiArtConverter::clear_cache();
            service.reset("workbench.mascot.image", m_active_scope);
            if (m_hwnd) {
              InvalidateRect(m_hwnd, nullptr, FALSE);
              UpdateWindow(m_hwnd);
            }
            if (m_parent_hwnd && IsWindow(m_parent_hwnd)) {
              InvalidateRect(m_parent_hwnd, nullptr, FALSE);
              UpdateWindow(m_parent_hwnd);
            }
            return true;
          }
        }

        // Mascot Render Mode interactions
        if (row.def->id == "workbench.mascot.renderMode") {
          if (row.switch_default_btn_bounds.contains(x, y)) {
            Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
            Utility::Ascii::AsciiArtConverter::clear_cache();
            service.set("workbench.mascot.renderMode", std::string("default"),
                        m_active_scope);
            if (m_hwnd) {
              InvalidateRect(m_hwnd, nullptr, FALSE);
              UpdateWindow(m_hwnd);
            }
            if (m_parent_hwnd && IsWindow(m_parent_hwnd)) {
              InvalidateRect(m_parent_hwnd, nullptr, FALSE);
              UpdateWindow(m_parent_hwnd);
            }
            return true;
          }
          if (row.switch_ascii_btn_bounds.contains(x, y)) {
            Utility::Ascii::AsciiMascotRenderer::clear_bitmap_cache();
            Utility::Ascii::AsciiArtConverter::clear_cache();
            service.set("workbench.mascot.renderMode", std::string("ascii"),
                        m_active_scope);
            if (m_hwnd) {
              InvalidateRect(m_hwnd, nullptr, FALSE);
              UpdateWindow(m_hwnd);
            }
            if (m_parent_hwnd && IsWindow(m_parent_hwnd)) {
              InvalidateRect(m_parent_hwnd, nullptr, FALSE);
              UpdateWindow(m_parent_hwnd);
            }
            return true;
          }
        }

        // Font Family Combobox / Dropdown
        if (row.def->id == "editor.fontFamily") {
          if (row.input_bounds.contains(x, y) ||
              row.dropdown_btn_bounds.contains(x, y)) {
            m_font_dropdown_open = !m_font_dropdown_open;
            if (m_font_dropdown_open) {
              m_font_dropdown_scroll = 0.0F;
            }
            return true;
          }
        }

        // Text / Numeric input editing
        if ((row.def->type == Zenvra::Settings::SettingType::Integer ||
             row.def->type == Zenvra::Settings::SettingType::Float ||
             row.def->type == Zenvra::Settings::SettingType::String) &&
            row.def->id != "editor.fontFamily" &&
            row.def->id != "workbench.mascot.image") {
          if (row.input_bounds.contains(x, y)) {
            m_editing_setting_id = row.def->id;
            m_editing_text = service.get(row.def->id).to_display_string();
            m_caret_visible = true;
            return true;
          }
        }

        // Boolean Checkbox
        if (row.def->type == Zenvra::Settings::SettingType::Boolean) {
          if (row.checkbox_bounds.contains(x, y) ||
              row.toggle_bounds.contains(x, y) ||
              row.control_bounds.contains(x, y)) {
            const bool current = service.get<bool>(row.def->id);
            service.set(row.def->id, !current, m_active_scope);
            return true;
          }
        }

        // Enum Option
        if (row.def->type == Zenvra::Settings::SettingType::Enum &&
            !row.def->enum_values.empty()) {
          if (row.option_btn_bounds.contains(x, y)) {
            const std::string current = service.get<std::string>(row.def->id);
            std::size_t curr_idx = 0;
            for (std::size_t i = 0; i < row.def->enum_values.size(); ++i) {
              if (row.def->enum_values[i].value == current) {
                curr_idx = i;
                break;
              }
            }
            const std::size_t next_idx =
                (curr_idx + 1) % row.def->enum_values.size();
            service.set(row.def->id, row.def->enum_values[next_idx].value,
                        m_active_scope);
            return true;
          }
        }

        // Clicked row body outside controls
        m_editing_setting_id.clear();
        return true;
      }
    }
    m_editing_setting_id.clear();
  }

  return true;
}

bool SettingsWindow::handle_pointer_move(
    float x, float y, const SettingsWindowLayoutResult &layout) noexcept {
  bool changed = false;

  if (m_is_dragging_scrollbar && m_max_scroll > 0.0F) {
    const float track_h = layout.scrollbar_track.height;
    const float thumb_h = layout.scrollbar_thumb.height;
    const float travel = track_h - thumb_h;
    if (travel > 0.0F) {
      const float delta_y = y - m_drag_start_y;
      const float delta_scroll = (delta_y / travel) * m_max_scroll;
      m_scroll_offset = std::clamp(m_drag_start_scroll_offset + delta_scroll,
                                   0.0F, m_max_scroll);
      // Scroll spy: update active sidebar category
      if (m_search_input.get_text().empty()) {
        for (const auto &hdr : layout.section_headers) {
          if (hdr.content_y <= m_scroll_offset + 50.0F) {
            m_active_category = hdr.category_id;
          }
        }
      }
      changed = true;
    }
    return true;
  }

  if (m_is_dragging_font_scrollbar && m_font_dropdown_max_scroll > 0.0F) {
    const float track_h = m_font_dropdown_scrollbar_track.height;
    const float thumb_h = m_font_dropdown_scrollbar_thumb.height;
    const float travel = track_h - thumb_h;
    if (travel > 0.0F) {
      const float delta_y = y - m_drag_start_font_y;
      const float delta_scroll =
          (delta_y / travel) * m_font_dropdown_max_scroll;
      m_font_dropdown_scroll =
          std::clamp(m_drag_start_font_scroll + delta_scroll, 0.0F,
                     m_font_dropdown_max_scroll);
      changed = true;
    }
    return true;
  }

  if (m_font_dropdown_open) {
    const bool s_hovered = !m_font_dropdown_scrollbar_thumb.is_empty() &&
                           m_font_dropdown_scrollbar_thumb.contains(x, y);
    if (m_font_scrollbar_thumb_hovered != s_hovered) {
      m_font_scrollbar_thumb_hovered = s_hovered;
      changed = true;
    }

    std::string new_opt;
    if (m_font_dropdown_bounds.contains(x, y) &&
        !m_font_dropdown_scrollbar_track.contains(x, y)) {
      for (const auto &[font_name, bounds] : m_font_dropdown_items) {
        if (bounds.bottom() > m_font_dropdown_bounds.y &&
            bounds.y < m_font_dropdown_bounds.bottom() &&
            bounds.contains(x, y)) {
          new_opt = font_name;
          break;
        }
      }
    }
    if (m_hovered_font_option != new_opt) {
      m_hovered_font_option = std::move(new_opt);
      changed = true;
    }
  }

  const bool close_h = layout.close_btn_bounds.contains(x, y);
  if (m_close_hovered != close_h) {
    m_close_hovered = close_h;
    changed = true;
  }

  const bool user_h = layout.user_tab_bounds.contains(x, y);
  if (m_user_tab_hovered != user_h) {
    m_user_tab_hovered = user_h;
    changed = true;
  }

  const bool ws_h = layout.workspace_tab_bounds.contains(x, y);
  if (m_workspace_tab_hovered != ws_h) {
    m_workspace_tab_hovered = ws_h;
    changed = true;
  }

  const bool clear_h = !layout.search_clear_btn_bounds.is_empty() &&
                       layout.search_clear_btn_bounds.contains(x, y);
  if (m_search_clear_hovered != clear_h) {
    m_search_clear_hovered = clear_h;
    changed = true;
  }

  std::string new_hovered_cat;
  if (layout.sidebar_bounds.contains(x, y)) {
    for (const auto &item : layout.category_items) {
      if (item.bounds.contains(x, y)) {
        new_hovered_cat = item.id;
        break;
      }
    }
  }
  if (m_hovered_category != new_hovered_cat) {
    m_hovered_category = std::move(new_hovered_cat);
    changed = true;
  }

  const bool scroll_h = !layout.scrollbar_thumb.is_empty() &&
                        layout.scrollbar_thumb.contains(x, y);
  if (m_scrollbar_thumb_hovered != scroll_h) {
    m_scrollbar_thumb_hovered = scroll_h;
    changed = true;
  }

  const bool sb_scroll_h = !layout.sidebar_scrollbar_thumb.is_empty() &&
                           layout.sidebar_scrollbar_thumb.contains(x, y);
  if (m_sidebar_scrollbar_thumb_hovered != sb_scroll_h) {
    m_sidebar_scrollbar_thumb_hovered = sb_scroll_h;
    changed = true;
  }

  for (auto &row : const_cast<SettingsWindowLayoutResult &>(layout).rows) {
    if (row.handle_pointer_move(x, y)) {
      changed = true;
    }
  }

  return changed;
}

bool SettingsWindow::handle_pointer_release(
    float, float, const SettingsWindowLayoutResult &) noexcept {
  bool released = false;
  if (m_is_dragging_scrollbar) {
    m_is_dragging_scrollbar = false;
    released = true;
  }
  if (m_is_dragging_sidebar_scrollbar) {
    m_is_dragging_sidebar_scrollbar = false;
    released = true;
  }
  if (m_is_dragging_font_scrollbar) {
    m_is_dragging_font_scrollbar = false;
    released = true;
  }
  return released;
}

bool SettingsWindow::handle_scroll(float delta_y,
                                   const SettingsWindowLayoutResult &layout,
                                   float mouse_x, float mouse_y) noexcept {
  const float step = 44.0F;

  // 0. If mouse is inside font dropdown, scroll font dropdown!
  if (m_font_dropdown_open &&
      m_font_dropdown_bounds.contains(mouse_x, mouse_y)) {
    if (m_font_dropdown_max_scroll > 0.0F) {
      const float new_font_offset =
          std::clamp(m_font_dropdown_scroll - delta_y * step, 0.0F,
                     m_font_dropdown_max_scroll);
      if (std::abs(new_font_offset - m_font_dropdown_scroll) > 0.01F) {
        m_font_dropdown_scroll = new_font_offset;
        return true;
      }
    }
    return true;
  }

  // If mouse is inside sidebar, scroll sidebar!
  const bool is_over_sidebar =
      (mouse_x >= 0.0F && mouse_x <= layout.sidebar_bounds.right()) &&
      (mouse_y < 0.0F || (mouse_y >= layout.sidebar_bounds.y &&
                          mouse_y <= layout.sidebar_bounds.bottom()));
  if (is_over_sidebar) {
    if (m_sidebar_max_scroll > 0.0F) {
      const float new_sidebar_offset = std::clamp(
          m_sidebar_scroll_offset - delta_y * step, 0.0F, m_sidebar_max_scroll);
      if (std::abs(new_sidebar_offset - m_sidebar_scroll_offset) > 0.01F) {
        m_sidebar_scroll_offset = new_sidebar_offset;
        return true;
      }
    }
    return false;
  }

  // Otherwise scroll content area
  if (m_max_scroll <= 0.0F)
    return false;

  const float new_offset =
      std::clamp(m_scroll_offset - delta_y * step, 0.0F, m_max_scroll);
  if (std::abs(new_offset - m_scroll_offset) > 0.01F) {
    m_scroll_offset = new_offset;
    // Scroll-spy: update active category in sidebar based on current scroll
    // position
    if (m_search_input.get_text().empty()) {
      for (const auto &hdr : layout.section_headers) {
        if (hdr.content_y <= m_scroll_offset + 50.0F) {
          m_active_category = hdr.category_id;
        }
      }
    }
    return true;
  }
  return false;
}

bool SettingsWindow::handle_char(char32_t codepoint) noexcept {
  if (codepoint >= 32) {
    std::string utf8_char;
    if (codepoint <= 0x7F) {
      utf8_char.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      utf8_char.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
      utf8_char.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      utf8_char.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
      utf8_char.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      utf8_char.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    static_cast<void>(m_search_input.handle_text_input(utf8_char));
    m_scroll_offset = 0.0F;
    return true;
  }
  return false;
}

bool SettingsWindow::handle_backspace() noexcept {
  const bool res = m_search_input.handle_backspace();
  if (res) {
    m_scroll_offset = 0.0F;
  }
  return res;
}

bool SettingsWindow::handle_escape() noexcept {
  if (!m_search_input.get_text().empty()) {
    m_search_input.set_text("");
    m_scroll_offset = 0.0F;
    return true;
  }
  close();
  return true;
}

void SettingsWindow::render(HDC device_context,
                            const SettingsWindowLayoutResult &layout,
                            const Theme::StudioTheme &theme,
                            float dpi_scale) const {
  HGDIOBJ prev_font = SelectObject(device_context, m_regular_font);

  // 1. Uniform Flat Dark Background matching StudioTheme
  const COLORREF bg_col = to_color_ref(theme.window_background);
  draw_rect_solid(device_context, layout.dialog_bounds, bg_col);

  // 2. Custom Titlebar Header matching StudioTheme
  const COLORREF titlebar_bg = to_color_ref(theme.titlebar_background);
  draw_rect_solid(device_context, layout.header_bounds, titlebar_bg);

  // Titlebar separator (1px crisp border under titlebar)
  HPEN title_sep_pen =
      CreatePen(PS_SOLID, 1, to_color_ref(theme.titlebar_border));
  HGDIOBJ prev_title_sep = SelectObject(device_context, title_sep_pen);
  MoveToEx(device_context, 0,
           static_cast<int>(layout.header_bounds.bottom() - 1), nullptr);
  LineTo(device_context, static_cast<int>(layout.header_bounds.right()),
         static_cast<int>(layout.header_bounds.bottom() - 1));
  SelectObject(device_context, prev_title_sep);
  DeleteObject(title_sep_pen);

  // Gear Icon + "Settings" text
  const int title_icon_size = static_cast<int>(15.0F * dpi_scale);
  const int title_icon_x = static_cast<int>(14.0F * dpi_scale);
  const int title_icon_y =
      static_cast<int>((layout.header_bounds.height - title_icon_size) * 0.5F);
  draw_icon(device_context, "Assets/icons/gear.svg", title_icon_x, title_icon_y,
            title_icon_size, to_color_ref(theme.text_secondary));

  SetBkMode(device_context, TRANSPARENT);
  SetTextColor(device_context, to_color_ref(theme.text_primary));
  SelectObject(device_context, m_semibold_font);
  RECT title_rc = to_native_rect(layout.title_bounds);
  DrawTextW(device_context, L"Settings", -1, &title_rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Close Button ✕
  if (layout.close_hovered) {
    draw_rect_solid(device_context, layout.close_btn_bounds,
                    to_color_ref(theme.close_hover));
  }
  const int close_cx = static_cast<int>(layout.close_btn_bounds.x +
                                        layout.close_btn_bounds.width * 0.5F);
  const int close_cy = static_cast<int>(layout.close_btn_bounds.y +
                                        layout.close_btn_bounds.height * 0.5F);
  const int cross_sz = static_cast<int>(10.0F * dpi_scale);
  HPEN close_pen =
      CreatePen(PS_SOLID, std::max(1, static_cast<int>(dpi_scale)),
                layout.close_hovered ? RGB(255, 255, 255)
                                     : to_color_ref(theme.text_secondary));
  HGDIOBJ prev_close_pen = SelectObject(device_context, close_pen);
  MoveToEx(device_context, close_cx - cross_sz / 2, close_cy - cross_sz / 2,
           nullptr);
  LineTo(device_context, close_cx + cross_sz / 2 + 1,
         close_cy + cross_sz / 2 + 1);
  MoveToEx(device_context, close_cx - cross_sz / 2, close_cy + cross_sz / 2,
           nullptr);
  LineTo(device_context, close_cx + cross_sz / 2 + 1,
         close_cy - cross_sz / 2 - 1);
  SelectObject(device_context, prev_close_pen);
  DeleteObject(close_pen);

  // 3. Search Bar
  const COLORREF search_bg = to_color_ref(theme.command_center_background);
  const COLORREF search_border =
      m_search_input.get_state().focused
          ? to_color_ref(theme.accent)
          : to_color_ref(theme.command_center_border);
  draw_rounded_rect(device_context, layout.search_bar_bounds, search_bg,
                    search_border, 3.0F * dpi_scale);

  // Search Icon inside search bar
  const int search_icon_sz = static_cast<int>(14.0F * dpi_scale);
  const int search_icon_x =
      static_cast<int>(layout.search_bar_bounds.x + 10.0F * dpi_scale);
  const int search_icon_y = static_cast<int>(
      layout.search_bar_bounds.y +
      (layout.search_bar_bounds.height - search_icon_sz) * 0.5F);
  draw_icon(device_context, "Assets/icons/search.svg", search_icon_x,
            search_icon_y, search_icon_sz, to_color_ref(theme.text_secondary));

  // Search Text / Placeholder
  const Rect search_text_bounds{
      layout.search_bar_bounds.x + 32.0F * dpi_scale,
      layout.search_bar_bounds.y,
      layout.search_bar_bounds.width -
          (layout.search_clear_btn_bounds.is_empty() ? 40.0F : 64.0F) *
              dpi_scale,
      layout.search_bar_bounds.height};

  SelectObject(device_context, m_regular_font);
  RECT text_rc = to_native_rect(search_text_bounds);

  if (m_search_input.get_text().empty()) {
    SetTextColor(device_context, to_color_ref(theme.text_secondary));
    const std::wstring ph =
        Utility::utf8_to_wide(m_search_input.get_placeholder()).value_or(L"");
    DrawTextW(device_context, ph.c_str(), -1, &text_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  } else {
    SetTextColor(device_context, to_color_ref(theme.text_primary));
    const std::wstring txt =
        Utility::utf8_to_wide(m_search_input.get_text()).value_or(L"");
    DrawTextW(device_context, txt.c_str(), -1, &text_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Blinking caret
    if (m_search_input.get_state().focused && m_caret_visible) {
      SIZE text_ext{};
      GetTextExtentPoint32W(device_context, txt.c_str(),
                            static_cast<int>(txt.size()), &text_ext);
      const int caret_x = text_rc.left + text_ext.cx + 1;
      const int caret_top =
          text_rc.top +
          static_cast<int>((search_text_bounds.height - 16.0F * dpi_scale) *
                           0.5F);
      const int caret_bottom = caret_top + static_cast<int>(16.0F * dpi_scale);

      HPEN caret_pen = CreatePen(PS_SOLID, 2, to_color_ref(theme.text_primary));
      HGDIOBJ prev_cp = SelectObject(device_context, caret_pen);
      MoveToEx(device_context, caret_x, caret_top, nullptr);
      LineTo(device_context, caret_x, caret_bottom);
      SelectObject(device_context, prev_cp);
      DeleteObject(caret_pen);
    }

    // Search clear button
    if (!layout.search_clear_btn_bounds.is_empty()) {
      if (layout.search_clear_hovered) {
        draw_rounded_rect(device_context, layout.search_clear_btn_bounds,
                          to_color_ref(theme.hover),
                          to_color_ref(theme.command_center_border),
                          3.0F * dpi_scale);
      }
      const int clr_cx =
          static_cast<int>(layout.search_clear_btn_bounds.x +
                           layout.search_clear_btn_bounds.width * 0.5F);
      const int clr_cy =
          static_cast<int>(layout.search_clear_btn_bounds.y +
                           layout.search_clear_btn_bounds.height * 0.5F);
      const int clr_sz = static_cast<int>(8.0F * dpi_scale);
      HPEN clr_pen = CreatePen(PS_SOLID, 1,
                               layout.search_clear_hovered
                                   ? RGB(255, 255, 255)
                                   : to_color_ref(theme.text_secondary));
      HGDIOBJ p_clr = SelectObject(device_context, clr_pen);
      MoveToEx(device_context, clr_cx - clr_sz / 2, clr_cy - clr_sz / 2,
               nullptr);
      LineTo(device_context, clr_cx + clr_sz / 2 + 1, clr_cy + clr_sz / 2 + 1);
      MoveToEx(device_context, clr_cx - clr_sz / 2, clr_cy + clr_sz / 2,
               nullptr);
      LineTo(device_context, clr_cx + clr_sz / 2 + 1, clr_cy - clr_sz / 2 - 1);
      SelectObject(device_context, p_clr);
      DeleteObject(clr_pen);
    }
  }

  // 4. Scope Tabs (User | Workspace) - Flat text links with active accent
  // underline
  const auto draw_scope_tab = [&](const Rect &bounds, const wchar_t *label,
                                  bool is_active, bool is_hovered) {
    SelectObject(device_context, is_active ? m_semibold_font : m_regular_font);
    SetTextColor(device_context,
                 is_active ? to_color_ref(theme.text_primary)
                           : (is_hovered ? RGB(220, 225, 235)
                                         : to_color_ref(theme.text_secondary)));
    RECT rc = to_native_rect(bounds);
    DrawTextW(device_context, label, -1, &rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  };

  draw_scope_tab(layout.user_tab_bounds, L"User",
                 m_active_scope == Zenvra::Settings::SettingsScope::User,
                 layout.user_tab_hovered);
  draw_scope_tab(layout.workspace_tab_bounds, L"Workspace",
                 m_active_scope == Zenvra::Settings::SettingsScope::Workspace,
                 layout.workspace_tab_hovered);

  // 4.5. Layout Borders: Horizontal separator under tabs + Vertical divider
  // between sidebar and content
  HPEN layout_border_pen =
      CreatePen(PS_SOLID, 1, to_color_ref(theme.titlebar_border));
  HGDIOBJ prev_lb = SelectObject(device_context, layout_border_pen);

  // Full-width horizontal border under tabs
  if (!layout.header_separator_bounds.is_empty()) {
    const int tab_line_y = static_cast<int>(layout.header_separator_bounds.y);
    MoveToEx(device_context, 0, tab_line_y, nullptr);
    LineTo(device_context, static_cast<int>(layout.dialog_bounds.right()),
           tab_line_y);
  }

  // Active Tab Underline (drawn on top of the horizontal separator)
  if (!layout.tab_underline_bounds.is_empty()) {
    draw_rect_solid(device_context, layout.tab_underline_bounds,
                    to_color_ref(theme.accent));
  }

  // Vertical divider border between sidebar and content area
  if (!layout.sidebar_divider_bounds.is_empty()) {
    const int div_x = static_cast<int>(layout.sidebar_divider_bounds.x);
    const int div_top = static_cast<int>(layout.sidebar_divider_bounds.y);
    MoveToEx(device_context, div_x, div_top, nullptr);
    LineTo(device_context, div_x,
           static_cast<int>(layout.dialog_bounds.bottom()));
  }

  SelectObject(device_context, prev_lb);
  DeleteObject(layout_border_pen);

  // 5. Sidebar (Category Tree)
  const int saved_sidebar_dc = SaveDC(device_context);
  IntersectClipRect(device_context, static_cast<int>(layout.sidebar_bounds.x),
                    static_cast<int>(layout.sidebar_bounds.y),
                    static_cast<int>(layout.sidebar_bounds.right()),
                    static_cast<int>(layout.sidebar_bounds.bottom()));

  for (const auto &item : layout.category_items) {
    if (item.bounds.bottom() < layout.sidebar_bounds.y ||
        item.bounds.y > layout.sidebar_bounds.bottom()) {
      continue;
    }

    const bool is_active =
        (item.id == m_active_category || item.name == m_active_category) &&
        m_search_input.get_text().empty();
    const bool is_hovered = (item.id == layout.hovered_category);

    if (is_active) {
      // Authentic VS Code 1px blue focus border outline
      HPEN focus_pen = CreatePen(PS_SOLID, 1, to_color_ref(theme.accent));
      HGDIOBJ old_pen = SelectObject(device_context, focus_pen);
      HGDIOBJ old_br =
          SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
      const RECT b_rc = to_native_rect(item.bounds);
      Rectangle(device_context, b_rc.left, b_rc.top, b_rc.right, b_rc.bottom);
      SelectObject(device_context, old_br);
      SelectObject(device_context, old_pen);
      DeleteObject(focus_pen);
    } else if (is_hovered) {
      const COLORREF hov_bg = to_color_ref(theme.hover);
      draw_rounded_rect(device_context, item.bounds, hov_bg, hov_bg,
                        2.0F * dpi_scale);
    }

    // Chevron icon for parent items
    if (item.has_children) {
      const int ch_sz = static_cast<int>(10.0F * dpi_scale);
      const int ch_x = static_cast<int>(item.bounds.x + 6.0F * dpi_scale);
      const int ch_y =
          static_cast<int>(item.bounds.y + (item.bounds.height - ch_sz) * 0.5F);
      const std::string ch_svg = item.is_expanded
                                     ? "Assets/icons/chevron-down.svg"
                                     : "Assets/icons/chevron-right.svg";
      draw_icon(device_context, ch_svg, ch_x, ch_y, ch_sz,
                is_active ? RGB(255, 255, 255)
                          : to_color_ref(theme.text_secondary));
    }

    // Label
    SelectObject(device_context, is_active ? m_semibold_font : m_regular_font);
    SetTextColor(device_context,
                 is_active ? RGB(255, 255, 255)
                           : (is_hovered ? RGB(235, 238, 245)
                                         : to_color_ref(theme.text_secondary)));

    const float text_indent =
        item.has_children
            ? (item.bounds.x + 22.0F * dpi_scale)
            : (item.depth == 0 ? (item.bounds.x + 22.0F * dpi_scale)
                               : (item.bounds.x + 36.0F * dpi_scale));
    Rect label_rect{text_indent, item.bounds.y,
                    item.bounds.right() - text_indent, item.bounds.height};
    RECT rc = to_native_rect(label_rect);
    const std::wstring w_name = Utility::utf8_to_wide(item.name).value_or(L"");
    DrawTextW(device_context, w_name.c_str(), -1, &rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  RestoreDC(device_context, saved_sidebar_dc);

  // Sidebar Scrollbar Thumb (if scrollable)
  if (!layout.sidebar_scrollbar_thumb.is_empty() &&
      m_sidebar_max_scroll > 0.0F) {
    const COLORREF sb_thumb_col = layout.sidebar_scrollbar_thumb_hovered ||
                                          m_is_dragging_sidebar_scrollbar
                                      ? RGB(95, 100, 110)
                                      : to_color_ref(theme.hover);
    draw_rounded_rect(device_context, layout.sidebar_scrollbar_thumb,
                      sb_thumb_col, sb_thumb_col, 2.0F * dpi_scale);
  }

  // 6. Content Area
  const int saved_dc = SaveDC(device_context);
  IntersectClipRect(device_context, static_cast<int>(layout.content_bounds.x),
                    static_cast<int>(layout.content_bounds.y),
                    static_cast<int>(layout.content_bounds.right()),
                    static_cast<int>(layout.content_bounds.bottom()));

  // Render Section Headers
  for (const auto &hdr : layout.section_headers) {
    if (hdr.bounds.bottom() < layout.content_bounds.y ||
        hdr.bounds.y > layout.content_bounds.bottom()) {
      continue;
    }
    SelectObject(device_context,
                 m_header_large_font ? m_header_large_font : m_title_font);
    SetTextColor(device_context, to_color_ref(theme.text_primary));
    RECT hdr_rc = to_native_rect(hdr.bounds);
    const std::wstring hdr_w = Utility::utf8_to_wide(hdr.title).value_or(L"");
    DrawTextW(device_context, hdr_w.c_str(), -1, &hdr_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  auto &service = Zenvra::Settings::SettingsService::instance();

  for (const auto &row : layout.rows) {
    if (!row.def)
      continue;
    if (row.bounds.bottom() < layout.content_bounds.y ||
        row.bounds.y > layout.content_bounds.bottom()) {
      continue;
    }

    const auto *def = row.def;

    // Active / Focused Row styling: Blue focus outline + Gear icon
    if (row.is_focused) {
      // Draw gear icon in gutter
      const int g_sz = static_cast<int>(15.0F * dpi_scale);
      const int gx = static_cast<int>(
          row.gear_btn_bounds.x + (row.gear_btn_bounds.width - g_sz) * 0.5F);
      const int gy = static_cast<int>(
          row.gear_btn_bounds.y + (row.gear_btn_bounds.height - g_sz) * 0.5F);
      draw_icon(device_context, "Assets/icons/gear.svg", gx, gy, g_sz,
                row.is_gear_hovered ? RGB(255, 255, 255)
                                    : to_color_ref(theme.text_secondary));

      // Draw 1px crisp blue focus rectangle around focus box
      HPEN focus_pen = CreatePen(PS_SOLID, 1, to_color_ref(theme.accent));
      HGDIOBJ old_pen = SelectObject(device_context, focus_pen);
      HGDIOBJ old_br =
          SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
      const RECT f_rc = to_native_rect(row.focus_box_bounds);
      Rectangle(device_context, f_rc.left, f_rc.top, f_rc.right, f_rc.bottom);
      SelectObject(device_context, old_br);
      SelectObject(device_context, old_pen);
      DeleteObject(focus_pen);
    }

    // Setting Title Line: "Category: " in regular font, "Title" in
    // bold/semibold font
    const std::string prefix = def->category + ": ";
    const std::wstring w_prefix = Utility::utf8_to_wide(prefix).value_or(L"");
    const std::wstring w_title =
        Utility::utf8_to_wide(def->title).value_or(L"");

    SelectObject(device_context, m_regular_font);
    SetTextColor(device_context, to_color_ref(theme.text_primary));
    SIZE p_sz{};
    GetTextExtentPoint32W(device_context, w_prefix.c_str(),
                          static_cast<int>(w_prefix.size()), &p_sz);
    RECT p_rc = to_native_rect(row.label_bounds);
    DrawTextW(device_context, w_prefix.c_str(), -1, &p_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(device_context, m_semibold_font);
    SetTextColor(device_context, to_color_ref(theme.text_primary));
    RECT t_rc = p_rc;
    t_rc.left += p_sz.cx;
    DrawTextW(device_context, w_title.c_str(), -1, &t_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SIZE t_sz{};
    GetTextExtentPoint32W(device_context, w_title.c_str(),
                          static_cast<int>(w_title.size()), &t_sz);

    if (row.is_modified) {
      SelectObject(device_context, m_small_font);
      SetTextColor(device_context, to_color_ref(theme.text_secondary));
      RECT m_rc = t_rc;
      m_rc.left += t_sz.cx + 8;
      DrawTextW(device_context, L"(Modified)", -1, &m_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Description
    SelectObject(device_context, m_regular_font);
    SetTextColor(device_context, to_color_ref(theme.text_secondary));
    RECT desc_rc = to_native_rect(row.description_bounds);
    const std::wstring desc_w =
        Utility::utf8_to_wide(def->description).value_or(L"");
    DrawTextW(device_context, desc_w.c_str(), -1, &desc_rc,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    // Control placed below description
    if (def->type == Zenvra::Settings::SettingType::Boolean) {
      const bool val = service.get<bool>(def->id);
      const COLORREF cb_bg =
          val ? to_color_ref(theme.accent)
              : (row.is_checkbox_hovered
                     ? to_color_ref(theme.command_center_background)
                     : RGB(24, 25, 28));
      const COLORREF cb_border =
          (val || row.is_checkbox_hovered)
              ? to_color_ref(theme.accent)
              : to_color_ref(theme.command_center_border);
      draw_rounded_rect(device_context, row.checkbox_bounds, cb_bg, cb_border,
                        2.0F * dpi_scale);

      if (val) {
        HPEN chk_pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ old_pen = SelectObject(device_context, chk_pen);
        const int cx = static_cast<int>(row.checkbox_bounds.x);
        const int cy = static_cast<int>(row.checkbox_bounds.y);
        MoveToEx(device_context, cx + 4, cy + 9, nullptr);
        LineTo(device_context, cx + 7, cy + 13);
        LineTo(device_context, cx + 14, cy + 5);
        SelectObject(device_context, old_pen);
        DeleteObject(chk_pen);
      }
    } else if (def->id == "editor.fontFamily") {
      // Font Family with Coding Fonts Dropdown
      const COLORREF font_bg = to_color_ref(theme.command_center_background);
      const COLORREF font_border =
          m_font_dropdown_open
              ? to_color_ref(theme.accent)
              : (row.is_input_hovered
                     ? to_color_ref(theme.hover)
                     : to_color_ref(theme.command_center_border));
      draw_rounded_rect(device_context, row.input_bounds, font_bg, font_border,
                        2.5F * dpi_scale);

      const std::string cur_font = service.get<std::string>(def->id);
      const std::wstring cur_font_w =
          Utility::utf8_to_wide(cur_font).value_or(L"");
      const std::string cur_primary = get_primary_font_name(cur_font);
      const std::wstring cur_primary_w =
          Utility::utf8_to_wide(cur_primary).value_or(L"");

      HFONT input_face_font = nullptr;
      if (!cur_primary_w.empty()) {
        input_face_font = CreateFontW(
            -static_cast<int>(13.5F * dpi_scale), 0, 0, 0, FW_NORMAL, FALSE,
            FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
            cur_primary_w.c_str());
      }

      HGDIOBJ old_in_font = SelectObject(
          device_context, input_face_font ? input_face_font : m_regular_font);
      SetTextColor(device_context, to_color_ref(theme.text_primary));
      Rect font_text_box = row.input_bounds;
      font_text_box.x += 8.0F * dpi_scale;
      font_text_box.width -= 32.0F * dpi_scale;
      RECT f_rc = to_native_rect(font_text_box);
      DrawTextW(device_context, cur_font_w.c_str(), -1, &f_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(device_context, old_in_font);
      if (input_face_font) {
        DeleteObject(input_face_font);
      }

      // Chevron down icon
      const int ch_sz = static_cast<int>(9.0F * dpi_scale);
      const int ch_x =
          static_cast<int>(row.dropdown_btn_bounds.x +
                           (row.dropdown_btn_bounds.width - ch_sz) * 0.5F);
      const int ch_y =
          static_cast<int>(row.dropdown_btn_bounds.y +
                           (row.dropdown_btn_bounds.height - ch_sz) * 0.5F);
      draw_icon(device_context, "Assets/icons/chevron-down.svg", ch_x, ch_y,
                ch_sz, to_color_ref(theme.text_secondary));
    } else if (def->id == "workbench.mascot.image") {
      const std::string img_path = service.get<std::string>(def->id);

      // 1. Thumbnail Preview Box (Unreal Engine asset slot style)
      const COLORREF thumb_border =
          (row.is_thumbnail_hovered || row.is_focused)
              ? to_color_ref(theme.accent)
              : to_color_ref(theme.command_center_border);
      const COLORREF thumb_bg = RGB(20, 21, 24);
      draw_rounded_rect(device_context, row.thumbnail_bounds, thumb_bg,
                        thumb_border, 3.0F * dpi_scale);

      // Draw mascot thumbnail inside
      draw_mascot_thumbnail(device_context, img_path, row.thumbnail_bounds,
                            theme, dpi_scale);

      const std::string render_mode =
          service.get_schema().has_setting("workbench.mascot.renderMode")
              ? service.get<std::string>("workbench.mascot.renderMode")
              : "default";
      const bool is_ascii_mode = (render_mode == "ascii");

      // Asset format badge (PNG / JPG / BMP / ASCII) at bottom-left of thumbnail
      std::string ext = is_ascii_mode ? "ASCII" : "";
      if (!is_ascii_mode) {
        const auto dot_pos = img_path.find_last_of('.');
        if (dot_pos != std::string::npos) {
          ext = img_path.substr(dot_pos + 1);
          for (auto &c : ext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          }
        }
        if (ext.empty()) {
          ext = "PNG";
        }
      }
      const std::wstring ext_w = Utility::utf8_to_wide(ext).value_or(L"PNG");

      const float badge_w = (is_ascii_mode ? 38.0F : 28.0F) * dpi_scale;
      const float badge_h = 13.0F * dpi_scale;
      const Rect badge_rc{
          row.thumbnail_bounds.x + 3.0F * dpi_scale,
          row.thumbnail_bounds.bottom() - badge_h - 3.0F * dpi_scale, badge_w,
          badge_h};
      draw_rounded_rect(device_context, badge_rc, RGB(12, 14, 18),
                        RGB(50, 54, 62), 2.0F * dpi_scale);
      SelectObject(device_context, m_small_font);
      SetTextColor(device_context, RGB(200, 205, 215));
      RECT b_native = to_native_rect(badge_rc);
      DrawTextW(device_context, ext_w.c_str(), -1, &b_native,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // 2. Asset Path Display Box
      const COLORREF path_bg = to_color_ref(theme.command_center_background);
      const COLORREF path_border =
          row.is_input_hovered ? to_color_ref(theme.hover)
                               : to_color_ref(theme.command_center_border);
      draw_rounded_rect(device_context, row.input_bounds, path_bg, path_border,
                        2.5F * dpi_scale);

      std::string display_name = img_path;
      if (display_name == "zenvra_logo.png" || display_name.empty()) {
        display_name = "zenvra_logo.png (Default Mascot)";
      }
      const std::wstring disp_w =
          Utility::utf8_to_wide(display_name).value_or(L"");
      SelectObject(device_context, m_regular_font);
      SetTextColor(device_context, to_color_ref(theme.text_primary));
      Rect text_box = row.input_bounds;
      text_box.x += 8.0F * dpi_scale;
      text_box.width -= 16.0F * dpi_scale;
      RECT p_rc = to_native_rect(text_box);
      DrawTextW(device_context, disp_w.c_str(), -1, &p_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS |
                    DT_NOPREFIX);

      // 3. Browse Button [ Browse... ]
      const COLORREF browse_bg =
          row.is_browse_hovered
              ? to_color_ref(theme.hover)
              : to_color_ref(theme.command_center_background);
      const COLORREF browse_border =
          row.is_browse_hovered ? to_color_ref(theme.accent)
                                : to_color_ref(theme.command_center_border);
      draw_rounded_rect(device_context, row.browse_btn_bounds, browse_bg,
                        browse_border, 2.5F * dpi_scale);
      SelectObject(device_context, m_regular_font);
      SetTextColor(device_context, to_color_ref(theme.text_primary));
      RECT br_rc = to_native_rect(row.browse_btn_bounds);
      DrawTextW(device_context, L"Browse...", -1, &br_rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // 4. Reset Button [ Reset ]
      const bool can_reset = row.is_modified;
      const COLORREF reset_bg =
          (can_reset && row.is_reset_hovered)
              ? to_color_ref(theme.hover)
              : to_color_ref(theme.command_center_background);
      const COLORREF reset_border =
          (can_reset && row.is_reset_hovered)
              ? to_color_ref(theme.accent)
              : to_color_ref(theme.command_center_border);
      draw_rounded_rect(device_context, row.reset_btn_bounds, reset_bg,
                        reset_border, 2.5F * dpi_scale);
      SelectObject(device_context, m_regular_font);
      SetTextColor(device_context,
                   can_reset ? to_color_ref(theme.text_primary)
                             : to_color_ref(theme.text_secondary));
      RECT rs_rc = to_native_rect(row.reset_btn_bounds);
      DrawTextW(device_context, L"Reset", -1, &rs_rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      // 5. Drag & drop hint text next to reset button
      Rect hint_rc{row.reset_btn_bounds.right() + 12.0F * dpi_scale,
                   row.browse_btn_bounds.y, 220.0F * dpi_scale,
                   row.browse_btn_bounds.height};
      SelectObject(device_context, m_small_font);
      SetTextColor(device_context, to_color_ref(theme.text_secondary));
      RECT h_native = to_native_rect(hint_rc);
      DrawTextW(device_context, L"Drop image here (.png, .jpg, .bmp)", -1,
                &h_native, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (def->id == "workbench.mascot.renderMode") {
      const bool is_ascii =
          (service.get<std::string>("workbench.mascot.renderMode") == "ascii");

      const COLORREF active_bg = to_color_ref(theme.accent);
      const COLORREF inactive_bg =
          to_color_ref(theme.command_center_background);
      const COLORREF active_border = to_color_ref(theme.accent);
      const COLORREF inactive_border =
          to_color_ref(theme.command_center_border);

      // [ Default (Solid) ]
      const bool def_active = !is_ascii;
      const COLORREF sw_def_bg =
          def_active ? active_bg
                     : (row.is_switch_default_hovered ? to_color_ref(theme.hover)
                                                      : inactive_bg);
      const COLORREF sw_def_border =
          def_active ? active_border
                     : (row.is_switch_default_hovered
                            ? to_color_ref(theme.accent)
                            : inactive_border);
      draw_rounded_rect(device_context, row.switch_default_btn_bounds, sw_def_bg,
                        sw_def_border, 3.0F * dpi_scale);
      SelectObject(device_context, m_regular_font);
      SetTextColor(device_context,
                   def_active ? RGB(255, 255, 255)
                              : to_color_ref(theme.text_secondary));
      RECT sw_def_rc = to_native_rect(row.switch_default_btn_bounds);
      DrawTextW(device_context, L"Default (Solid)", -1, &sw_def_rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // [ ASCII Art ]
      const bool asc_active = is_ascii;
      const COLORREF sw_asc_bg =
          asc_active ? active_bg
                     : (row.is_switch_ascii_hovered ? to_color_ref(theme.hover)
                                                    : inactive_bg);
      const COLORREF sw_asc_border =
          asc_active ? active_border
                     : (row.is_switch_ascii_hovered
                            ? to_color_ref(theme.accent)
                            : inactive_border);
      draw_rounded_rect(device_context, row.switch_ascii_btn_bounds, sw_asc_bg,
                        sw_asc_border, 3.0F * dpi_scale);
      SelectObject(device_context, m_regular_font);
      SetTextColor(device_context,
                   asc_active ? RGB(255, 255, 255)
                              : to_color_ref(theme.text_secondary));
      RECT sw_asc_rc = to_native_rect(row.switch_ascii_btn_bounds);
      DrawTextW(device_context, L"ASCII Art", -1, &sw_asc_rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if ((def->type == Zenvra::Settings::SettingType::Integer ||
               def->type == Zenvra::Settings::SettingType::Float ||
               def->type == Zenvra::Settings::SettingType::String) &&
               def->id != "workbench.mascot.image") {
      // Pure Text / Numeric Input (VS Code style)
      const COLORREF input_bg = to_color_ref(theme.command_center_background);
      const bool is_editing = (m_editing_setting_id == def->id);
      const COLORREF input_border =
          is_editing ? to_color_ref(theme.accent)
                     : (row.is_input_hovered
                            ? to_color_ref(theme.hover)
                            : to_color_ref(theme.command_center_border));

      draw_rounded_rect(device_context, row.input_bounds, input_bg,
                        input_border, 2.5F * dpi_scale);

      SelectObject(device_context, m_regular_font);
      SetTextColor(device_context, to_color_ref(theme.text_primary));

      const std::string val_str = is_editing
                                      ? m_editing_text
                                      : service.get(def->id).to_display_string();
      const std::wstring val_w = Utility::utf8_to_wide(val_str).value_or(L"");

      Rect text_box = row.input_bounds;
      text_box.x += 8.0F * dpi_scale;
      text_box.width -= 16.0F * dpi_scale;
      RECT v_rc = to_native_rect(text_box);
      DrawTextW(device_context, val_w.c_str(), -1, &v_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // Blinking caret when editing
      if (is_editing && m_caret_visible) {
        const std::wstring edit_w =
            Utility::utf8_to_wide(m_editing_text).value_or(L"");
        SIZE val_ext{};
        GetTextExtentPoint32W(device_context, edit_w.c_str(),
                              static_cast<int>(edit_w.size()), &val_ext);
        const int caret_x = v_rc.left + val_ext.cx + 1;
        const int caret_top =
            v_rc.top +
            static_cast<int>((row.input_bounds.height - 16.0F * dpi_scale) *
                             0.5F);
        const int caret_bottom =
            caret_top + static_cast<int>(16.0F * dpi_scale);

        HPEN caret_pen =
            CreatePen(PS_SOLID, 2, to_color_ref(theme.text_primary));
        HGDIOBJ prev_cp = SelectObject(device_context, caret_pen);
        MoveToEx(device_context, caret_x, caret_top, nullptr);
        LineTo(device_context, caret_x, caret_bottom);
        SelectObject(device_context, prev_cp);
        DeleteObject(caret_pen);
      }
    } else if (def->type == Zenvra::Settings::SettingType::Enum &&
               def->id != "workbench.mascot.renderMode") {
      // Dropdown combobox: [ Option Name        v ]
      const COLORREF opt_bg =
          row.is_option_hovered ? to_color_ref(theme.hover)
                                : to_color_ref(theme.command_center_background);
      const COLORREF opt_border =
          row.is_option_hovered ? to_color_ref(theme.accent)
                                : to_color_ref(theme.command_center_border);
      draw_rounded_rect(device_context, row.option_btn_bounds, opt_bg,
                        opt_border, 2.5F * dpi_scale);

      const std::string current = service.get<std::string>(def->id);
      const std::wstring cur_w = Utility::utf8_to_wide(current).value_or(L"");

      SelectObject(device_context, m_regular_font);
      SetTextColor(device_context, to_color_ref(theme.text_primary));
      Rect text_box = row.option_btn_bounds;
      text_box.x += 10.0F * dpi_scale;
      text_box.width -= 28.0F * dpi_scale;
      RECT o_rc = to_native_rect(text_box);
      DrawTextW(device_context, cur_w.c_str(), -1, &o_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // Chevron down icon
      const int ch_sz = static_cast<int>(9.0F * dpi_scale);
      const int ch_x =
          static_cast<int>(row.option_btn_bounds.right() - 16.0F * dpi_scale);
      const int ch_y =
          static_cast<int>(row.option_btn_bounds.y +
                           (row.option_btn_bounds.height - ch_sz) * 0.5F);
      draw_icon(device_context, "Assets/icons/chevron-down.svg", ch_x, ch_y,
                ch_sz, to_color_ref(theme.text_secondary));
    }
  }

  RestoreDC(device_context, saved_dc);

  // 7. Minimalist Content Scrollbar (6px pill)
  if (!layout.scrollbar_thumb.is_empty()) {
    const COLORREF thumb_col =
        layout.scrollbar_thumb_hovered || m_is_dragging_scrollbar
            ? RGB(85, 90, 102)
            : to_color_ref(theme.hover);
    draw_rounded_rect(device_context, layout.scrollbar_thumb, thumb_col,
                      thumb_col, 3.0F * dpi_scale);
  }

  // 8. Floating Coding Font Breakdown Dropdown (Rendered as floating popover on
  // top)
  if (m_font_dropdown_open && !m_font_dropdown_items.empty()) {
    // Dropdown container box with VS Code popover styling
    draw_rounded_rect(device_context, m_font_dropdown_bounds,
                      to_color_ref(theme.command_center_background),
                      to_color_ref(theme.accent), 3.0F * dpi_scale);

    // Clip items strictly within the rounded dropdown box so scrolled items
    // never bleed outside
    const int saved_font_dc = SaveDC(device_context);
    HRGN font_clip_rgn = CreateRoundRectRgn(
        static_cast<int>(m_font_dropdown_bounds.x + 1.0F * dpi_scale),
        static_cast<int>(m_font_dropdown_bounds.y + 1.0F * dpi_scale),
        static_cast<int>(m_font_dropdown_bounds.right() - 1.0F * dpi_scale),
        static_cast<int>(m_font_dropdown_bounds.bottom() - 1.0F * dpi_scale),
        static_cast<int>(3.0F * dpi_scale), static_cast<int>(3.0F * dpi_scale));
    SelectClipRgn(device_context, font_clip_rgn);

    const std::string cur_font = service.get<std::string>("editor.fontFamily");
    const auto &installed_fonts = get_installed_coding_fonts();

    for (std::size_t opt_idx = 0; opt_idx < m_font_dropdown_items.size() &&
                                  opt_idx < installed_fonts.size();
         ++opt_idx) {
      const auto &[font_name, item_bounds] = m_font_dropdown_items[opt_idx];
      // Skip items scrolled out of dropdown viewport
      if (item_bounds.bottom() <= m_font_dropdown_bounds.y ||
          item_bounds.y >= m_font_dropdown_bounds.bottom()) {
        continue;
      }

      const auto &opt_def = installed_fonts[opt_idx];
      const bool is_item_hovered = (m_hovered_font_option == font_name);
      const bool is_item_selected =
          (cur_font == font_name ||
           get_primary_font_name(cur_font) == opt_def.primary_name);

      if (is_item_hovered) {
        draw_rounded_rect(device_context, item_bounds,
                          to_color_ref(theme.hover), to_color_ref(theme.hover),
                          2.0F * dpi_scale);
      }

      // Active indicator / checkmark on left
      if (is_item_selected) {
        Rect pip{item_bounds.x + 4.0F * dpi_scale,
                 item_bounds.y + 8.0F * dpi_scale, 3.0F * dpi_scale,
                 item_bounds.height - 16.0F * dpi_scale};
        draw_rect_solid(device_context, pip, to_color_ref(theme.accent));
      }

      // Create preview font matching this installed font family!
      const std::wstring w_face =
          Utility::utf8_to_wide(opt_def.primary_name).value_or(L"");
      HFONT preview_font = nullptr;
      if (!w_face.empty()) {
        preview_font = CreateFontW(
            -static_cast<int>(13.5F * dpi_scale), 0, 0, 0,
            is_item_selected ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, w_face.c_str());
      }

      // Font Family Name rendered in its ACTUAL typeface
      HGDIOBJ old_font_item = SelectObject(
          device_context,
          preview_font ? preview_font
                       : (is_item_selected ? m_semibold_font : m_regular_font));
      SetTextColor(device_context, is_item_selected
                                       ? RGB(255, 255, 255)
                                       : to_color_ref(theme.text_primary));
      Rect name_rc_box{
          item_bounds.x + 14.0F * dpi_scale, item_bounds.y + 3.0F * dpi_scale,
          item_bounds.width - 20.0F * dpi_scale, 16.0F * dpi_scale};
      RECT n_rc = to_native_rect(name_rc_box);
      const std::wstring w_name =
          Utility::utf8_to_wide(opt_def.name).value_or(L"");
      DrawTextW(device_context, w_name.c_str(), -1, &n_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(device_context, old_font_item);
      if (preview_font) {
        DeleteObject(preview_font);
      }

      // Tag / Description (in clean UI small font)
      SelectObject(device_context, m_small_font);
      SetTextColor(device_context, to_color_ref(theme.text_secondary));
      Rect tag_rc_box{item_bounds.x + 14.0F * dpi_scale,
                      item_bounds.y + 19.0F * dpi_scale,
                      item_bounds.width - 20.0F * dpi_scale, 14.0F * dpi_scale};
      RECT tg_rc = to_native_rect(tag_rc_box);
      const std::wstring w_tag =
          Utility::utf8_to_wide(opt_def.tag).value_or(L"");
      DrawTextW(device_context, w_tag.c_str(), -1, &tg_rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 8b. Scrollbar for Font Dropdown (Minimalist 6px pill, VS Code style)
    if (m_font_dropdown_max_scroll > 0.0F &&
        !m_font_dropdown_scrollbar_thumb.is_empty()) {
      const bool is_thumb_active =
          m_is_dragging_font_scrollbar || m_font_scrollbar_thumb_hovered;
      const COLORREF thumb_col =
          is_thumb_active ? RGB(95, 100, 115) : to_color_ref(theme.hover);
      draw_rounded_rect(device_context, m_font_dropdown_scrollbar_thumb,
                        thumb_col, thumb_col, 3.0F * dpi_scale);
    }

    RestoreDC(device_context, saved_font_dc);
    DeleteObject(font_clip_rgn);
  }

  // 9. 1px Crisp Window Border around entire dialog
  HPEN win_border_pen =
      CreatePen(PS_SOLID, 1, to_color_ref(theme.titlebar_border));
  HGDIOBJ old_wb = SelectObject(device_context, win_border_pen);
  HGDIOBJ old_wb_br =
      SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
  Rectangle(device_context, 0, 0, static_cast<int>(layout.dialog_bounds.width),
            static_cast<int>(layout.dialog_bounds.height));
  SelectObject(device_context, old_wb_br);
  SelectObject(device_context, old_wb);
  DeleteObject(win_border_pen);

  SelectObject(device_context, prev_font);
}

void SettingsWindow::render(HDC device_context, const Rect &viewport_bounds,
                            float dpi_scale,
                            const Theme::StudioTheme &theme) const {
  const auto layout = calculate_layout(viewport_bounds, dpi_scale);
  render(device_context, layout, theme, dpi_scale);
}

} // namespace Zenvra::UI::Settings

#endif // defined(_WIN32)
