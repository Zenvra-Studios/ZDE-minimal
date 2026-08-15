#include "AddNewItemDialog.h"
#include "Utility/TextEncoding.h"
#include <algorithm>
#include <array>
#include <cctype>

#include <lunasvg.h>

#if defined(_WIN32)

#include <dwmapi.h>
#include <windowsx.h>

namespace Zenvra::UI::Components {

static RECT to_native_rect(const UI::Rect &rect) {
  return RECT{static_cast<LONG>(rect.x), static_cast<LONG>(rect.y),
              static_cast<LONG>(rect.x + rect.width),
              static_cast<LONG>(rect.y + rect.height)};
}

static COLORREF to_color_ref(const UI::Theme::Color &color) {
  return RGB(color.red, color.green, color.blue);
}

static constexpr const wchar_t *add_item_class_name = L"ZDE_AddNewItemWindow";

AddNewItemDialog::AddNewItemDialog() { init_default_templates(); }

AddNewItemDialog::~AddNewItemDialog() {
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
  for (auto &[key, entry] : m_icon_cache) {
    if (entry.bitmap) {
      DeleteObject(entry.bitmap);
      entry.bitmap = nullptr;
    }
  }
  m_icon_cache.clear();
}

void AddNewItemDialog::refresh_fonts() {
  if (m_regular_font)
    DeleteObject(m_regular_font);
  if (m_semibold_font)
    DeleteObject(m_semibold_font);
  if (m_small_font)
    DeleteObject(m_small_font);

  const int font_size_9pt = -MulDiv(9, static_cast<int>(m_dpi), 72);
  const int font_size_8pt = -MulDiv(8, static_cast<int>(m_dpi), 72);

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

static std::filesystem::path resolve_asset_path(const std::string &rel_path) {
  std::error_code ec;
  // 1. Try current working directory
  std::filesystem::path p1 = std::filesystem::current_path(ec) / rel_path;
  if (!ec && std::filesystem::exists(p1, ec))
    return p1;

  // 2. Try walking up from current working directory
  std::filesystem::path cur = std::filesystem::current_path(ec);
  for (int i = 0; i < 6 && !cur.empty(); ++i) {
    std::filesystem::path candidate = cur / rel_path;
    if (std::filesystem::exists(candidate, ec))
      return candidate;
    if (!cur.has_parent_path() || cur == cur.parent_path())
      break;
    cur = cur.parent_path();
  }

  // 3. Try executable directory
  std::array<wchar_t, 4096> exe_buf{};
  DWORD len = GetModuleFileNameW(nullptr, exe_buf.data(),
                                 static_cast<DWORD>(exe_buf.size()));
  if (len > 0) {
    std::filesystem::path exe_dir =
        std::filesystem::path(exe_buf.data()).parent_path();
    for (int i = 0; i < 6 && !exe_dir.empty(); ++i) {
      std::filesystem::path candidate = exe_dir / rel_path;
      if (std::filesystem::exists(candidate, ec))
        return candidate;
      if (!exe_dir.has_parent_path() || exe_dir == exe_dir.parent_path())
        break;
      exe_dir = exe_dir.parent_path();
    }
  }

  return std::filesystem::path(rel_path);
}

void AddNewItemDialog::draw_icon(HDC dc, const std::string &icon_rel_path,
                                 int x, int y, int size) const {
  if (icon_rel_path.empty() || size <= 0)
    return;

  std::filesystem::path full_path = resolve_asset_path(icon_rel_path);
  std::error_code ec;
  if (!std::filesystem::exists(full_path, ec)) {
    return;
  }

  const std::string cache_key = full_path.string() + "@" + std::to_string(size);
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

    const auto *src_pixels =
        reinterpret_cast<const std::uint8_t *>(bitmap.data());
    auto *dst_pixels = reinterpret_cast<std::uint8_t *>(bits);
    for (int i = 0; i < size * size; ++i) {
      std::uint8_t c0 = src_pixels[i * 4 + 0];
      std::uint8_t c1 = src_pixels[i * 4 + 1];
      std::uint8_t c2 = src_pixels[i * 4 + 2];
      std::uint8_t a = src_pixels[i * 4 + 3];

      dst_pixels[i * 4 + 0] = (c0 * a) / 255;
      dst_pixels[i * 4 + 1] = (c1 * a) / 255;
      dst_pixels[i * 4 + 2] = (c2 * a) / 255;
      dst_pixels[i * 4 + 3] = a;
    }

    m_icon_cache[cache_key] = CachedBitmap{icon_bm, size, size};
  }

  HDC mem_dc = CreateCompatibleDC(dc);
  HGDIOBJ old_bm = SelectObject(mem_dc, icon_bm);

  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;

  AlphaBlend(dc, x, y, size, size, mem_dc, 0, 0, size, size, blend);

  SelectObject(mem_dc, old_bm);
  DeleteDC(mem_dc);
}

void AddNewItemDialog::init_default_templates() {
  m_categories.clear();

  // 1. C/C++ Category
  TemplateCategory cpp_cat{
      "cpp",
      "C/C++",
      "Assets/icons/material-icon-theme/cpp.svg",
      {{"cpp_file", "C++ File (.cpp)", "Source.cpp", ".cpp", "C/C++",
        "Creates a file containing C++ source code.",
        "Assets/icons/material-icon-theme/cpp.svg",
        "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello from "
        "ZDE!\" << std::endl;\n    return 0;\n}\n"},
       {"h_file", "Header File (.h)", "Header.h", ".h", "C/C++",
        "Creates a C/C++ header file with include guards.",
        "Assets/icons/material-icon-theme/h.svg",
        "#pragma once\n\nnamespace Name\n{\n\n}\n"},
       {"cpp_class", "C++ Class", "MyClass.h", ".h", "C/C++",
        "Creates a C++ class declaration with constructor and destructor.",
        "Assets/icons/material-icon-theme/cpp.svg",
        "#pragma once\n\nnamespace Name\n{\n\nclass MyClass\n{\npublic:\n    "
        "MyClass() = default;\n    ~MyClass() = "
        "default;\n\nprivate:\n};\n\n}\n"},
       {"ixx_file", "C++ Module Interface (.ixx)", "Module.ixx", ".ixx",
        "C/C++", "Creates a modern C++20 module interface unit.",
        "Assets/icons/material-icon-theme/cpp.svg",
        "export module MyModule;\n\nexport namespace MyModule\n{\n    void "
        "hello();\n}\n"},
       {"hpp_file", "Header File (.hpp)", "Header.hpp", ".hpp", "C/C++",
        "Creates a C++ template header file.",
        "Assets/icons/material-icon-theme/hpp.svg",
        "#pragma once\n\ntemplate <typename T>\nclass Buffer\n{\npublic:\n    "
        "Buffer() = default;\n};\n"}}};

  // 2. Rust Category
  TemplateCategory rust_cat{
      "rust",
      "Rust",
      "Assets/icons/material-icon-theme/rust.svg",
      {{"rs_main", "Rust Binary (main.rs)", "main.rs", ".rs", "Rust",
        "Creates a Rust binary application entry point.",
        "Assets/icons/material-icon-theme/rust.svg",
        "fn main() {\n    println!(\"Hello from Rust!\");\n}\n"},
       {"rs_mod", "Rust Module (mod.rs)", "mod.rs", ".rs", "Rust",
        "Creates a Rust module file.",
        "Assets/icons/material-icon-theme/rust.svg",
        "pub fn hello() -> &'static str {\n    \"Hello from module\"\n}\n"},
       {"rs_lib", "Rust Library (lib.rs)", "lib.rs", ".rs", "Rust",
        "Creates a Rust library root with unit tests.",
        "Assets/icons/material-icon-theme/rust.svg",
        "pub fn add(left: usize, right: usize) -> usize {\n    left + "
        "right\n}\n"},
       {"cargo_toml", "Cargo Manifest (Cargo.toml)", "Cargo.toml", ".toml",
        "Rust", "Creates a Cargo package configuration manifest.",
        "Assets/icons/material-icon-theme/toml.svg",
        "[package]\nname = \"my_project\"\nversion = \"0.1.0\"\nedition = "
        "\"2021\"\n\n[dependencies]\n"}}};

  // 3. Shaders & Graphics Category
  TemplateCategory shader_cat{
      "shader",
      "Shaders & Graphics",
      "Assets/icons/material-icon-theme/shader.svg",
      {{"glsl_frag", "GLSL Fragment Shader (.frag)", "shader.frag", ".frag",
        "Shaders & Graphics",
        "Creates a GLSL fragment shader with standard output.",
        "Assets/icons/material-icon-theme/shader.svg",
        "#version 450 core\n\nin vec2 v_uv;\nout vec4 frag_color;\n\nvoid "
        "main()\n{\n    frag_color = vec4(v_uv, 0.5, 1.0);\n}\n"},
       {"glsl_vert", "GLSL Vertex Shader (.vert)", "shader.vert", ".vert",
        "Shaders & Graphics",
        "Creates a GLSL vertex shader with position input.",
        "Assets/icons/material-icon-theme/shader.svg",
        "#version 450 core\n\nlayout(location = 0) in vec3 "
        "a_pos;\nlayout(location = 1) in vec2 a_uv;\n\nout vec2 v_uv;\n\nvoid "
        "main()\n{\n    v_uv = a_uv;\n    gl_Position = vec4(a_pos, "
        "1.0);\n}\n"},
       {"glsl_comp", "GLSL Compute Shader (.comp)", "compute.comp", ".comp",
        "Shaders & Graphics", "Creates a GLSL compute shader.",
        "Assets/icons/material-icon-theme/shader.svg",
        "#version 450 core\n\nlayout(local_size_x = 16, local_size_y = 16) "
        "in;\n\nvoid main()\n{\n    ivec2 coord = "
        "ivec2(gl_GlobalInvocationID.xy);\n}\n"},
       {"hlsl_shader", "HLSL Pixel Shader (.hlsl)", "PixelShader.hlsl", ".hlsl",
        "Shaders & Graphics", "Creates an HLSL pixel shader.",
        "Assets/icons/material-icon-theme/shader.svg",
        "struct PSInput {\n    float4 pos : SV_POSITION;\n    float2 uv  : "
        "TEXCOORD0;\n};\n\nfloat4 main(PSInput input) : SV_TARGET\n{\n    "
        "return float4(input.uv, 0.0, 1.0);\n}\n"}}};

  // 4. Build & Config Category
  TemplateCategory build_cat{
      "build",
      "Build & Config",
      "Assets/icons/material-icon-theme/cmake.svg",
      {{"cmakelists", "CMakeLists (CMakeLists.txt)", "CMakeLists.txt", ".txt",
        "Build & Config", "Creates a CMake project build configuration script.",
        "Assets/icons/material-icon-theme/cmake.svg",
        "cmake_minimum_required(VERSION 3.25)\nproject(MyProject LANGUAGES "
        "CXX)\n\nset(CMAKE_CXX_STANDARD 20)\nadd_executable(MyProject "
        "Source.cpp)\n"},
       {"json_file", "JSON Configuration (.json)", "config.json", ".json",
        "Build & Config", "Creates a JSON configuration file.",
        "Assets/icons/material-icon-theme/json.svg",
        "{\n    \"name\": \"ZDE-Project\",\n    \"version\": \"1.0.0\"\n}\n"},
       {"toml_file", "TOML Document (.toml)", "settings.toml", ".toml",
        "Build & Config", "Creates a TOML document.",
        "Assets/icons/material-icon-theme/toml.svg",
        "[settings]\ntheme = \"zenvra_dark\"\n"}}};

  // 5. General Category
  TemplateCategory gen_cat{
      "general",
      "General",
      "Assets/icons/material-icon-theme/document.svg",
      {{"txt_file", "Text Document (.txt)", "Document.txt", ".txt", "General",
        "Creates an empty plain text document.",
        "Assets/icons/material-icon-theme/document.svg", ""},
       {"md_file", "Markdown Document (.md)", "README.md", ".md", "General",
        "Creates a Markdown documentation file.",
        "Assets/icons/material-icon-theme/markdown.svg",
        "# Project Documentation\n"},
       {"gitignore", "Git Ignore (.gitignore)", ".gitignore", "", "General",
        "Creates standard gitignore rules.",
        "Assets/icons/material-icon-theme/git.svg",
        "build/\nbin/\n*.obj\n*.exe\n.cache/\n"}}};

  m_categories.push_back(cpp_cat);
  m_categories.push_back(rust_cat);
  m_categories.push_back(shader_cat);
  m_categories.push_back(build_cat);
  m_categories.push_back(gen_cat);
}

void AddNewItemDialog::open(HWND parent_hwnd,
                            const std::filesystem::path &target_folder,
                            const std::string &project_name,
                            CreateCallback callback) {
  m_parent_hwnd = parent_hwnd;
  m_target_folder = target_folder;
  m_project_name = project_name.empty() ? "Project" : project_name;
  m_callback = std::move(callback);
  m_selected_category_index = 0;
  m_selected_template_index = 0;
  m_template_scroll_offset = 0;
  select_template(0);
  m_name_input_focused = true;
  m_caret_visible = true;

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
  wc.cbWndExtra = sizeof(AddNewItemDialog *);
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = nullptr;
  wc.lpszClassName = add_item_class_name;

  RegisterClassExW(&wc);

  m_dpi = parent_hwnd ? GetDpiForWindow(parent_hwnd) : 96;
  if (m_dpi == 0)
    m_dpi = 96;
  refresh_fonts();
  const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;

  const int width = static_cast<int>(800.0F * dpi_scale);
  const int height = static_cast<int>(520.0F * dpi_scale);

  int pos_x = CW_USEDEFAULT;
  int pos_y = CW_USEDEFAULT;

  if (parent_hwnd && IsWindow(parent_hwnd)) {
    RECT parent_rc{};
    GetWindowRect(parent_hwnd, &parent_rc);
    pos_x = parent_rc.left + (parent_rc.right - parent_rc.left - width) / 2;
    pos_y = parent_rc.top + (parent_rc.bottom - parent_rc.top - height) / 2;
  }

  m_hwnd =
      CreateWindowExW(WS_EX_APPWINDOW, add_item_class_name, L"Add New Item",
                      WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME, pos_x, pos_y,
                      width, height, parent_hwnd, nullptr, instance, this);

  if (m_hwnd != nullptr) {
    // 1. Enable immersive dark mode
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                          sizeof(dark));

    // 2. Window corner preference (Windows 11 rounded corners)
    constexpr DWORD dwm_corner_pref_attr = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
    const DWORD corner_preference = 2;         // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hwnd, dwm_corner_pref_attr, &corner_preference,
                          sizeof(corner_preference));

    // 3. Border color matching dark IDE theme (prevents high-contrast white
    // border artifact)
    constexpr DWORD dwm_border_color_attr = 34; // DWMWA_BORDER_COLOR
    const COLORREF border_color = RGB(48, 50, 55);
    DwmSetWindowAttribute(m_hwnd, dwm_border_color_attr, &border_color,
                          sizeof(border_color));

    // 4. Extend frame margins into client area so DWM manages hardware
    // dropshadow without legacy white NC frame
    const MARGINS frame_margins{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(m_hwnd, &frame_margins);

    SetTimer(m_hwnd, 1, 500, nullptr);
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    SetFocus(m_hwnd);
  }
}

void AddNewItemDialog::close() {
  if (m_hwnd != nullptr && IsWindow(m_hwnd)) {
    KillTimer(m_hwnd, 1);
    HWND hwnd = m_hwnd;
    m_hwnd = nullptr;
    DestroyWindow(hwnd);
  }
  m_close_hovered = false;
  m_add_hovered = false;
  m_cancel_hovered = false;
  m_hovered_category_index.reset();
  m_hovered_template_index.reset();
}

void AddNewItemDialog::select_template(std::size_t index) {
  if (m_selected_category_index < m_categories.size()) {
    const auto &templates = m_categories[m_selected_category_index].templates;
    if (index < templates.size()) {
      m_selected_template_index = index;
      m_filename_input = templates[index].default_filename;
      m_caret_position = m_filename_input.size();
    }
  }
}

void AddNewItemDialog::submit() {
  if (m_filename_input.empty())
    return;
  std::string content;
  if (m_selected_category_index < m_categories.size()) {
    const auto &templates = m_categories[m_selected_category_index].templates;
    if (m_selected_template_index < templates.size()) {
      content = templates[m_selected_template_index].default_content;
    }
  }
  const std::string filename = m_filename_input;
  const auto cb = m_callback;
  close();
  if (cb) {
    cb(filename, content);
  }
}

AddNewItemDialog::LayoutResult
AddNewItemDialog::calculate_layout(const UI::Rect &viewport,
                                   float dpi_scale) const {
  return calculate_layout(viewport.width, viewport.height, dpi_scale);
}

AddNewItemDialog::LayoutResult
AddNewItemDialog::calculate_layout(float width, float height,
                                   float dpi_scale) const {
  LayoutResult layout;
  layout.backdrop = {0.0F, 0.0F, width, height};
  layout.dialog_bounds = {0.0F, 0.0F, width, height};

  // Titlebar
  const float title_h = 32.0F * dpi_scale;
  layout.titlebar_bounds = {0.0F, 0.0F, width, title_h};
  const float close_btn_w = 42.0F * dpi_scale;
  layout.close_button_bounds = {width - close_btn_w, 0.0F, close_btn_w,
                                title_h};

  // Footer Panel (Bottom)
  const float footer_h = 76.0F * dpi_scale;
  layout.footer_bounds = {0.0F, height - footer_h, width, footer_h};

  const float name_label_w = 70.0F * dpi_scale;
  const float input_h = 24.0F * dpi_scale;
  const float btn_w = 80.0F * dpi_scale;
  const float btn_h = 24.0F * dpi_scale;
  const float pad_x = 16.0F * dpi_scale;

  // Row 1 of Footer: Name input + Add & Cancel buttons
  const float row1_y = layout.footer_bounds.y + 12.0F * dpi_scale;
  layout.name_label_bounds = {pad_x, row1_y + 2.0F * dpi_scale, name_label_w,
                              input_h};

  layout.cancel_button_bounds = {width - pad_x - btn_w, row1_y, btn_w, btn_h};
  layout.add_button_bounds = {layout.cancel_button_bounds.x - btn_w -
                                  8.0F * dpi_scale,
                              row1_y, btn_w, btn_h};

  const float name_input_w = layout.add_button_bounds.x - 14.0F * dpi_scale -
                             (layout.name_label_bounds.right());
  layout.name_input_bounds = {layout.name_label_bounds.right(), row1_y,
                              std::max(60.0F * dpi_scale, name_input_w),
                              input_h};

  // Row 2 of Footer: Location label & path
  const float row2_y = row1_y + input_h + 8.0F * dpi_scale;
  layout.location_label_bounds = {pad_x, row2_y + 2.0F * dpi_scale,
                                  name_label_w, input_h};
  layout.location_value_bounds = {layout.location_label_bounds.right(),
                                  row2_y + 2.0F * dpi_scale,
                                  width - pad_x * 2.0F - name_label_w, input_h};

  // Middle Workspace (Between Titlebar and Footer)
  const float middle_y = title_h;
  const float middle_h = layout.footer_bounds.y - middle_y;

  const float cat_w = 175.0F * dpi_scale;
  const float details_w = 235.0F * dpi_scale;
  const float template_w = width - cat_w - details_w;

  layout.category_pane_bounds = {0.0F, middle_y, cat_w, middle_h};
  layout.template_pane_bounds = {cat_w, middle_y, template_w, middle_h};
  layout.details_pane_bounds = {cat_w + template_w, middle_y, details_w,
                                middle_h};

  // Category Items (Full width rows starting flush at middle_y)
  const float cat_item_h = 28.0F * dpi_scale;
  float curr_cat_y = middle_y;
  for (std::size_t i = 0; i < m_categories.size(); ++i) {
    layout.category_item_bounds.push_back(
        {0.0F, curr_cat_y, cat_w, cat_item_h});
    curr_cat_y += cat_item_h;
  }

  // Template Items (Full width rows starting flush at middle_y)
  const float tpl_item_h = 28.0F * dpi_scale;
  float curr_tpl_y = middle_y - static_cast<float>(m_template_scroll_offset);
  if (m_selected_category_index < m_categories.size()) {
    const auto &templates = m_categories[m_selected_category_index].templates;
    for (std::size_t i = 0; i < templates.size(); ++i) {
      layout.template_item_bounds.push_back(
          {cat_w, curr_tpl_y, template_w, tpl_item_h});
      curr_tpl_y += tpl_item_h;
    }
  }

  return layout;
}

void AddNewItemDialog::render(HDC dc, const LayoutResult &layout,
                              const UI::Theme::StudioTheme &theme,
                              float dpi_scale) const {
  const auto &dlg = layout.dialog_bounds;
  const RECT native_dlg = to_native_rect(dlg);

  // Select Antialiased ClearType Font into DC
  HGDIOBJ prev_font = SelectObject(dc, m_regular_font);

  // 1. Clean Dark Background & Frame (Matching Text Editor Slate-Gray Palette)
  const COLORREF bg_col = RGB(30, 31, 34);
  const COLORREF border_col = RGB(48, 50, 55);
  const COLORREF section_border_col = RGB(48, 50, 55);

  HBRUSH bg_brush = CreateSolidBrush(bg_col);
  HPEN border_pen = CreatePen(PS_SOLID, 1, border_col);
  HGDIOBJ prev_brush = SelectObject(dc, bg_brush);
  HGDIOBJ prev_pen = SelectObject(dc, border_pen);

  Rectangle(dc, native_dlg.left, native_dlg.top, native_dlg.right,
            native_dlg.bottom);

  SelectObject(dc, prev_pen);
  SelectObject(dc, prev_brush);
  DeleteObject(border_pen);
  DeleteObject(bg_brush);

  // 2. Titlebar
  const COLORREF titlebar_col = RGB(29, 30, 33);
  HBRUSH title_brush = CreateSolidBrush(titlebar_col);
  RECT native_title = to_native_rect(layout.titlebar_bounds);
  FillRect(dc, &native_title, title_brush);
  DeleteObject(title_brush);

  // Titlebar separator (1px crisp border under titlebar)
  HPEN sep_pen = CreatePen(PS_SOLID, 1, RGB(48, 50, 55));
  HGDIOBJ p_pen = SelectObject(dc, sep_pen);
  MoveToEx(dc, 0, native_title.bottom - 1, nullptr);
  LineTo(dc, native_title.right, native_title.bottom - 1);

  // Title icon (16x16) & title text
  const int title_icon_size = static_cast<int>(16.0F * dpi_scale);
  const int title_icon_x = static_cast<int>(12.0F * dpi_scale);
  const int title_icon_y = static_cast<int>(
      (layout.titlebar_bounds.height - title_icon_size) * 0.5F);
  draw_icon(dc, "Assets/icons/new-file.svg", title_icon_x, title_icon_y,
            title_icon_size);

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(188, 190, 196));
  RECT title_text_r = native_title;
  title_text_r.left += static_cast<LONG>(32.0F * dpi_scale);
  const std::wstring title_str =
      L"Add New Item - " +
      Utility::utf8_to_wide(m_project_name).value_or(L"Project");
  DrawTextW(dc, title_str.c_str(), -1, &title_text_r,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Close button (✕)
  if (m_close_hovered) {
    HBRUSH close_hover_br = CreateSolidBrush(RGB(196, 43, 28));
    RECT close_r = to_native_rect(layout.close_button_bounds);
    FillRect(dc, &close_r, close_hover_br);
    DeleteObject(close_hover_br);
  }
  RECT close_text_r = to_native_rect(layout.close_button_bounds);
  SetTextColor(dc, m_close_hovered ? RGB(255, 255, 255) : RGB(140, 144, 155));
  DrawTextW(dc, L"\u2715", -1, &close_text_r,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // 3. Vertical Separator Lines
  RECT cat_r = to_native_rect(layout.category_pane_bounds);
  MoveToEx(dc, cat_r.right, cat_r.top, nullptr);
  LineTo(dc, cat_r.right, cat_r.bottom);

  RECT tpl_r = to_native_rect(layout.template_pane_bounds);
  MoveToEx(dc, tpl_r.right, tpl_r.top, nullptr);
  LineTo(dc, tpl_r.right, tpl_r.bottom);

  // 4. Left Categories Pane (With SVG Icons & Antialiased text)
  const COLORREF select_blue = RGB(53, 132, 228); // JetBrains / ZDE Accent Blue
  const int icon_size_16 = static_cast<int>(16.0F * dpi_scale);

  for (std::size_t i = 0;
       i < m_categories.size() && i < layout.category_item_bounds.size(); ++i) {
    const auto &cat = m_categories[i];
    const auto &item_b = layout.category_item_bounds[i];
    const bool selected = (i == m_selected_category_index);
    const bool hovered =
        (m_hovered_category_index && *m_hovered_category_index == i);

    RECT ir = to_native_rect(item_b);
    if (selected) {
      HBRUSH it_br = CreateSolidBrush(select_blue);
      FillRect(dc, &ir, it_br);
      DeleteObject(it_br);
    } else if (hovered) {
      HBRUSH it_br = CreateSolidBrush(RGB(45, 47, 52));
      FillRect(dc, &ir, it_br);
      DeleteObject(it_br);
    }

    // Draw Category Icon
    const int cat_ic_x = ir.left + static_cast<int>(10.0F * dpi_scale);
    const int cat_ic_y =
        ir.top + static_cast<int>((item_b.height - icon_size_16) * 0.5F);
    draw_icon(dc, cat.icon_path, cat_ic_x, cat_ic_y, icon_size_16);

    // Draw Category Text
    RECT tr = ir;
    tr.left += static_cast<LONG>(32.0F * dpi_scale);
    SetTextColor(dc, selected ? RGB(255, 255, 255) : RGB(188, 190, 196));
    const std::wstring label_w = Utility::utf8_to_wide(cat.name).value_or(L"");
    DrawTextW(dc, label_w.c_str(), -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  // 5. Middle Template Items Pane (With SVG Icons & Antialiased text)
  if (m_selected_category_index < m_categories.size()) {
    const auto &templates = m_categories[m_selected_category_index].templates;
    for (std::size_t i = 0;
         i < templates.size() && i < layout.template_item_bounds.size(); ++i) {
      const auto &tpl = templates[i];
      const auto &item_b = layout.template_item_bounds[i];
      const bool selected = (i == m_selected_template_index);
      const bool hovered =
          (m_hovered_template_index && *m_hovered_template_index == i);

      RECT ir = to_native_rect(item_b);
      if (selected) {
        HBRUSH it_br = CreateSolidBrush(select_blue);
        FillRect(dc, &ir, it_br);
        DeleteObject(it_br);
      } else if (hovered) {
        HBRUSH it_br = CreateSolidBrush(RGB(45, 47, 52));
        FillRect(dc, &ir, it_br);
        DeleteObject(it_br);
      }

      // Draw Template File Icon
      const int tpl_ic_x = ir.left + static_cast<int>(10.0F * dpi_scale);
      const int tpl_ic_y =
          ir.top + static_cast<int>((item_b.height - icon_size_16) * 0.5F);
      draw_icon(dc, tpl.icon_path, tpl_ic_x, tpl_ic_y, icon_size_16);

      // Draw Template Name
      RECT tr = ir;
      tr.left += static_cast<LONG>(32.0F * dpi_scale);
      SetTextColor(dc, selected ? RGB(255, 255, 255) : RGB(188, 190, 196));
      const std::wstring name_w = Utility::utf8_to_wide(tpl.name).value_or(L"");
      DrawTextW(dc, name_w.c_str(), -1, &tr,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      // Draw Category Tag (with small font)
      SelectObject(dc, m_small_font);
      RECT tag_r = ir;
      tag_r.right -= static_cast<LONG>(14.0F * dpi_scale);
      SetTextColor(dc, selected ? RGB(230, 230, 235) : RGB(104, 107, 115));
      const std::wstring cat_w =
          Utility::utf8_to_wide(tpl.category).value_or(L"");
      DrawTextW(dc, cat_w.c_str(), -1, &tag_r,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(dc, m_regular_font);
    }
  }

  // 6. Right Details Pane (With Large 28x28 Icon badge & Crisp Type header)
  if (m_selected_category_index < m_categories.size()) {
    const auto &templates = m_categories[m_selected_category_index].templates;
    if (m_selected_template_index < templates.size()) {
      const auto &tpl = templates[m_selected_template_index];
      RECT det_r = to_native_rect(layout.details_pane_bounds);
      det_r.left += static_cast<LONG>(14.0F * dpi_scale);
      det_r.top += static_cast<LONG>(14.0F * dpi_scale);
      det_r.right -= static_cast<LONG>(14.0F * dpi_scale);

      // Draw 28x28 Details Icon
      const int large_icon_size = static_cast<int>(24.0F * dpi_scale);
      draw_icon(dc, tpl.icon_path, det_r.left, det_r.top, large_icon_size);

      // Type header next to icon (using Semibold font)
      SelectObject(dc, m_semibold_font);
      SetTextColor(dc, RGB(220, 222, 228));
      RECT type_r = det_r;
      type_r.left += static_cast<LONG>(32.0F * dpi_scale);
      type_r.top += static_cast<LONG>(3.0F * dpi_scale);
      const std::wstring type_str =
          L"Type: " + Utility::utf8_to_wide(tpl.category).value_or(L"");
      DrawTextW(dc, type_str.c_str(), -1, &type_r,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(dc, m_regular_font);

      // Description text
      RECT desc_r = det_r;
      desc_r.top += static_cast<LONG>(34.0F * dpi_scale);
      SetTextColor(dc, RGB(140, 144, 155));
      const std::wstring desc_str =
          Utility::utf8_to_wide(tpl.description).value_or(L"");
      DrawTextW(dc, desc_str.c_str(), -1, &desc_r,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
  }

  // 7. Footer Panel
  RECT native_footer = to_native_rect(layout.footer_bounds);
  MoveToEx(dc, native_footer.left, native_footer.top, nullptr);
  LineTo(dc, native_footer.right, native_footer.top);
  SelectObject(dc, p_pen);
  DeleteObject(sep_pen);

  // Name Label
  SetTextColor(dc, RGB(188, 190, 196));
  RECT name_lbl = to_native_rect(layout.name_label_bounds);
  DrawTextW(dc, L"Name:", -1, &name_lbl,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Location Label (full "Location:")
  RECT loc_lbl = to_native_rect(layout.location_label_bounds);
  DrawTextW(dc, L"Location:", -1, &loc_lbl,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Location Value
  SetTextColor(dc, RGB(120, 124, 135));
  RECT loc_val = to_native_rect(layout.location_value_bounds);
  const std::wstring loc_w = m_target_folder.wstring();
  DrawTextW(dc, loc_w.c_str(), -1, &loc_val,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                DT_PATH_ELLIPSIS);

  // Name Input Box
  const COLORREF input_bg = RGB(24, 25, 28);
  const COLORREF input_border =
      m_name_input_focused ? select_blue : RGB(48, 50, 55);

  HBRUSH in_br = CreateSolidBrush(input_bg);
  HPEN in_pen = CreatePen(PS_SOLID, 1, input_border);
  HGDIOBJ p_in_b = SelectObject(dc, in_br);
  HGDIOBJ p_in_p = SelectObject(dc, in_pen);
  RECT in_r = to_native_rect(layout.name_input_bounds);
  Rectangle(dc, in_r.left, in_r.top, in_r.right, in_r.bottom);
  SelectObject(dc, p_in_p);
  SelectObject(dc, p_in_b);
  DeleteObject(in_pen);
  DeleteObject(in_br);

  // Input text
  RECT in_text_r = in_r;
  in_text_r.left += static_cast<LONG>(6.0F * dpi_scale);
  in_text_r.right -= static_cast<LONG>(6.0F * dpi_scale);
  SetTextColor(dc, RGB(220, 222, 228));
  const std::wstring fn_w =
      Utility::utf8_to_wide(m_filename_input).value_or(L"");
  DrawTextW(dc, fn_w.c_str(), -1, &in_text_r,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Blinking Caret
  if (m_name_input_focused && m_caret_visible) {
    SIZE text_size{};
    const std::wstring pre_caret =
        fn_w.substr(0, std::min(m_caret_position, fn_w.size()));
    GetTextExtentPoint32W(dc, pre_caret.c_str(),
                          static_cast<int>(pre_caret.size()), &text_size);
    const int caret_x = in_text_r.left + text_size.cx;
    const int caret_y1 = in_r.top + static_cast<int>(3.0F * dpi_scale);
    const int caret_y2 = in_r.bottom - static_cast<int>(3.0F * dpi_scale);
    HPEN caret_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ p_cp = SelectObject(dc, caret_pen);
    MoveToEx(dc, caret_x, caret_y1, nullptr);
    LineTo(dc, caret_x, caret_y2);
    SelectObject(dc, p_cp);
    DeleteObject(caret_pen);
  }

  // Add Button (JetBrains / ZDE Accent Blue Flat Button)
  const COLORREF add_bg = m_add_hovered ? RGB(65, 145, 240) : select_blue;
  HBRUSH add_br = CreateSolidBrush(add_bg);
  RECT add_r = to_native_rect(layout.add_button_bounds);
  FillRect(dc, &add_r, add_br);
  DeleteObject(add_br);
  SelectObject(dc, m_regular_font);
  SetTextColor(dc, RGB(255, 255, 255));
  DrawTextW(dc, L"Add", -1, &add_r,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Cancel Button (Dark Slate-Gray Flat Button)
  const COLORREF cancel_bg =
      m_cancel_hovered ? RGB(45, 47, 52) : RGB(36, 37, 42);
  HBRUSH can_br = CreateSolidBrush(cancel_bg);
  HPEN can_pen = CreatePen(PS_SOLID, 1, RGB(48, 50, 55));
  HGDIOBJ p_can_b = SelectObject(dc, can_br);
  HGDIOBJ p_can_p = SelectObject(dc, can_pen);
  RECT can_r = to_native_rect(layout.cancel_button_bounds);
  Rectangle(dc, can_r.left, can_r.top, can_r.right, can_r.bottom);
  SelectObject(dc, p_can_p);
  SelectObject(dc, p_can_b);
  DeleteObject(can_pen);
  DeleteObject(can_br);
  SetTextColor(dc, RGB(188, 190, 196));
  DrawTextW(dc, L"Cancel", -1, &can_r,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // 8. 1px Outer Window Frame Border (drawn last to outline the entire dialog
  // window)
  HPEN frame_pen = CreatePen(PS_SOLID, 1, RGB(48, 50, 55));
  HGDIOBJ p_fp = SelectObject(dc, frame_pen);
  HGDIOBJ p_fb = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  Rectangle(dc, native_dlg.left, native_dlg.top, native_dlg.right,
            native_dlg.bottom);
  SelectObject(dc, p_fb);
  SelectObject(dc, p_fp);
  DeleteObject(frame_pen);

  // Restore original font
  SelectObject(dc, prev_font);
}

LRESULT CALLBACK AddNewItemDialog::dialog_proc(HWND hwnd, UINT message,
                                               WPARAM w_param, LPARAM l_param) {
  AddNewItemDialog *self = nullptr;
  if (message == WM_NCCREATE) {
    auto *cs = reinterpret_cast<CREATESTRUCTW *>(l_param);
    self = reinterpret_cast<AddNewItemDialog *>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<AddNewItemDialog *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self) {
    return self->handle_message(hwnd, message, w_param, l_param);
  }

  return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT AddNewItemDialog::handle_message(HWND hwnd, UINT message,
                                         WPARAM w_param, LPARAM l_param) {
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

  case WM_NCHITTEST: {
    POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    ScreenToClient(hwnd, &pt);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout = calculate_layout(width, height, dpi_scale);

    // Close button handles client click
    if (layout.close_button_bounds.contains(static_cast<float>(pt.x),
                                            static_cast<float>(pt.y))) {
      return HTCLIENT;
    }
    // Titlebar draggable
    if (layout.titlebar_bounds.contains(static_cast<float>(pt.x),
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

    if (handle_pointer_move(x, y, layout)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }

    TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    return 0;
  }

  case WM_MOUSELEAVE: {
    m_close_hovered = false;
    m_add_hovered = false;
    m_cancel_hovered = false;
    m_hovered_category_index.reset();
    m_hovered_template_index.reset();
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  }

  case WM_LBUTTONDOWN: {
    const float x = static_cast<float>(GET_X_LPARAM(l_param));
    const float y = static_cast<float>(GET_Y_LPARAM(l_param));
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout =
        calculate_layout(static_cast<float>(rc.right - rc.left),
                         static_cast<float>(rc.bottom - rc.top), dpi_scale);

    if (handle_pointer_press(x, y, layout)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_LBUTTONDBLCLK: {
    const float x = static_cast<float>(GET_X_LPARAM(l_param));
    const float y = static_cast<float>(GET_Y_LPARAM(l_param));
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout =
        calculate_layout(static_cast<float>(rc.right - rc.left),
                         static_cast<float>(rc.bottom - rc.top), dpi_scale);

    if (handle_double_click(x, y, layout)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_MOUSEWHEEL: {
    const short delta = GET_WHEEL_DELTA_WPARAM(w_param);
    if (handle_scroll(delta)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_CHAR: {
    char ch = static_cast<char>(w_param);
    if (ch >= 32 && ch != 127) {
      std::string s(1, ch);
      if (handle_text_input(s)) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
    }
    return 0;
  }

  case WM_KEYDOWN: {
    if (handle_key_down(w_param)) {
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  }

  case WM_CLOSE:
    close();
    return 0;

  case WM_DESTROY:
    KillTimer(hwnd, 1);
    m_hwnd = nullptr;
    return 0;

  default:
    break;
  }

  return DefWindowProcW(hwnd, message, w_param, l_param);
}

bool AddNewItemDialog::handle_pointer_move(float x, float y,
                                           const LayoutResult &layout) {
  const bool old_close = m_close_hovered;
  const bool old_add = m_add_hovered;
  const bool old_cancel = m_cancel_hovered;
  const auto old_cat = m_hovered_category_index;
  const auto old_tpl = m_hovered_template_index;

  m_close_hovered = layout.close_button_bounds.contains(x, y);
  m_add_hovered = layout.add_button_bounds.contains(x, y);
  m_cancel_hovered = layout.cancel_button_bounds.contains(x, y);

  m_hovered_category_index.reset();
  for (std::size_t i = 0; i < layout.category_item_bounds.size(); ++i) {
    if (layout.category_item_bounds[i].contains(x, y)) {
      m_hovered_category_index = i;
      break;
    }
  }

  m_hovered_template_index.reset();
  for (std::size_t i = 0; i < layout.template_item_bounds.size(); ++i) {
    if (layout.template_item_bounds[i].contains(x, y)) {
      m_hovered_template_index = i;
      break;
    }
  }

  return old_close != m_close_hovered || old_add != m_add_hovered ||
         old_cancel != m_cancel_hovered ||
         old_cat != m_hovered_category_index ||
         old_tpl != m_hovered_template_index;
}

bool AddNewItemDialog::handle_pointer_press(float x, float y,
                                            const LayoutResult &layout) {
  if (layout.close_button_bounds.contains(x, y) ||
      layout.cancel_button_bounds.contains(x, y)) {
    close();
    return true;
  }

  if (layout.add_button_bounds.contains(x, y)) {
    submit();
    return true;
  }

  if (layout.name_input_bounds.contains(x, y)) {
    m_name_input_focused = true;
    return true;
  }

  // Category click
  for (std::size_t i = 0; i < layout.category_item_bounds.size(); ++i) {
    if (layout.category_item_bounds[i].contains(x, y)) {
      m_selected_category_index = i;
      m_template_scroll_offset = 0;
      select_template(0);
      return true;
    }
  }

  // Template click
  for (std::size_t i = 0; i < layout.template_item_bounds.size(); ++i) {
    if (layout.template_item_bounds[i].contains(x, y)) {
      select_template(i);
      return true;
    }
  }

  return true;
}

bool AddNewItemDialog::handle_double_click(float x, float y,
                                           const LayoutResult &layout) {
  // Double click on template immediately submits
  for (std::size_t i = 0; i < layout.template_item_bounds.size(); ++i) {
    if (layout.template_item_bounds[i].contains(x, y)) {
      select_template(i);
      submit();
      return true;
    }
  }
  return false;
}

bool AddNewItemDialog::handle_scroll(int delta) {
  const int step = 30;
  if (delta > 0) {
    m_template_scroll_offset = std::max(0, m_template_scroll_offset - step);
  } else {
    m_template_scroll_offset = std::min(400, m_template_scroll_offset + step);
  }
  return true;
}

bool AddNewItemDialog::handle_text_input(std::string_view text) {
  if (!m_name_input_focused || text.empty())
    return false;
  for (char ch : text) {
    if (ch >= 32 && ch != 127) {
      m_filename_input.insert(m_caret_position, 1, ch);
      m_caret_position++;
    }
  }
  return true;
}

bool AddNewItemDialog::handle_key_down(WPARAM w_param) {
  if (w_param == VK_ESCAPE) {
    close();
    return true;
  }

  if (w_param == VK_RETURN) {
    submit();
    return true;
  }

  if (w_param == VK_BACK) {
    if (!m_filename_input.empty() && m_caret_position > 0) {
      m_filename_input.erase(m_caret_position - 1, 1);
      m_caret_position--;
      return true;
    }
  }

  if (w_param == VK_DELETE) {
    if (m_caret_position < m_filename_input.size()) {
      m_filename_input.erase(m_caret_position, 1);
      return true;
    }
  }

  if (w_param == VK_LEFT) {
    if (m_caret_position > 0) {
      m_caret_position--;
      return true;
    }
  }

  if (w_param == VK_RIGHT) {
    if (m_caret_position < m_filename_input.size()) {
      m_caret_position++;
      return true;
    }
  }

  if (w_param == VK_UP) {
    if (m_selected_template_index > 0) {
      select_template(m_selected_template_index - 1);
      return true;
    }
  }

  if (w_param == VK_DOWN) {
    if (m_selected_category_index < m_categories.size()) {
      const auto &tpls = m_categories[m_selected_category_index].templates;
      if (m_selected_template_index + 1 < tpls.size()) {
        select_template(m_selected_template_index + 1);
        return true;
      }
    }
  }

  return false;
}

bool AddNewItemDialog::is_interactive_point(float x, float y,
                                            const LayoutResult &layout) const {
  if (layout.close_button_bounds.contains(x, y) ||
      layout.add_button_bounds.contains(x, y) ||
      layout.cancel_button_bounds.contains(x, y) ||
      layout.name_input_bounds.contains(x, y)) {
    return true;
  }
  for (const auto &b : layout.category_item_bounds) {
    if (b.contains(x, y))
      return true;
  }
  for (const auto &b : layout.template_item_bounds) {
    if (b.contains(x, y))
      return true;
  }
  return false;
}

} // namespace Zenvra::UI::Components

#endif // defined(_WIN32)
