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
  std::vector<std::string> path_variants = {rel_path};
  if (rel_path.starts_with("Assets/icons/") || rel_path.starts_with("Assets\\icons\\")) {
    path_variants.push_back("Resources/icons/" + rel_path.substr(13));
    path_variants.push_back("Resources/" + rel_path.substr(13));
    path_variants.push_back("icons/" + rel_path.substr(13));
  } else if (rel_path.starts_with("Assets/") || rel_path.starts_with("Assets\\")) {
    path_variants.push_back("Resources/" + rel_path.substr(7));
  }

  // 1. Try executable directory first (crucial for installed app / Start Menu)
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

    std::memcpy(bits, bitmap.data(), static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);

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
      "Assets/icons/vscode-symbols/files/cplus.svg",
      {{"cpp_file", "C++ File (.cpp)", "Source.cpp", ".cpp", "C/C++",
        "Creates a file containing C++ source code.",
        "Assets/icons/vscode-symbols/files/cplus.svg",
        "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello from "
        "ZDE!\" << std::endl;\n    return 0;\n}\n"},
       {"h_file", "Header File (.h)", "Header.h", ".h", "C/C++",
        "Creates a C/C++ header file with include guards.",
        "Assets/icons/vscode-symbols/files/h.svg",
        "#pragma once\n\nnamespace Name\n{\n\n}\n"},
       {"cpp_class", "C++ Class", "MyClass.h", ".h", "C/C++",
        "Creates a C++ class declaration with constructor and destructor.",
        "Assets/icons/vscode-symbols/files/cplus.svg",
        "#pragma once\n\nnamespace Name\n{\n\nclass MyClass\n{\npublic:\n    "
        "MyClass() = default;\n    ~MyClass() = "
        "default;\n\nprivate:\n};\n\n}\n"},
       {"ixx_file", "C++ Module Interface (.ixx)", "Module.ixx", ".ixx",
        "C/C++", "Creates a modern C++20 module interface unit.",
        "Assets/icons/vscode-symbols/files/cplus.svg",
        "export module MyModule;\n\nexport namespace MyModule\n{\n    void "
        "hello();\n}\n"},
       {"hpp_file", "Header File (.hpp)", "Header.hpp", ".hpp", "C/C++",
        "Creates a C++ template header file.",
        "Assets/icons/vscode-symbols/files/cplus.svg",
        "#pragma once\n\ntemplate <typename T>\nclass Buffer\n{\npublic:\n    "
        "Buffer() = default;\n};\n"}}};

  // 2. Rust Category
  TemplateCategory rust_cat{
      "rust",
      "Rust",
      "Assets/icons/vscode-symbols/files/rust.svg",
      {{"rs_main", "Rust Binary (main.rs)", "main.rs", ".rs", "Rust",
        "Creates a Rust binary application entry point.",
        "Assets/icons/vscode-symbols/files/rust.svg",
        "fn main() {\n    println!(\"Hello from Rust!\");\n}\n"},
       {"rs_mod", "Rust Module (mod.rs)", "mod.rs", ".rs", "Rust",
        "Creates a Rust module file.",
        "Assets/icons/vscode-symbols/files/rust.svg",
        "pub fn hello() -> &'static str {\n    \"Hello from module\"\n}\n"},
       {"rs_lib", "Rust Library (lib.rs)", "lib.rs", ".rs", "Rust",
        "Creates a Rust library root with unit tests.",
        "Assets/icons/vscode-symbols/files/rust.svg",
        "pub fn add(left: usize, right: usize) -> usize {\n    left + "
        "right\n}\n"},
       {"cargo_toml", "Cargo Manifest (Cargo.toml)", "Cargo.toml", ".toml",
        "Rust", "Creates a Cargo package configuration manifest.",
        "Assets/icons/vscode-symbols/files/rust.svg",
        "[package]\nname = \"my_project\"\nversion = \"0.1.0\"\nedition = "
        "\"2021\"\n\n[dependencies]\n"}}};

  // 3. TypeScript & JavaScript Category
  TemplateCategory ts_cat{
      "typescript",
      "TypeScript / JavaScript",
      "Assets/icons/vscode-symbols/files/ts.svg",
      {{"ts_file", "TypeScript File (.ts)", "index.ts", ".ts", "TypeScript",
        "Creates a modern TypeScript source file.",
        "Assets/icons/vscode-symbols/files/ts.svg",
        "export interface AppConfig {\n    title: string;\n    version: string;\n}\n\nexport const config: AppConfig = {\n    title: \"ZDE Application\",\n    version: \"1.0.0\",\n};\n\nexport function bootstrap(): void {\n    console.log(`Starting ${config.title} v${config.version}...`);\n}\n\nbootstrap();\n"},
       {"js_file", "JavaScript File (.js)", "index.js", ".js", "JavaScript",
        "Creates a standard JavaScript source file.",
        "Assets/icons/vscode-symbols/files/js.svg",
        "// @ts-check\n\nexport function bootstrap() {\n    console.log('Hello from JavaScript in ZDE!');\n}\n\nbootstrap();\n"},
       {"mjs_file", "ES Module File (.mjs)", "index.mjs", ".mjs", "JavaScript",
        "Creates a modern ECMAScript module file.",
        "Assets/icons/vscode-symbols/files/js.svg",
        "import { promises as fs } from 'node:fs';\n\nexport async function bootstrap() {\n    console.log('Running ES Module in ZDE...');\n}\n\nawait bootstrap();\n"},
       {"tsx_file", "React Component (.tsx)", "Component.tsx", ".tsx", "TypeScript",
        "Creates a React functional component with TypeScript props.",
        "Assets/icons/vscode-symbols/files/react-ts.svg",
        "import React, { useState } from 'react';\n\nexport interface ComponentProps {\n    title?: string;\n}\n\nexport const Component: React.FC<ComponentProps> = ({\n    title = \"ZDE Component\",\n}) => {\n    const [count, setCount] = useState<number>(0);\n\n    return (\n        <div className=\"container\">\n            <h2>{title}</h2>\n            <button onClick={() => setCount(count + 1)}>Count: {count}</button>\n        </div>\n    );\n};\n\nexport default Component;\n"},
       {"jsx_file", "React Component (.jsx)", "Component.jsx", ".jsx", "JavaScript",
        "Creates a React functional component with JSX syntax.",
        "Assets/icons/vscode-symbols/files/react.svg",
        "import React, { useState } from 'react';\n\nexport const Component = ({ title = 'ZDE Component' }) => {\n    const [count, setCount] = useState(0);\n\n    return (\n        <div className=\"container\">\n            <h2>{title}</h2>\n            <button onClick={() => setCount(count + 1)}>Count: {count}</button>\n        </div>\n    );\n};\n\nexport default Component;\n"},
       {"dts_file", "TypeScript Declaration (.d.ts)", "types.d.ts", ".d.ts", "TypeScript",
        "Creates a TypeScript ambient declaration type definitions file.",
        "Assets/icons/vscode-symbols/files/dts.svg",
        "declare namespace ZDE {\n    interface UserSession {\n        id: string;\n        username: string;\n        createdAt: Date;\n    }\n}\n"},
       {"tsconfig", "TSConfig (tsconfig.json)", "tsconfig.json", ".json", "TypeScript",
        "Creates a standard TypeScript compiler configuration file.",
        "Assets/icons/vscode-symbols/files/tsconfig.svg",
        "{\n  \"compilerOptions\": {\n    \"target\": \"ESNext\",\n    \"module\": \"ESNext\",\n    \"moduleResolution\": \"bundler\",\n    \"strict\": true,\n    \"jsx\": \"react-jsx\",\n    \"esModuleInterop\": true,\n    \"skipLibCheck\": true,\n    \"forceConsistentCasingInFileNames\": true\n  },\n  \"include\": [\"src/**/*\"]\n}\n"}}};

  // 4. Shaders & Graphics Category
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
        "Creates a GLSL vertex shader with attribute inputs.",
        "Assets/icons/material-icon-theme/shader.svg",
        "#version 450 core\n\nlayout (location = 0) in vec3 a_pos;\nlayout "
        "(location = 1) in vec2 a_uv;\n\nout vec2 v_uv;\n\nvoid main()\n{\n    "
        "v_uv = a_uv;\n    gl_Position = vec4(a_pos, 1.0);\n}\n"},
       {"hlsl_file", "HLSL Compute Shader (.hlsl)", "Compute.hlsl", ".hlsl",
        "Shaders & Graphics",
        "Creates a Direct3D HLSL compute shader with thread group attributes.",
        "Assets/icons/material-icon-theme/shader.svg",
        "[numthreads(8, 8, 1)]\nvoid CSMain(uint3 id : SV_DispatchThreadID)\n{\n "
        "   // Compute logic\n}\n"}}};

  // 5. Build & Config Category
  TemplateCategory build_cat{
      "build",
      "Build & Config",
      "Assets/icons/vscode-symbols/files/cmake.svg",
      {{"cmakelists", "CMakeLists (CMakeLists.txt)", "CMakeLists.txt", ".txt",
        "Build & Config", "Creates a CMake project build configuration script.",
        "Assets/icons/vscode-symbols/files/cmake.svg",
        "cmake_minimum_required(VERSION 3.25)\nproject(MyProject LANGUAGES "
        "CXX)\n\nset(CMAKE_CXX_STANDARD 20)\nadd_executable(MyProject "
        "Source.cpp)\n"},
       {"json_file", "JSON Configuration (.json)", "config.json", ".json",
        "Build & Config", "Creates a JSON configuration file.",
        "Assets/icons/vscode-symbols/files/brackets-yellow.svg",
        "{\n    \"name\": \"ZDE-Project\",\n    \"version\": \"1.0.0\"\n}\n"},
       {"toml_file", "TOML Document (.toml)", "settings.toml", ".toml",
        "Build & Config", "Creates a TOML document.",
        "Assets/icons/vscode-symbols/files/gear.svg",
        "[settings]\ntheme = \"zenvra_dark\"\n"}}};

  // 6. HTML & Web Category
  TemplateCategory web_cat{
      "web",
      "HTML & Web",
      "Assets/icons/vscode-symbols/files/code-orange.svg",
      {{"html5_page", "HTML5 Page (.html)", "index.html", ".html", "HTML & Web",
        "Creates a modern HTML5 document structure with viewport and styling.",
        "Assets/icons/vscode-symbols/files/code-orange.svg",
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    <meta charset=\"UTF-8\">\n    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n    <title>ZDE App</title>\n    <link rel=\"stylesheet\" href=\"style.css\">\n</head>\n<body>\n    <div class=\"container\">\n        <h1>Hello from ZDE!</h1>\n        <p>Built with native C++ power.</p>\n    </div>\n    <script src=\"main.js\"></script>\n</body>\n</html>\n"},
       {"css_style", "CSS Stylesheet (.css)", "style.css", ".css", "HTML & Web",
        "Creates a CSS stylesheet for HTML layouts.",
        "Assets/icons/vscode-symbols/files/code-sky.svg",
        "* {\n    box-sizing: border-box;\n    margin: 0;\n    padding: 0;\n}\n\nbody {\n    font-family: system-ui, -apple-system, sans-serif;\n    background-color: #1e1e1e;\n    color: #ffffff;\n    padding: 2rem;\n}\n"},
       {"svg_graphic", "SVG Vector Graphic (.svg)", "graphic.svg", ".svg", "HTML & Web",
        "Creates an SVG scalable vector graphics XML file.",
        "Assets/icons/vscode-symbols/files/svg.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n    <circle cx=\"12\" cy=\"12\" r=\"10\"></circle>\n</svg>\n"}}};

  // 7. General Category
  TemplateCategory gen_cat{
      "general",
      "General",
      "Assets/icons/vscode-symbols/files/document.svg",
      {{"txt_file", "Text Document (.txt)", "Document.txt", ".txt", "General",
        "Creates an empty plain text document.",
        "Assets/icons/vscode-symbols/files/document.svg", ""},
       {"md_file", "Markdown Document (.md)", "README.md", ".md", "General",
        "Creates a Markdown documentation file.",
        "Assets/icons/vscode-symbols/files/markdown.svg",
        "# Project Documentation\n"},
       {"gitignore", "Git Ignore (.gitignore)", ".gitignore", "", "General",
        "Creates standard gitignore rules.",
        "Assets/icons/vscode-symbols/files/git.svg",
        "build/\nbin/\n*.obj\n*.exe\n.cache/\n"}}};

  m_categories.push_back(cpp_cat);
  m_categories.push_back(rust_cat);
  m_categories.push_back(ts_cat);
  m_categories.push_back(web_cat);
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
  m_category_scroll_offset = 0.0F;
  m_template_scroll_offset = 0.0F;
  m_category_scroll_dragging = false;
  m_template_scroll_dragging = false;
  for (auto &[key, entry] : m_icon_cache) {
    if (entry.bitmap) {
      DeleteObject(entry.bitmap);
      entry.bitmap = nullptr;
    }
  }
  m_icon_cache.clear();
}

void AddNewItemDialog::select_template(std::size_t index) {
  if (m_selected_category_index < m_categories.size()) {
    const auto &templates = m_categories[m_selected_category_index].templates;
    if (index < templates.size()) {
      m_selected_template_index = index;
      m_filename_input = templates[index].default_filename;
      select_stem();
    }
  }
}

std::pair<std::size_t, std::size_t> AddNewItemDialog::get_selection_range() const noexcept {
  if (!m_selection_anchor.has_value() || *m_selection_anchor == m_caret_position) {
    return {m_caret_position, m_caret_position};
  }
  return {std::min(*m_selection_anchor, m_caret_position),
          std::max(*m_selection_anchor, m_caret_position)};
}

void AddNewItemDialog::select_all() noexcept {
  m_selection_anchor = 0;
  m_caret_position = m_filename_input.size();
}

void AddNewItemDialog::select_stem() noexcept {
  const std::size_t dot_pos = m_filename_input.rfind('.');
  if (dot_pos != std::string::npos && dot_pos > 0) {
    m_selection_anchor = 0;
    m_caret_position = dot_pos;
  } else {
    select_all();
  }
}

void AddNewItemDialog::delete_selection() {
  if (!has_selection())
    return;
  const auto [start, end] = get_selection_range();
  if (start < m_filename_input.size() && end <= m_filename_input.size() && start < end) {
    m_filename_input.erase(start, end - start);
    m_caret_position = start;
  }
  clear_selection();
}

void AddNewItemDialog::copy_selection_to_clipboard() const {
  if (!has_selection())
    return;
  const auto [start, end] = get_selection_range();
  if (start < end && end <= m_filename_input.size()) {
    const std::string selected_text = m_filename_input.substr(start, end - start);
    const std::wstring wide_text = Utility::utf8_to_wide(selected_text).value_or(L"");
    if (!wide_text.empty() && OpenClipboard(m_hwnd)) {
      EmptyClipboard();
      const std::size_t bytes = (wide_text.size() + 1) * sizeof(wchar_t);
      HGLOBAL h_glob = GlobalAlloc(GMEM_MOVEABLE, bytes);
      if (h_glob) {
        void *locked = GlobalLock(h_glob);
        if (locked) {
          memcpy(locked, wide_text.c_str(), bytes);
          GlobalUnlock(h_glob);
          SetClipboardData(CF_UNICODETEXT, h_glob);
        }
      }
      CloseClipboard();
    }
  }
}

void AddNewItemDialog::paste_from_clipboard() {
  if (OpenClipboard(m_hwnd)) {
    HANDLE h_data = GetClipboardData(CF_UNICODETEXT);
    if (h_data) {
      const wchar_t *wide_str = static_cast<const wchar_t *>(GlobalLock(h_data));
      if (wide_str) {
        std::string text = Utility::wide_to_utf8(wide_str).value_or("");
        GlobalUnlock(h_data);
        if (!text.empty()) {
          text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
          text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
          if (has_selection()) {
            delete_selection();
          }
          m_filename_input.insert(m_caret_position, text);
          m_caret_position += text.size();
          clear_selection();
        }
      }
    }
    CloseClipboard();
  }
}

void AddNewItemDialog::cut_selection_to_clipboard() {
  if (has_selection()) {
    copy_selection_to_clipboard();
    delete_selection();
  }
}

std::size_t AddNewItemDialog::get_char_index_from_x(float click_x,
                                                    const LayoutResult &layout,
                                                    float dpi_scale) const {
  const float pad_left = 6.0F * dpi_scale;
  const float text_x = layout.name_input_bounds.x + pad_left;
  const float local_x = click_x - text_x;
  if (local_x <= 0.0F || m_filename_input.empty()) {
    return 0;
  }

  HDC dc = GetDC(m_hwnd);
  HGDIOBJ prev_font = SelectObject(dc, m_regular_font);
  const std::wstring fn_w = Utility::utf8_to_wide(m_filename_input).value_or(L"");
  std::size_t best_idx = fn_w.size();
  int prev_cx = 0;
  for (std::size_t i = 1; i <= fn_w.size(); ++i) {
    SIZE sz{};
    GetTextExtentPoint32W(dc, fn_w.c_str(), static_cast<int>(i), &sz);
    if (local_x < (prev_cx + sz.cx) / 2.0F) {
      best_idx = i - 1;
      break;
    }
    if (local_x <= static_cast<float>(sz.cx)) {
      best_idx = i;
      break;
    }
    prev_cx = sz.cx;
  }
  SelectObject(dc, prev_font);
  ReleaseDC(m_hwnd, dc);
  return best_idx;
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

  // Category Items & Scrollbar Calculation
  const float cat_item_h = 28.0F * dpi_scale;
  const float cat_total_h = static_cast<float>(m_categories.size()) * cat_item_h;
  const float cat_max_scroll = (cat_total_h > middle_h) ? (cat_total_h - middle_h + 4.0F * dpi_scale) : 0.0F;
  const_cast<AddNewItemDialog *>(this)->m_category_scroll_offset =
      std::clamp(m_category_scroll_offset, 0.0F, cat_max_scroll);

  layout.category_scroll_visible = (cat_max_scroll > 0.0F);
  if (layout.category_scroll_visible) {
    layout.category_scrollbar_track = {cat_w - 6.0F * dpi_scale, middle_y + 2.0F * dpi_scale,
                                       4.0F * dpi_scale, middle_h - 4.0F * dpi_scale};
    const float thumb_h = std::max(20.0F * dpi_scale, (middle_h / cat_total_h) * layout.category_scrollbar_track.height);
    const float thumb_y = layout.category_scrollbar_track.y +
                          (m_category_scroll_offset / cat_max_scroll) * (layout.category_scrollbar_track.height - thumb_h);
    layout.category_scrollbar_thumb = {layout.category_scrollbar_track.x, thumb_y,
                                       layout.category_scrollbar_track.width, thumb_h};
  }

  float curr_cat_y = middle_y - m_category_scroll_offset;
  for (std::size_t i = 0; i < m_categories.size(); ++i) {
    layout.category_item_bounds.push_back(
        {0.0F, curr_cat_y, cat_w, cat_item_h});
    curr_cat_y += cat_item_h;
  }

  // Template Items & Scrollbar Calculation
  const float tpl_item_h = 28.0F * dpi_scale;
  float tpl_total_h = 0.0F;
  if (m_selected_category_index < m_categories.size()) {
    tpl_total_h = static_cast<float>(m_categories[m_selected_category_index].templates.size()) * tpl_item_h;
  }
  const float tpl_max_scroll = (tpl_total_h > middle_h) ? (tpl_total_h - middle_h + 4.0F * dpi_scale) : 0.0F;
  const_cast<AddNewItemDialog *>(this)->m_template_scroll_offset =
      std::clamp(m_template_scroll_offset, 0.0F, tpl_max_scroll);

  layout.template_scroll_visible = (tpl_max_scroll > 0.0F);
  if (layout.template_scroll_visible) {
    layout.template_scrollbar_track = {cat_w + template_w - 6.0F * dpi_scale, middle_y + 2.0F * dpi_scale,
                                       4.0F * dpi_scale, middle_h - 4.0F * dpi_scale};
    const float thumb_h = std::max(20.0F * dpi_scale, (middle_h / tpl_total_h) * layout.template_scrollbar_track.height);
    const float thumb_y = layout.template_scrollbar_track.y +
                          (m_template_scroll_offset / tpl_max_scroll) * (layout.template_scrollbar_track.height - thumb_h);
    layout.template_scrollbar_thumb = {layout.template_scrollbar_track.x, thumb_y,
                                       layout.template_scrollbar_track.width, thumb_h};
  }

  float curr_tpl_y = middle_y - m_template_scroll_offset;
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
  SetTextColor(dc, RGB(220, 222, 228));
  SelectObject(dc, m_semibold_font);
  RECT title_text_r = native_title;
  title_text_r.left += static_cast<LONG>(34.0F * dpi_scale);
  const std::wstring title_str =
      L"Add New Item - " +
      Utility::utf8_to_wide(m_project_name).value_or(L"Project");
  DrawTextW(dc, title_str.c_str(), -1, &title_text_r,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, m_regular_font);

  // Close button (Native DWM / Custom Chrome style)
  if (m_close_hovered) {
    HBRUSH close_hover_br = CreateSolidBrush(to_color_ref(m_theme.close_hover));
    RECT close_r = to_native_rect(layout.close_button_bounds);
    FillRect(dc, &close_r, close_hover_br);
    DeleteObject(close_hover_br);
  }

  const int close_cx = static_cast<int>(
      layout.close_button_bounds.x + layout.close_button_bounds.width * 0.5F);
  const int close_cy = static_cast<int>(
      layout.close_button_bounds.y + layout.close_button_bounds.height * 0.5F);
  const int icon_size = static_cast<int>(10.0F * dpi_scale);
  const int half_size = icon_size / 2;

  HPEN close_pen = CreatePen(
      PS_SOLID, std::max(1, static_cast<int>(dpi_scale)),
      m_close_hovered ? RGB(255, 255, 255) : to_color_ref(m_theme.text_primary));
  HGDIOBJ prev_close_pen = SelectObject(dc, close_pen);

  MoveToEx(dc, close_cx - half_size, close_cy - half_size, nullptr);
  LineTo(dc, close_cx + half_size + 1, close_cy + half_size + 1);

  MoveToEx(dc, close_cx - half_size, close_cy + half_size, nullptr);
  LineTo(dc, close_cx + half_size + 1, close_cy - half_size - 1);

  SelectObject(dc, prev_close_pen);
  DeleteObject(close_pen);

  // 3. Vertical Separator Lines
  RECT cat_r = to_native_rect(layout.category_pane_bounds);
  MoveToEx(dc, cat_r.right, cat_r.top, nullptr);
  LineTo(dc, cat_r.right, cat_r.bottom);

  RECT tpl_r = to_native_rect(layout.template_pane_bounds);
  MoveToEx(dc, tpl_r.right, tpl_r.top, nullptr);
  LineTo(dc, tpl_r.right, tpl_r.bottom);

  const COLORREF select_blue = RGB(53, 132, 228); // JetBrains / ZDE Accent Blue
  const int icon_size_16 = static_cast<int>(16.0F * dpi_scale);

  // 4. Left Categories Pane with strict clipping
  {
    const int save_state = SaveDC(dc);
    IntersectClipRect(dc, cat_r.left, cat_r.top, cat_r.right, cat_r.bottom);

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

    RestoreDC(dc, save_state);

    // Draw Category Scrollbar (only if category list overflows pane)
    if (layout.category_scroll_visible) {
      RECT thumb_r = to_native_rect(layout.category_scrollbar_thumb);
      HBRUSH thumb_br = CreateSolidBrush(RGB(75, 78, 85));
      FillRect(dc, &thumb_r, thumb_br);
      DeleteObject(thumb_br);
    }
  }

  // 5. Middle Template Items Pane with strict clipping
  {
    const int save_state = SaveDC(dc);
    IntersectClipRect(dc, tpl_r.left, tpl_r.top, tpl_r.right, tpl_r.bottom);

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

    RestoreDC(dc, save_state);

    // Draw Template Scrollbar (only if template items overflow pane)
    if (layout.template_scroll_visible) {
      RECT thumb_r = to_native_rect(layout.template_scrollbar_thumb);
      HBRUSH thumb_br = CreateSolidBrush(RGB(75, 78, 85));
      FillRect(dc, &thumb_r, thumb_br);
      DeleteObject(thumb_br);
    }
  }

  // 6. Right Details Pane with strict clipping
  {
    RECT det_r = to_native_rect(layout.details_pane_bounds);
    const int save_state = SaveDC(dc);
    IntersectClipRect(dc, det_r.left, det_r.top, det_r.right, det_r.bottom);

    if (m_selected_category_index < m_categories.size()) {
      const auto &templates = m_categories[m_selected_category_index].templates;
      if (m_selected_template_index < templates.size()) {
        const auto &tpl = templates[m_selected_template_index];
        RECT inner_det = det_r;
        inner_det.left += static_cast<LONG>(14.0F * dpi_scale);
        inner_det.top += static_cast<LONG>(14.0F * dpi_scale);
        inner_det.right -= static_cast<LONG>(14.0F * dpi_scale);

        // Draw 24x24 Details Icon
        const int large_icon_size = static_cast<int>(24.0F * dpi_scale);
        draw_icon(dc, tpl.icon_path, inner_det.left, inner_det.top, large_icon_size);

        // Type header next to icon (using Semibold font)
        SelectObject(dc, m_semibold_font);
        SetTextColor(dc, RGB(220, 222, 228));
        RECT type_r = inner_det;
        type_r.left += static_cast<LONG>(32.0F * dpi_scale);
        type_r.top += static_cast<LONG>(3.0F * dpi_scale);
        const std::wstring type_str =
            L"Type: " + Utility::utf8_to_wide(tpl.category).value_or(L"");
        DrawTextW(dc, type_str.c_str(), -1, &type_r,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, m_regular_font);

        // Description text
        RECT desc_r = inner_det;
        desc_r.top += static_cast<LONG>(34.0F * dpi_scale);
        SetTextColor(dc, RGB(140, 144, 155));
        const std::wstring desc_str =
            Utility::utf8_to_wide(tpl.description).value_or(L"");
        DrawTextW(dc, desc_str.c_str(), -1, &desc_r,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
      }
    }

    RestoreDC(dc, save_state);
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
  const std::wstring fn_w =
      Utility::utf8_to_wide(m_filename_input).value_or(L"");

  // Draw Selection highlight behind text
  if (m_name_input_focused && has_selection()) {
    const auto [sel_start, sel_end] = get_selection_range();
    SIZE sz_before{};
    SIZE sz_sel{};
    const std::wstring part_before = fn_w.substr(0, std::min(sel_start, fn_w.size()));
    const std::wstring part_sel = fn_w.substr(sel_start, sel_end - sel_start);
    GetTextExtentPoint32W(dc, part_before.c_str(), static_cast<int>(part_before.size()), &sz_before);
    GetTextExtentPoint32W(dc, part_sel.c_str(), static_cast<int>(part_sel.size()), &sz_sel);

    RECT sel_rect{
        in_text_r.left + sz_before.cx,
        in_r.top + static_cast<LONG>(2.0F * dpi_scale),
        in_text_r.left + sz_before.cx + sz_sel.cx,
        in_r.bottom - static_cast<LONG>(2.0F * dpi_scale)
    };
    HBRUSH sel_br = CreateSolidBrush(RGB(38, 79, 120));
    FillRect(dc, &sel_rect, sel_br);
    DeleteObject(sel_br);
  }

  SetTextColor(dc, RGB(220, 222, 228));
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

    bool need_redraw = false;
    if (m_category_scroll_dragging || m_template_scroll_dragging) {
      need_redraw = handle_pointer_drag(x, y, layout);
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
    m_add_hovered = false;
    m_cancel_hovered = false;
    m_hovered_category_index.reset();
    m_hovered_template_index.reset();
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  }

  case WM_LBUTTONUP: {
    if (m_is_dragging_text) {
      m_is_dragging_text = false;
      ReleaseCapture();
      if (m_selection_anchor && *m_selection_anchor == m_caret_position) {
        m_selection_anchor.reset();
      }
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (m_category_scroll_dragging || m_template_scroll_dragging) {
      handle_pointer_release();
      ReleaseCapture();
      InvalidateRect(hwnd, nullptr, FALSE);
    }
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
    POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
    ScreenToClient(hwnd, &pt);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const auto layout =
        calculate_layout(static_cast<float>(rc.right - rc.left),
                         static_cast<float>(rc.bottom - rc.top), dpi_scale);

    if (handle_scroll(delta, static_cast<float>(pt.x), static_cast<float>(pt.y), layout)) {
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
  if (m_is_dragging_text) {
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const std::size_t idx = get_char_index_from_x(x, layout, dpi_scale);
    if (idx != m_caret_position) {
      m_caret_position = idx;
      return true;
    }
  }

  const bool old_close = m_close_hovered;
  const bool old_add = m_add_hovered;
  const bool old_cancel = m_cancel_hovered;
  const auto old_cat = m_hovered_category_index;
  const auto old_tpl = m_hovered_template_index;

  m_close_hovered = layout.close_button_bounds.contains(x, y);
  m_add_hovered = layout.add_button_bounds.contains(x, y);
  m_cancel_hovered = layout.cancel_button_bounds.contains(x, y);

  m_hovered_category_index.reset();
  if (layout.category_pane_bounds.contains(x, y)) {
    for (std::size_t i = 0; i < layout.category_item_bounds.size(); ++i) {
      if (layout.category_item_bounds[i].contains(x, y)) {
        m_hovered_category_index = i;
        break;
      }
    }
  }

  m_hovered_template_index.reset();
  if (layout.template_pane_bounds.contains(x, y)) {
    for (std::size_t i = 0; i < layout.template_item_bounds.size(); ++i) {
      if (layout.template_item_bounds[i].contains(x, y)) {
        m_hovered_template_index = i;
        break;
      }
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

  // Scrollbar click on category pane
  if (layout.category_scroll_visible && layout.category_scrollbar_thumb.contains(x, y)) {
    m_category_scroll_dragging = true;
    m_drag_start_y = y;
    m_drag_start_offset = m_category_scroll_offset;
    if (m_hwnd) SetCapture(m_hwnd);
    return true;
  }

  // Scrollbar click on template pane
  if (layout.template_scroll_visible && layout.template_scrollbar_thumb.contains(x, y)) {
    m_template_scroll_dragging = true;
    m_drag_start_y = y;
    m_drag_start_offset = m_template_scroll_offset;
    if (m_hwnd) SetCapture(m_hwnd);
    return true;
  }

  if (layout.name_input_bounds.contains(x, y)) {
    m_name_input_focused = true;
    const float dpi_scale = static_cast<float>(m_dpi) / 96.0F;
    const std::size_t idx = get_char_index_from_x(x, layout, dpi_scale);
    const bool is_shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (is_shift_down) {
      if (!m_selection_anchor) {
        m_selection_anchor = m_caret_position;
      }
      m_caret_position = idx;
    } else {
      m_caret_position = idx;
      m_selection_anchor = idx;
      m_is_dragging_text = true;
      if (m_hwnd) {
        SetCapture(m_hwnd);
      }
    }
    return true;
  }

  // Category click (only within category pane)
  if (layout.category_pane_bounds.contains(x, y)) {
    for (std::size_t i = 0; i < layout.category_item_bounds.size(); ++i) {
      if (layout.category_item_bounds[i].contains(x, y)) {
        m_selected_category_index = i;
        m_template_scroll_offset = 0.0F;
        select_template(0);
        return true;
      }
    }
  }

  // Template click (only within template pane)
  if (layout.template_pane_bounds.contains(x, y)) {
    for (std::size_t i = 0; i < layout.template_item_bounds.size(); ++i) {
      if (layout.template_item_bounds[i].contains(x, y)) {
        select_template(i);
        return true;
      }
    }
  }

  return true;
}

bool AddNewItemDialog::handle_pointer_drag(float x, float y,
                                           const LayoutResult &layout) {
  if (m_category_scroll_dragging && layout.category_scroll_visible) {
    const float dy = y - m_drag_start_y;
    const float track_usable_h = layout.category_scrollbar_track.height - layout.category_scrollbar_thumb.height;
    if (track_usable_h > 0.0F) {
      const float cat_total_h = static_cast<float>(m_categories.size()) * (28.0F * (static_cast<float>(m_dpi) / 96.0F));
      const float cat_max_scroll = std::max(0.0F, cat_total_h - layout.category_pane_bounds.height);
      m_category_scroll_offset = std::clamp(m_drag_start_offset + (dy / track_usable_h) * cat_max_scroll, 0.0F, cat_max_scroll);
      return true;
    }
  }

  if (m_template_scroll_dragging && layout.template_scroll_visible) {
    const float dy = y - m_drag_start_y;
    const float track_usable_h = layout.template_scrollbar_track.height - layout.template_scrollbar_thumb.height;
    if (track_usable_h > 0.0F && m_selected_category_index < m_categories.size()) {
      const float tpl_total_h = static_cast<float>(m_categories[m_selected_category_index].templates.size()) * (28.0F * (static_cast<float>(m_dpi) / 96.0F));
      const float tpl_max_scroll = std::max(0.0F, tpl_total_h - layout.template_pane_bounds.height);
      m_template_scroll_offset = std::clamp(m_drag_start_offset + (dy / track_usable_h) * tpl_max_scroll, 0.0F, tpl_max_scroll);
      return true;
    }
  }

  return false;
}

bool AddNewItemDialog::handle_pointer_release() {
  m_category_scroll_dragging = false;
  m_template_scroll_dragging = false;
  return true;
}

bool AddNewItemDialog::handle_double_click(float x, float y,
                                           const LayoutResult &layout) {
  if (layout.name_input_bounds.contains(x, y)) {
    m_name_input_focused = true;
    select_stem();
    return true;
  }

  // Double click on template immediately submits
  if (layout.template_pane_bounds.contains(x, y)) {
    for (std::size_t i = 0; i < layout.template_item_bounds.size(); ++i) {
      if (layout.template_item_bounds[i].contains(x, y)) {
        select_template(i);
        submit();
        return true;
      }
    }
  }
  return false;
}

bool AddNewItemDialog::handle_scroll(int delta, float mouse_x, float mouse_y,
                                     const LayoutResult &layout) {
  const float step = 32.0F;

  // Category pane scroll
  if (layout.category_pane_bounds.contains(mouse_x, mouse_y)) {
    if (!layout.category_scroll_visible) {
      return false; // Jangan scroll jika kategori masih sedikit
    }
    if (delta > 0) {
      m_category_scroll_offset = std::max(0.0F, m_category_scroll_offset - step);
    } else {
      m_category_scroll_offset += step;
    }
    return true;
  }

  // Template / middle pane scroll
  if (layout.template_pane_bounds.contains(mouse_x, mouse_y) ||
      layout.details_pane_bounds.contains(mouse_x, mouse_y)) {
    if (!layout.template_scroll_visible) {
      return false; // Jangan scroll jika template masih sedikit
    }
    if (delta > 0) {
      m_template_scroll_offset = std::max(0.0F, m_template_scroll_offset - step);
    } else {
      m_template_scroll_offset += step;
    }
    return true;
  }

  return false;
}

bool AddNewItemDialog::handle_text_input(std::string_view text) {
  if (!m_name_input_focused || text.empty())
    return false;
  if (has_selection()) {
    delete_selection();
  }
  for (char ch : text) {
    if (ch >= 32 && ch != 127) {
      m_filename_input.insert(m_caret_position, 1, ch);
      m_caret_position++;
    }
  }
  clear_selection();
  return true;
}

bool AddNewItemDialog::handle_key_down(WPARAM w_param) {
  const bool is_ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool is_shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

  if (w_param == VK_ESCAPE) {
    close();
    return true;
  }

  if (w_param == VK_RETURN) {
    submit();
    return true;
  }

  if (is_ctrl_down && (w_param == 'A' || w_param == 'a')) {
    select_all();
    return true;
  }

  if (is_ctrl_down && (w_param == 'C' || w_param == 'c')) {
    copy_selection_to_clipboard();
    return true;
  }

  if (is_ctrl_down && (w_param == 'X' || w_param == 'x')) {
    cut_selection_to_clipboard();
    return true;
  }

  if (is_ctrl_down && (w_param == 'V' || w_param == 'v')) {
    paste_from_clipboard();
    return true;
  }

  if (w_param == VK_BACK) {
    if (has_selection()) {
      delete_selection();
      return true;
    }
    if (!m_filename_input.empty() && m_caret_position > 0) {
      m_filename_input.erase(m_caret_position - 1, 1);
      m_caret_position--;
      clear_selection();
      return true;
    }
  }

  if (w_param == VK_DELETE) {
    if (has_selection()) {
      delete_selection();
      return true;
    }
    if (m_caret_position < m_filename_input.size()) {
      m_filename_input.erase(m_caret_position, 1);
      clear_selection();
      return true;
    }
  }

  if (w_param == VK_LEFT) {
    if (is_shift_down) {
      if (!m_selection_anchor) {
        m_selection_anchor = m_caret_position;
      }
      if (m_caret_position > 0) {
        m_caret_position--;
      }
    } else {
      if (has_selection()) {
        const auto [start, end] = get_selection_range();
        m_caret_position = start;
        clear_selection();
      } else if (m_caret_position > 0) {
        m_caret_position--;
      }
    }
    return true;
  }

  if (w_param == VK_RIGHT) {
    if (is_shift_down) {
      if (!m_selection_anchor) {
        m_selection_anchor = m_caret_position;
      }
      if (m_caret_position < m_filename_input.size()) {
        m_caret_position++;
      }
    } else {
      if (has_selection()) {
        const auto [start, end] = get_selection_range();
        m_caret_position = end;
        clear_selection();
      } else if (m_caret_position < m_filename_input.size()) {
        m_caret_position++;
      }
    }
    return true;
  }

  if (w_param == VK_HOME) {
    if (is_shift_down) {
      if (!m_selection_anchor) {
        m_selection_anchor = m_caret_position;
      }
      m_caret_position = 0;
    } else {
      m_caret_position = 0;
      clear_selection();
    }
    return true;
  }

  if (w_param == VK_END) {
    if (is_shift_down) {
      if (!m_selection_anchor) {
        m_selection_anchor = m_caret_position;
      }
      m_caret_position = m_filename_input.size();
    } else {
      m_caret_position = m_filename_input.size();
      clear_selection();
    }
    return true;
  }

  if (w_param == VK_UP) {
    if (m_selected_template_index > 0) {
      select_template(m_selected_template_index - 1);
      return true;
    }
  }

  if (w_param == VK_DOWN) {
    if (m_selected_category_index < m_categories.size()) {
      const auto &templates = m_categories[m_selected_category_index].templates;
      if (m_selected_template_index + 1 < templates.size()) {
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
