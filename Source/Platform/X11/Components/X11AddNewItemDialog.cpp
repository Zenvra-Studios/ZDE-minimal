#include "Platform/X11/Components/X11AddNewItemDialog.h"
#include "Utility/Fonts.h"
#include <X11/Xutil.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <lunasvg.h>
#include <sstream>


namespace Zenvra::Platform::X11::Components {

static unsigned long alloc_rgb(Display *dpy, int scr, unsigned char r,
                               unsigned char g, unsigned char b) {
  XColor c{};
  c.red = static_cast<unsigned short>(r * 257U);
  c.green = static_cast<unsigned short>(g * 257U);
  c.blue = static_cast<unsigned short>(b * 257U);
  c.flags = DoRed | DoGreen | DoBlue;
  if (XAllocColor(dpy, DefaultColormap(dpy, scr), &c) == 0) {
    return BlackPixel(dpy, scr);
  }
  return c.pixel;
}

X11AddNewItemDialog::X11AddNewItemDialog() { init_default_templates(); }

X11AddNewItemDialog::~X11AddNewItemDialog() {
  close();
  for (auto &pair : m_svg_cache) {
    if (pair.second) {
      XDestroyImage(pair.second);
    }
  }
  m_svg_cache.clear();
}

bool X11AddNewItemDialog::initialize(
    Display *display, int screen, float dpi_scale,
    const std::filesystem::path &icon_asset_root) {
  m_display = display;
  m_screen = screen;
  m_dpi_scale = std::max(dpi_scale, 1.0F);
  m_icon_asset_root = icon_asset_root;

  const int base_title_size = std::max(12, static_cast<int>(13.5F * m_dpi_scale));
  const int base_ui_size = std::max(10, static_cast<int>(12.0F * m_dpi_scale));
  const int base_small_size = std::max(9, static_cast<int>(11.0F * m_dpi_scale));

  char pattern[256]{};
  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
                base_title_size);
  m_title_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:weight=bold:antialias=true:hinting=true:hintstyle=hintslight",
                base_ui_size);
  m_bold_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
                base_ui_size);
  m_ui_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
                base_small_size);
  m_small_font = std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  return true;
}

void X11AddNewItemDialog::init_default_templates() {
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

  // 3. TypeScript & JavaScript Category
  TemplateCategory ts_cat{
      "typescript",
      "TypeScript / JavaScript",
      "Assets/icons/material-icon-theme/typescript.svg",
      {{"ts_file", "TypeScript File (.ts)", "index.ts", ".ts", "TypeScript",
        "Creates a modern TypeScript source file.",
        "Assets/icons/material-icon-theme/typescript.svg",
        "export interface AppConfig {\n    title: string;\n    version: string;\n}\n\nexport const config: AppConfig = {\n    title: \"ZDE Application\",\n    version: \"1.0.0\",\n};\n\nexport function bootstrap(): void {\n    console.log(`Starting ${config.title} v${config.version}...`);\n}\n\nbootstrap();\n"},
       {"js_file", "JavaScript File (.js)", "index.js", ".js", "JavaScript",
        "Creates a standard JavaScript source file.",
        "Assets/icons/material-icon-theme/javascript.svg",
        "// @ts-check\n\nexport function bootstrap() {\n    console.log('Hello from JavaScript in ZDE!');\n}\n\nbootstrap();\n"},
       {"mjs_file", "ES Module File (.mjs)", "index.mjs", ".mjs", "JavaScript",
        "Creates a modern ECMAScript module file.",
        "Assets/icons/material-icon-theme/javascript.svg",
        "import { promises as fs } from 'node:fs';\n\nexport async function bootstrap() {\n    console.log('Running ES Module in ZDE...');\n}\n\nawait bootstrap();\n"},
       {"tsx_file", "React Component (.tsx)", "Component.tsx", ".tsx", "TypeScript",
        "Creates a React functional component with TypeScript props.",
        "Assets/icons/material-icon-theme/react_ts.svg",
        "import React, { useState } from 'react';\n\nexport interface ComponentProps {\n    title?: string;\n}\n\nexport const Component: React.FC<ComponentProps> = ({\n    title = \"ZDE Component\",\n}) => {\n    const [count, setCount] = useState<number>(0);\n\n    return (\n        <div className=\"container\">\n            <h2>{title}</h2>\n            <button onClick={() => setCount(count + 1)}>Count: {count}</button>\n        </div>\n    );\n};\n\nexport default Component;\n"},
       {"jsx_file", "React Component (.jsx)", "Component.jsx", ".jsx", "JavaScript",
        "Creates a React functional component with JSX syntax.",
        "Assets/icons/material-icon-theme/react.svg",
        "import React, { useState } from 'react';\n\nexport const Component = ({ title = 'ZDE Component' }) => {\n    const [count, setCount] = useState(0);\n\n    return (\n        <div className=\"container\">\n            <h2>{title}</h2>\n            <button onClick={() => setCount(count + 1)}>Count: {count}</button>\n        </div>\n    );\n};\n\nexport default Component;\n"},
       {"dts_file", "TypeScript Declaration (.d.ts)", "types.d.ts", ".d.ts", "TypeScript",
        "Creates a TypeScript ambient declaration type definitions file.",
        "Assets/icons/material-icon-theme/typescript-def.svg",
        "declare namespace ZDE {\n    interface UserSession {\n        id: string;\n        username: string;\n        createdAt: Date;\n    }\n}\n"},
       {"tsconfig", "TSConfig (tsconfig.json)", "tsconfig.json", ".json", "TypeScript",
        "Creates a standard TypeScript compiler configuration file.",
        "Assets/icons/material-icon-theme/tsconfig.svg",
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

  // 5. Build & Config Category
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

  // 6. HTML & Web Category
  TemplateCategory web_cat{
      "web",
      "HTML & Web",
      "Assets/icons/material-icon-theme/html.svg",
      {{"html5_page", "HTML5 Page (.html)", "index.html", ".html", "HTML & Web",
        "Creates a modern HTML5 document structure with viewport and styling.",
        "Assets/icons/material-icon-theme/html.svg",
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    <meta charset=\"UTF-8\">\n    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n    <title>ZDE App</title>\n    <link rel=\"stylesheet\" href=\"style.css\">\n</head>\n<body>\n    <div class=\"container\">\n        <h1>Hello from ZDE!</h1>\n        <p>Built with native C++ power.</p>\n    </div>\n    <script src=\"main.js\"></script>\n</body>\n</html>\n"},
       {"css_style", "CSS Stylesheet (.css)", "style.css", ".css", "HTML & Web",
        "Creates a CSS stylesheet for HTML layouts.",
        "Assets/icons/material-icon-theme/css.svg",
        "* {\n    box-sizing: border-box;\n    margin: 0;\n    padding: 0;\n}\n\nbody {\n    font-family: system-ui, -apple-system, sans-serif;\n    background-color: #1e1e1e;\n    color: #ffffff;\n    padding: 2rem;\n}\n"},
       {"svg_graphic", "SVG Vector Graphic (.svg)", "graphic.svg", ".svg", "HTML & Web",
        "Creates an SVG scalable vector graphics XML file.",
        "Assets/icons/material-icon-theme/svg.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n    <circle cx=\"12\" cy=\"12\" r=\"10\"></circle>\n</svg>\n"}}};

  // 7. General Category
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
  m_categories.push_back(ts_cat);
  m_categories.push_back(shader_cat);
  m_categories.push_back(build_cat);
  m_categories.push_back(web_cat);
  m_categories.push_back(gen_cat);
}

void X11AddNewItemDialog::select_category(std::size_t index) {
  if (index >= m_categories.size()) {
    return;
  }
  m_selected_category_index = index;
  m_selected_template_index = 0;
  if (!m_categories[index].templates.empty()) {
    m_filename_input = m_categories[index].templates[0].default_filename;
  }
}

void X11AddNewItemDialog::select_template(std::size_t index) {
  if (m_selected_category_index >= m_categories.size()) {
    return;
  }
  const auto &tpls = m_categories[m_selected_category_index].templates;
  if (index >= tpls.size()) {
    return;
  }
  m_selected_template_index = index;
  m_filename_input = tpls[index].default_filename;
}

void X11AddNewItemDialog::open(Window parent_window,
                               const std::filesystem::path &target_folder,
                               const std::string &project_name,
                               CreateCallback callback) {
  close();

  m_parent_window = parent_window;
  m_target_folder = target_folder.empty() ? std::filesystem::current_path() : target_folder;
  std::error_code ec;
  if (!std::filesystem::is_directory(m_target_folder, ec)) {
    m_target_folder = m_target_folder.parent_path();
  }
  m_project_name = project_name.empty() ? "Project" : project_name;
  m_callback = std::move(callback);

  select_category(0);

  const float scale = m_dpi_scale;
  m_width = static_cast<int>(820.0F * scale);
  m_height = static_cast<int>(520.0F * scale);

  XWindowAttributes parent_attrs{};
  XGetWindowAttributes(m_display, parent_window, &parent_attrs);

  int root_x = 0;
  int root_y = 0;
  Window child = 0;
  XTranslateCoordinates(m_display, parent_window,
                        RootWindow(m_display, m_screen), 0, 0, &root_x, &root_y,
                        &child);

  m_win_x = root_x + (parent_attrs.width - m_width) / 2;
  m_win_y = root_y + (parent_attrs.height - m_height) / 2;

  const int screen_w = DisplayWidth(m_display, m_screen);
  const int screen_h = DisplayHeight(m_display, m_screen);
  m_win_x = std::clamp(m_win_x, 0, std::max(screen_w - m_width, 0));
  m_win_y = std::clamp(m_win_y, 0, std::max(screen_h - m_height, 0));

  XSetWindowAttributes attrs{};
  attrs.override_redirect = True;
  attrs.background_pixel = alloc_rgb(m_display, m_screen, 30, 31, 34);
  attrs.save_under = True;
  attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | KeyPressMask | LeaveWindowMask |
                     FocusChangeMask;

  m_window = XCreateWindow(
      m_display, RootWindow(m_display, m_screen), m_win_x, m_win_y,
      static_cast<unsigned int>(m_width), static_cast<unsigned int>(m_height),
      0, DefaultDepth(m_display, m_screen), InputOutput,
      DefaultVisual(m_display, m_screen),
      CWOverrideRedirect | CWBackPixel | CWSaveUnder | CWEventMask, &attrs);

  m_back_buffer = XCreatePixmap(
      m_display, m_window, static_cast<unsigned int>(m_width),
      static_cast<unsigned int>(m_height),
      static_cast<unsigned int>(DefaultDepth(m_display, m_screen)));

  m_gc = XCreateGC(m_display, m_window, 0, nullptr);

  // Calculate layout rects
  const float titlebar_h = 34.0F * scale;
  const float footer_h = 70.0F * scale;
  const float pane_top = titlebar_h;
  const float pane_h = static_cast<float>(m_height) - titlebar_h - footer_h;
  const float cat_w = 200.0F * scale;
  const float tpl_w = 340.0F * scale;

  m_titlebar_rect = {0.0F, 0.0F, static_cast<float>(m_width), titlebar_h};
  m_close_btn_rect = {static_cast<float>(m_width) - 46.0F * scale, 0.0F,
                      46.0F * scale, titlebar_h};

  m_category_pane_rect = {0.0F, pane_top, cat_w, pane_h};
  m_template_pane_rect = {cat_w, pane_top, tpl_w, pane_h};
  m_details_pane_rect = {cat_w + tpl_w, pane_top,
                         static_cast<float>(m_width) - (cat_w + tpl_w), pane_h};

  const float footer_top = static_cast<float>(m_height) - footer_h;
  m_name_input_rect = {76.0F * scale, footer_top + 10.0F * scale,
                       static_cast<float>(m_width) - 260.0F * scale,
                       26.0F * scale};
  m_add_btn_rect = {static_cast<float>(m_width) - 170.0F * scale,
                    footer_top + 10.0F * scale, 75.0F * scale, 26.0F * scale};
  m_cancel_btn_rect = {static_cast<float>(m_width) - 85.0F * scale,
                       footer_top + 10.0F * scale, 75.0F * scale,
                       26.0F * scale};

  m_open = true;
  m_close_hovered = false;
  m_add_hovered = false;
  m_cancel_hovered = false;
  m_name_input_focused = true;

  XMapRaised(m_display, m_window);
  XSetInputFocus(m_display, m_window, RevertToParent, CurrentTime);
  render();
}

void X11AddNewItemDialog::close() {
  if (m_open && m_display != nullptr) {
    if (m_window != 0) {
      XDestroyWindow(m_display, m_window);
      m_window = 0;
    }
    if (m_back_buffer != 0) {
      XFreePixmap(m_display, m_back_buffer);
      m_back_buffer = 0;
    }
    if (m_gc != nullptr) {
      XFreeGC(m_display, m_gc);
      m_gc = nullptr;
    }
    m_open = false;
  }
}

void X11AddNewItemDialog::submit() {
  if (m_filename_input.empty()) {
    return;
  }

  std::string initial_content;
  if (m_selected_category_index < m_categories.size()) {
    const auto &tpls = m_categories[m_selected_category_index].templates;
    if (m_selected_template_index < tpls.size()) {
      initial_content = tpls[m_selected_template_index].default_content;
    }
  }

  std::string filename = m_filename_input;
  auto cb = std::move(m_callback);
  close();
  if (cb) {
    cb(filename, initial_content);
  }
}

void X11AddNewItemDialog::draw_icon(Drawable drawable, const std::string &path,
                                    int x, int y, int size, uint8_t bg_r,
                                    uint8_t bg_g, uint8_t bg_b) {
  if (size <= 0 || m_display == nullptr || path.empty()) {
    return;
  }

  std::filesystem::path resolved_path{path};
  if (!std::filesystem::exists(resolved_path)) {
    if (!m_icon_asset_root.empty()) {
      if (std::filesystem::exists(m_icon_asset_root / resolved_path)) {
        resolved_path = m_icon_asset_root / resolved_path;
      } else if (resolved_path.string().starts_with("Assets/icons/")) {
        const auto sub = resolved_path.string().substr(13);
        if (std::filesystem::exists(m_icon_asset_root / sub)) {
          resolved_path = m_icon_asset_root / sub;
        }
      } else if (resolved_path.string().starts_with("Assets/")) {
        const auto sub = resolved_path.string().substr(7);
        if (std::filesystem::exists(m_icon_asset_root.parent_path() / "Assets" /
                                    sub)) {
          resolved_path = m_icon_asset_root.parent_path() / "Assets" / sub;
        }
      }
    }
  }
  if (!std::filesystem::exists(resolved_path)) {
    const auto cwd_p = std::filesystem::current_path() / path;
    if (std::filesystem::exists(cwd_p)) {
      resolved_path = cwd_p;
    }
  }

  if (!std::filesystem::exists(resolved_path)) {
    return;
  }

  const std::string cache_key =
      resolved_path.string() + "@" + std::to_string(size) + "_" +
      std::to_string(bg_r) + "_" + std::to_string(bg_g) + "_" +
      std::to_string(bg_b);
  XImage *image = nullptr;
  auto it = m_svg_cache.find(cache_key);
  if (it != m_svg_cache.end()) {
    image = it->second;
  } else {
    auto document = lunasvg::Document::loadFromFile(resolved_path.string());
    if (!document) {
      return;
    }
    auto bitmap = document->renderToBitmap(static_cast<std::uint32_t>(size),
                                           static_cast<std::uint32_t>(size));
    if (bitmap.isNull()) {
      return;
    }

    char *x11_data = static_cast<char *>(std::malloc(size * size * 4));
    if (!x11_data) {
      return;
    }

    const uint32_t *src = reinterpret_cast<const uint32_t *>(bitmap.data());
    uint32_t *dst = reinterpret_cast<uint32_t *>(x11_data);

    for (int i = 0; i < size * size; ++i) {
      uint32_t pixel = src[i];
      uint32_t a = (pixel >> 24) & 0xFF;
      uint32_t source_r = (pixel >> 16) & 0xFF;
      uint32_t source_g = (pixel >> 8) & 0xFF;
      uint32_t source_b = pixel & 0xFF;

      uint32_t out_r =
          source_r + (static_cast<uint32_t>(bg_r) * (255 - a)) / 255;
      uint32_t out_g =
          source_g + (static_cast<uint32_t>(bg_g) * (255 - a)) / 255;
      uint32_t out_b =
          source_b + (static_cast<uint32_t>(bg_b) * (255 - a)) / 255;

      dst[i] = (out_r << 16) | (out_g << 8) | out_b;
    }

    image = XCreateImage(
        m_display, DefaultVisual(m_display, m_screen),
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)), ZPixmap,
        0, x11_data, size, size, 32, 0);
    if (image) {
      m_svg_cache[cache_key] = image;
    }
  }

  if (image) {
    XPutImage(m_display, drawable, m_gc, image, 0, 0, x, y, size, size);
  }
}

void X11AddNewItemDialog::render() {
  if (!m_open || m_display == nullptr || m_back_buffer == 0) {
    return;
  }

  const float scale = m_dpi_scale;
  const unsigned long bg_col = alloc_rgb(m_display, m_screen, 30, 31, 34);
  const unsigned long titlebar_bg = alloc_rgb(m_display, m_screen, 29, 30, 33);
  const unsigned long border_col = alloc_rgb(m_display, m_screen, 48, 50, 55);
  const unsigned long sep_col = alloc_rgb(m_display, m_screen, 48, 50, 55);
  const unsigned long sel_cat_blue =
      alloc_rgb(m_display, m_screen, 53, 132, 228);
  const unsigned long sel_tpl_blue =
      alloc_rgb(m_display, m_screen, 53, 132, 228);
  const unsigned long hov_item_bg =
      alloc_rgb(m_display, m_screen, 45, 47, 52);
  const unsigned long input_bg = alloc_rgb(m_display, m_screen, 24, 25, 28);
  const unsigned long input_border =
      alloc_rgb(m_display, m_screen, 53, 132, 228);

  // 1. Fill whole dialog background
  XSetForeground(m_display, m_gc, bg_col);
  XFillRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                 static_cast<unsigned int>(m_width),
                 static_cast<unsigned int>(m_height));

  // 2. Titlebar Background & Titlebar bottom line
  XSetForeground(m_display, m_gc, titlebar_bg);
  XFillRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                 static_cast<unsigned int>(m_width),
                 static_cast<unsigned int>(m_titlebar_rect.height));
  XSetForeground(m_display, m_gc, sep_col);
  XDrawLine(m_display, m_back_buffer, m_gc, 0,
            static_cast<int>(m_titlebar_rect.height - 1), m_width,
            static_cast<int>(m_titlebar_rect.height - 1));

  // Titlebar Icon
  draw_icon(m_back_buffer, "Assets/icons/material-icon-theme/document.svg",
            static_cast<int>(12.0F * scale), static_cast<int>(9.0F * scale),
            static_cast<int>(16.0F * scale), 29, 30, 33);

  // Titlebar Title
  if (m_title_font && m_title_font->isValid()) {
    const std::string full_title = "Add New Item - " + m_project_name;
    m_title_font->drawString(m_back_buffer, "#cccccc",
                             static_cast<int>(36.0F * scale),
                             static_cast<int>(21.0F * scale), full_title);
  }

  // Titlebar Close Button (Identical to Main Window Custom Chrome)
  if (m_close_hovered) {
    XSetForeground(m_display, m_gc,
                   alloc_rgb(m_display, m_screen, 232, 17, 35));
    XFillRectangle(m_display, m_back_buffer, m_gc,
                   static_cast<int>(m_close_btn_rect.x),
                   static_cast<int>(m_close_btn_rect.y),
                   static_cast<unsigned int>(m_close_btn_rect.width),
                   static_cast<unsigned int>(m_close_btn_rect.height));
  }
  const int close_cx =
      static_cast<int>(m_close_btn_rect.x + m_close_btn_rect.width * 0.5F);
  const int close_cy =
      static_cast<int>(m_close_btn_rect.y + m_close_btn_rect.height * 0.5F);
  const int close_half = std::max(static_cast<int>(5.0F * scale), 4);
  const int line_width = std::max(static_cast<int>(scale), 1);

  XSetForeground(m_display, m_gc,
                 m_close_hovered
                     ? alloc_rgb(m_display, m_screen, 255, 255, 255)
                     : alloc_rgb(m_display, m_screen, 204, 204, 204));
  XSetLineAttributes(m_display, m_gc, line_width, LineSolid, CapProjecting,
                     JoinMiter);
  XDrawLine(m_display, m_back_buffer, m_gc, close_cx - close_half,
            close_cy - close_half, close_cx + close_half,
            close_cy + close_half);
  XDrawLine(m_display, m_back_buffer, m_gc, close_cx - close_half,
            close_cy + close_half, close_cx + close_half,
            close_cy - close_half);
  XSetLineAttributes(m_display, m_gc, 1, LineSolid, CapButt, JoinMiter);

  // 3. Vertical Separator Lines Between Panes
  const int pane_top_y = static_cast<int>(m_titlebar_rect.height);
  const int pane_bottom_y =
      static_cast<int>(m_titlebar_rect.height + m_category_pane_rect.height);
  XSetForeground(m_display, m_gc, sep_col);
  XDrawLine(m_display, m_back_buffer, m_gc,
            static_cast<int>(m_category_pane_rect.right()), pane_top_y,
            static_cast<int>(m_category_pane_rect.right()), pane_bottom_y);
  XDrawLine(m_display, m_back_buffer, m_gc,
            static_cast<int>(m_template_pane_rect.right()), pane_top_y,
            static_cast<int>(m_template_pane_rect.right()), pane_bottom_y);

  // 4. Left Categories Pane
  m_category_item_rects.clear();
  const float cat_item_h = 28.0F * scale;
  float cat_y = m_category_pane_rect.y;

  for (std::size_t i = 0; i < m_categories.size(); ++i) {
    const auto &cat = m_categories[i];
    UI::Rect item_rect{m_category_pane_rect.x, cat_y,
                       m_category_pane_rect.width, cat_item_h};
    m_category_item_rects.push_back(item_rect);

    const bool is_sel = (i == m_selected_category_index);
    const bool is_hov =
        (m_hovered_category_index && *m_hovered_category_index == i);

    if (is_sel) {
      XSetForeground(m_display, m_gc, sel_cat_blue);
      XFillRectangle(m_display, m_back_buffer, m_gc,
                     static_cast<int>(item_rect.x),
                     static_cast<int>(item_rect.y),
                     static_cast<unsigned int>(item_rect.width),
                     static_cast<unsigned int>(item_rect.height));
    } else if (is_hov) {
      XSetForeground(m_display, m_gc, hov_item_bg);
      XFillRectangle(m_display, m_back_buffer, m_gc,
                     static_cast<int>(item_rect.x),
                     static_cast<int>(item_rect.y),
                     static_cast<unsigned int>(item_rect.width),
                     static_cast<unsigned int>(item_rect.height));
    }

    const uint8_t bg_r = is_sel ? 0 : (is_hov ? 45 : 31);
    const uint8_t bg_g = is_sel ? 122 : (is_hov ? 45 : 31);
    const uint8_t bg_b = is_sel ? 204 : (is_hov ? 45 : 31);
    draw_icon(m_back_buffer, cat.icon_path,
              static_cast<int>(item_rect.x + 12.0F * scale),
              static_cast<int>(item_rect.y + 6.0F * scale),
              static_cast<int>(16.0F * scale), bg_r, bg_g, bg_b);

    if (m_ui_font && m_ui_font->isValid()) {
      m_ui_font->drawString(m_back_buffer, is_sel ? "#ffffff" : "#cdcdcd",
                            static_cast<int>(item_rect.x + 36.0F * scale),
                            static_cast<int>(item_rect.y + 19.0F * scale),
                            cat.name);
    }

    cat_y += cat_item_h;
  }

  // 5. Middle Template Items Pane
  m_template_item_rects.clear();
  const float tpl_item_h = 28.0F * scale;
  float tpl_y = m_template_pane_rect.y;

  if (m_selected_category_index < m_categories.size()) {
    const auto &tpls = m_categories[m_selected_category_index].templates;
    for (std::size_t i = 0; i < tpls.size(); ++i) {
      const auto &tpl = tpls[i];
      UI::Rect item_rect{m_template_pane_rect.x, tpl_y,
                         m_template_pane_rect.width, tpl_item_h};
      m_template_item_rects.push_back(item_rect);

      const bool is_sel = (i == m_selected_template_index);
      const bool is_hov =
          (m_hovered_template_index && *m_hovered_template_index == i);

      if (is_sel) {
        XSetForeground(m_display, m_gc, sel_tpl_blue);
        XFillRectangle(m_display, m_back_buffer, m_gc,
                       static_cast<int>(item_rect.x),
                       static_cast<int>(item_rect.y),
                       static_cast<unsigned int>(item_rect.width),
                       static_cast<unsigned int>(item_rect.height));
      } else if (is_hov) {
        XSetForeground(m_display, m_gc, hov_item_bg);
        XFillRectangle(m_display, m_back_buffer, m_gc,
                       static_cast<int>(item_rect.x),
                       static_cast<int>(item_rect.y),
                       static_cast<unsigned int>(item_rect.width),
                       static_cast<unsigned int>(item_rect.height));
      }

      const uint8_t bg_r = is_sel ? 0 : (is_hov ? 40 : 27);
      const uint8_t bg_g = is_sel ? 122 : (is_hov ? 40 : 27);
      const uint8_t bg_b = is_sel ? 204 : (is_hov ? 44 : 30);
      draw_icon(m_back_buffer, tpl.icon_path,
                static_cast<int>(item_rect.x + 12.0F * scale),
                static_cast<int>(item_rect.y + 6.0F * scale),
                static_cast<int>(16.0F * scale), bg_r, bg_g, bg_b);

      if (m_ui_font && m_ui_font->isValid()) {
        m_ui_font->drawString(m_back_buffer, is_sel ? "#ffffff" : "#dcdcdc",
                              static_cast<int>(item_rect.x + 36.0F * scale),
                              static_cast<int>(item_rect.y + 19.0F * scale),
                              tpl.name);

        // Right category tag
        if (m_small_font && m_small_font->isValid()) {
          const int cat_w = m_small_font->getTextWidth(tpl.category);
          m_small_font->drawString(
              m_back_buffer, is_sel ? "#d0e6ff" : "#787884",
              static_cast<int>(item_rect.right() - cat_w - 14.0F * scale),
              static_cast<int>(item_rect.y + 19.0F * scale), tpl.category);
        }
      }

      tpl_y += tpl_item_h;
    }
  }

  // 6. Right Details Pane
  if (m_selected_category_index < m_categories.size()) {
    const auto &tpls = m_categories[m_selected_category_index].templates;
    if (m_selected_template_index < tpls.size()) {
      const auto &tpl = tpls[m_selected_template_index];

      draw_icon(m_back_buffer, tpl.icon_path,
                static_cast<int>(m_details_pane_rect.x + 16.0F * scale),
                static_cast<int>(m_details_pane_rect.y + 16.0F * scale),
                static_cast<int>(24.0F * scale), 27, 27, 30);

      if (m_bold_font && m_bold_font->isValid()) {
        m_bold_font->drawString(
            m_back_buffer, "#ffffff",
            static_cast<int>(m_details_pane_rect.x + 48.0F * scale),
            static_cast<int>(m_details_pane_rect.y + 32.0F * scale),
            "Type: " + tpl.category);
      }

      if (m_ui_font && m_ui_font->isValid()) {
        std::string desc = tpl.description;
        std::vector<std::string> lines;
        std::string current_line;
        std::istringstream stream(desc);
        std::string word;
        const int max_desc_w =
            static_cast<int>(m_details_pane_rect.width - 32.0F * scale);

        while (stream >> word) {
          std::string test_line =
              current_line.empty() ? word : current_line + " " + word;
          if (m_ui_font->getTextWidth(test_line) > max_desc_w &&
              !current_line.empty()) {
            lines.push_back(current_line);
            current_line = word;
          } else {
            current_line = test_line;
          }
        }
        if (!current_line.empty()) {
          lines.push_back(current_line);
        }

        int dy = static_cast<int>(m_details_pane_rect.y + 60.0F * scale);
        for (const auto &line : lines) {
          m_ui_font->drawString(
              m_back_buffer, "#a0a0aa",
              static_cast<int>(m_details_pane_rect.x + 16.0F * scale), dy,
              line);
          dy += static_cast<int>(18.0F * scale);
        }
      }
    }
  }

  // 7. Footer Panel & Horizontal Separator Line
  const float footer_top = static_cast<float>(m_height) - 70.0F * scale;
  XSetForeground(m_display, m_gc, sep_col);
  XDrawLine(m_display, m_back_buffer, m_gc, 0, static_cast<int>(footer_top),
            m_width, static_cast<int>(footer_top));

  // Name Label
  if (m_ui_font && m_ui_font->isValid()) {
    m_ui_font->drawString(
        m_back_buffer, "#b4b4b4", static_cast<int>(18.0F * scale),
        static_cast<int>(footer_top + 24.0F * scale), "Name:");
  }

  // Name Input Box
  m_name_input_rect = {76.0F * scale, footer_top + 10.0F * scale,
                       static_cast<float>(m_width) - 260.0F * scale,
                       26.0F * scale};
  m_add_btn_rect = {static_cast<float>(m_width) - 170.0F * scale,
                    footer_top + 10.0F * scale, 75.0F * scale, 26.0F * scale};
  m_cancel_btn_rect = {static_cast<float>(m_width) - 85.0F * scale,
                       footer_top + 10.0F * scale, 75.0F * scale,
                       26.0F * scale};

  XSetForeground(m_display, m_gc, input_bg);
  XFillRectangle(m_display, m_back_buffer, m_gc,
                 static_cast<int>(m_name_input_rect.x),
                 static_cast<int>(m_name_input_rect.y),
                 static_cast<unsigned int>(m_name_input_rect.width),
                 static_cast<unsigned int>(m_name_input_rect.height));
  XSetForeground(m_display, m_gc, input_border);
  XDrawRectangle(m_display, m_back_buffer, m_gc,
                 static_cast<int>(m_name_input_rect.x),
                 static_cast<int>(m_name_input_rect.y),
                 static_cast<unsigned int>(m_name_input_rect.width),
                 static_cast<unsigned int>(m_name_input_rect.height));

  const int text_x = static_cast<int>(m_name_input_rect.x + 8.0F * scale);
  const int text_y = static_cast<int>(m_name_input_rect.y + 18.0F * scale);

  if (m_ui_font && m_ui_font->isValid()) {
    m_ui_font->drawString(m_back_buffer, "#ffffff", text_x, text_y,
                          m_filename_input);

    // Caret
    const int text_w = m_ui_font->getTextWidth(m_filename_input);
    const int caret_x = text_x + text_w;
    XSetForeground(m_display, m_gc,
                   alloc_rgb(m_display, m_screen, 255, 255, 255));
    XDrawLine(m_display, m_back_buffer, m_gc, caret_x,
              static_cast<int>(m_name_input_rect.y + 4.0F * scale), caret_x,
              static_cast<int>(m_name_input_rect.bottom() - 4.0F * scale));
  }

  // Location label & value
  if (m_small_font && m_small_font->isValid()) {
    m_small_font->drawString(
        m_back_buffer, "#888890", static_cast<int>(18.0F * scale),
        static_cast<int>(footer_top + 52.0F * scale), "Location:");

    std::string loc_str = m_target_folder.string();
    const int max_loc_w = static_cast<int>(static_cast<float>(m_width) -
                                           (76.0F * scale + 24.0F * scale));
    if (m_small_font->getTextWidth(loc_str) > max_loc_w &&
        loc_str.size() > 10) {
      while (loc_str.size() > 10 &&
             m_small_font->getTextWidth("..." + loc_str) > max_loc_w) {
        loc_str = loc_str.substr(1);
      }
      loc_str = "..." + loc_str;
    }

    m_small_font->drawString(
        m_back_buffer, "#a0a0b0", static_cast<int>(76.0F * scale),
        static_cast<int>(footer_top + 52.0F * scale), loc_str);
  }

  // Add Button (Synced Theme Accent Blue)
  const unsigned long add_bg =
      m_add_hovered ? alloc_rgb(m_display, m_screen, 28, 151, 234)
                    : sel_tpl_blue;
  XSetForeground(m_display, m_gc, add_bg);
  XFillRectangle(m_display, m_back_buffer, m_gc,
                 static_cast<int>(m_add_btn_rect.x),
                 static_cast<int>(m_add_btn_rect.y),
                 static_cast<unsigned int>(m_add_btn_rect.width),
                 static_cast<unsigned int>(m_add_btn_rect.height));
  if (m_ui_font && m_ui_font->isValid()) {
    m_ui_font->drawString(m_back_buffer, "#ffffff",
                          static_cast<int>(m_add_btn_rect.x + 25.0F * scale),
                          static_cast<int>(m_add_btn_rect.y + 18.0F * scale),
                          "Add");
  }

  // Cancel Button (Flat dark gray with subtle border)
  const unsigned long cancel_bg =
      m_cancel_hovered ? alloc_rgb(m_display, m_screen, 45, 47, 52)
                       : alloc_rgb(m_display, m_screen, 36, 37, 42);
  XSetForeground(m_display, m_gc, cancel_bg);
  XFillRectangle(m_display, m_back_buffer, m_gc,
                 static_cast<int>(m_cancel_btn_rect.x),
                 static_cast<int>(m_cancel_btn_rect.y),
                 static_cast<unsigned int>(m_cancel_btn_rect.width),
                 static_cast<unsigned int>(m_cancel_btn_rect.height));
  XSetForeground(m_display, m_gc, border_col);
  XDrawRectangle(m_display, m_back_buffer, m_gc,
                 static_cast<int>(m_cancel_btn_rect.x),
                 static_cast<int>(m_cancel_btn_rect.y),
                 static_cast<unsigned int>(m_cancel_btn_rect.width),
                 static_cast<unsigned int>(m_cancel_btn_rect.height));
  if (m_ui_font && m_ui_font->isValid()) {
    m_ui_font->drawString(m_back_buffer, "#d0d0d0",
                          static_cast<int>(m_cancel_btn_rect.x + 16.0F * scale),
                          static_cast<int>(m_cancel_btn_rect.y + 18.0F * scale),
                          "Cancel");
  }

  // 8. Outer 1px Window Border
  XSetForeground(m_display, m_gc, border_col);
  XDrawRectangle(m_display, m_back_buffer, m_gc, 0, 0,
                 static_cast<unsigned int>(m_width - 1),
                 static_cast<unsigned int>(m_height - 1));

  // Blit to window
  XCopyArea(m_display, m_back_buffer, m_window, m_gc, 0, 0,
            static_cast<unsigned int>(m_width),
            static_cast<unsigned int>(m_height), 0, 0);
  XFlush(m_display);
}

bool X11AddNewItemDialog::handle_event(const XEvent &event) {
  if (!m_open || event.xany.window != m_window) {
    return false;
  }

  switch (event.type) {
  case Expose:
    render();
    return true;

  case MotionNotify: {
    const float mx = static_cast<float>(event.xmotion.x);
    const float my = static_cast<float>(event.xmotion.y);

    if (m_dragging_titlebar) {
      const int dx = event.xmotion.x_root - m_drag_start_root_x;
      const int dy = event.xmotion.y_root - m_drag_start_root_y;
      m_win_x = m_drag_start_win_x + dx;
      m_win_y = m_drag_start_win_y + dy;
      XMoveWindow(m_display, m_window, m_win_x, m_win_y);
      return true;
    }

    const bool new_close_h = m_close_btn_rect.contains(mx, my);
    const bool new_add_h = m_add_btn_rect.contains(mx, my);
    const bool new_cancel_h = m_cancel_btn_rect.contains(mx, my);

    std::optional<std::size_t> new_cat_h;
    for (std::size_t i = 0; i < m_category_item_rects.size(); ++i) {
      if (m_category_item_rects[i].contains(mx, my)) {
        new_cat_h = i;
        break;
      }
    }

    std::optional<std::size_t> new_tpl_h;
    for (std::size_t i = 0; i < m_template_item_rects.size(); ++i) {
      if (m_template_item_rects[i].contains(mx, my)) {
        new_tpl_h = i;
        break;
      }
    }

    if (new_close_h != m_close_hovered || new_add_h != m_add_hovered ||
        new_cancel_h != m_cancel_hovered ||
        new_cat_h != m_hovered_category_index ||
        new_tpl_h != m_hovered_template_index) {
      m_close_hovered = new_close_h;
      m_add_hovered = new_add_h;
      m_cancel_hovered = new_cancel_h;
      m_hovered_category_index = new_cat_h;
      m_hovered_template_index = new_tpl_h;
      render();
    }
    return true;
  }

  case ButtonPress: {
    const float bx = static_cast<float>(event.xbutton.x);
    const float by = static_cast<float>(event.xbutton.y);

    if (m_close_btn_rect.contains(bx, by)) {
      close();
      return true;
    }
    if (m_cancel_btn_rect.contains(bx, by)) {
      close();
      return true;
    }
    if (m_add_btn_rect.contains(bx, by)) {
      submit();
      return true;
    }

    // Category click
    for (std::size_t i = 0; i < m_category_item_rects.size(); ++i) {
      if (m_category_item_rects[i].contains(bx, by)) {
        select_category(i);
        render();
        return true;
      }
    }

    // Template click / double click
    for (std::size_t i = 0; i < m_template_item_rects.size(); ++i) {
      if (m_template_item_rects[i].contains(bx, by)) {
        const Time now = event.xbutton.time;
        if (m_selected_template_index == i && (now - m_last_click_time < 400)) {
          submit();
          return true;
        }
        m_last_click_time = now;
        m_last_clicked_template_index = i;
        select_template(i);
        render();
        return true;
      }
    }

    // Titlebar dragging
    if (m_titlebar_rect.contains(bx, by)) {
      m_dragging_titlebar = true;
      m_drag_start_root_x = event.xbutton.x_root;
      m_drag_start_root_y = event.xbutton.y_root;
      m_drag_start_win_x = m_win_x;
      m_drag_start_win_y = m_win_y;
      return true;
    }
    return true;
  }

  case ButtonRelease:
    m_dragging_titlebar = false;
    return true;

  case KeyPress: {
    KeySym sym = XLookupKeysym(const_cast<XKeyEvent *>(&event.xkey), 0);
    if (sym == XK_Escape) {
      close();
      return true;
    }
    if (sym == XK_Return || sym == XK_KP_Enter) {
      submit();
      return true;
    }
    if (sym == XK_BackSpace) {
      if (!m_filename_input.empty()) {
        m_filename_input.pop_back();
        render();
      }
      return true;
    }

    char buf[32]{};
    int len = XLookupString(const_cast<XKeyEvent *>(&event.xkey), buf,
                            sizeof(buf), nullptr, nullptr);
    if (len > 0 && !std::iscntrl(static_cast<unsigned char>(buf[0]))) {
      m_filename_input.append(buf, len);
      render();
      return true;
    }
    return true;
  }

  default:
    break;
  }

  return true;
}

} // namespace Zenvra::Platform::X11::Components
