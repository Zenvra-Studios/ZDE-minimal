#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::UI::Editor {

namespace Detail {

inline std::string lowercase_ascii(std::string value) {
  for (char &character : value) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

inline std::string symbol_icon(std::string_view icon_name) {
  std::string asset = "vscode-symbols/files/";
  asset.append(icon_name);
  asset.append(".svg");
  return asset;
}

inline std::string shader_icon() { return "material-icon-theme/shader.svg"; }
inline std::string assembly_icon() { return "material-icon-theme/assembly.svg"; }

} // namespace Detail

// Returns a path relative to Assets/icons. UI chrome and folder icons continue
// to use standard folder assets.
[[nodiscard]] inline std::string
file_icon_asset_for_path(const std::filesystem::path &path) {
  const std::string filename =
      Detail::lowercase_ascii(path.filename().string());
  const std::string extension =
      Detail::lowercase_ascii(path.extension().string());

  // Well-known project and config files
  if (filename.ends_with(".blade.php") || filename.ends_with(".blade.phtml") ||
      extension == ".blade" || filename == "artisan") {
    return Detail::symbol_icon("laravel");
  }
  if (filename == "cmakelists.txt") {
    return Detail::symbol_icon("cmake");
  }
  if (filename == "dockerfile" || filename.starts_with("dockerfile.")) {
    return Detail::symbol_icon("docker");
  }
  if (filename == ".dockerignore") {
    return Detail::symbol_icon("docker");
  }
  if (filename == "makefile" || filename.starts_with("makefile.")) {
    return Detail::symbol_icon("gear");
  }
  if (filename == ".editorconfig") {
    return Detail::symbol_icon("editorconfig");
  }
  if (filename == ".gitignore" || filename == ".gitattributes" ||
      filename == ".gitmodules") {
    return Detail::symbol_icon("git");
  }
  if (filename.starts_with("readme")) {
    return Detail::symbol_icon("markdown");
  }
  if (filename.starts_with("license") || filename.starts_with("licence")) {
    return Detail::symbol_icon("license");
  }
  if (filename.starts_with("changelog") || filename.starts_with("changes")) {
    return Detail::symbol_icon("document");
  }
  if (filename == "package.json" || filename == "package-lock.json" ||
      filename == "npm-shrinkwrap.json") {
    return Detail::symbol_icon("npm");
  }
  if (filename == "yarn.lock") {
    return Detail::symbol_icon("yarn");
  }
  if (filename == "pnpm-lock.yaml" || filename == "pnpm-workspace.yaml") {
    return Detail::symbol_icon("pnpm");
  }
  if (filename == "bun.lockb" || filename == "bunfig.toml") {
    return Detail::symbol_icon("bun");
  }
  if (filename == "cargo.toml" || filename == "cargo.lock") {
    return Detail::symbol_icon("rust");
  }
  if (filename == "go.mod" || filename == "go.sum" || filename == "go.work") {
    return Detail::symbol_icon("go");
  }
  if (filename == "pom.xml") {
    return Detail::symbol_icon("maven");
  }
  if (filename == "build.gradle" || filename == "settings.gradle" ||
      filename == "gradle.properties") {
    return Detail::symbol_icon("gradle");
  }
  if (filename == ".eslintrc" || filename.starts_with(".eslintrc.") ||
      filename == "eslint.config.js" || filename == "eslint.config.mjs") {
    return Detail::symbol_icon("eslint");
  }
  if (filename == ".prettierrc" || filename.starts_with(".prettierrc.") ||
      filename == "prettier.config.js" || filename == "prettier.config.mjs") {
    return Detail::symbol_icon("prettier");
  }
  if (filename.starts_with("vite.config.")) {
    return Detail::symbol_icon("vite");
  }
  if (filename.starts_with("vitest.config.")) {
    return Detail::symbol_icon("vitest");
  }
  if (filename.starts_with("webpack.config.")) {
    return Detail::symbol_icon("webpack");
  }
  if (filename.starts_with("babel.config.") || filename == ".babelrc") {
    return Detail::symbol_icon("babel");
  }
  if (filename.starts_with("tailwind.config.")) {
    return Detail::symbol_icon("tailwind");
  }
  if (filename.starts_with("next.config.")) {
    return Detail::symbol_icon("next");
  }
  if (filename.starts_with("nuxt.config.")) {
    return Detail::symbol_icon("nuxt");
  }
  if (filename.starts_with("svelte.config.")) {
    return Detail::symbol_icon("svelte");
  }
  if (filename.starts_with("astro.config.")) {
    return Detail::symbol_icon("astro");
  }
  if (filename == "components.json") {
    return Detail::symbol_icon("shadcn");
  }
  if (filename == "prisma.schema" || filename == "schema.prisma") {
    return Detail::symbol_icon("prisma");
  }
  if (filename.starts_with("tsconfig") && extension == ".json") {
    return Detail::symbol_icon("tsconfig");
  }
  if (filename.starts_with("jsconfig") && extension == ".json") {
    return Detail::symbol_icon("tsconfig");
  }
  if (filename.ends_with(".d.ts") || filename.ends_with(".d.mts") ||
      filename.ends_with(".d.cts")) {
    return Detail::symbol_icon("dts");
  }
  if (filename == ".env" || filename.starts_with(".env.")) {
    return Detail::symbol_icon("gear");
  }

  // C++ Standard Library & STL headers without extension
  if (extension.empty()) {
    if (filename == "string" || filename == "vector" || filename == "map" ||
        filename == "set" || filename == "unordered_map" || filename == "unordered_set" ||
        filename == "iostream" || filename == "ostream" || filename == "istream" ||
        filename == "fstream" || filename == "sstream" || filename == "memory" ||
        filename == "algorithm" || filename == "utility" || filename == "tuple" ||
        filename == "chrono" || filename == "filesystem" || filename == "optional" ||
        filename == "variant" || filename == "any" || filename == "thread" ||
        filename == "mutex" || filename == "atomic" || filename == "future" ||
        filename == "type_traits" || filename == "functional" || filename == "array" ||
        filename == "deque" || filename == "list" || filename == "span" ||
        filename == "string_view" || filename == "format" || filename == "ranges" ||
        filename == "xstring" || filename == "xmemory" || filename == "xutility" ||
        filename == "yvals" || filename == "yvals_core") {
      return Detail::symbol_icon("cplus");
    }
  }

  // --- Shaders: Kept unchanged (material-icon-theme/shader.svg) ---
  if (extension == ".glsl" || extension == ".hlsl" || extension == ".wgsl" ||
      extension == ".vert" || extension == ".frag" || extension == ".geom" ||
      extension == ".comp" || extension == ".tesc" || extension == ".tese" ||
      extension == ".shader") {
    return Detail::shader_icon();
  }

  // --- Assembly: (material-icon-theme/assembly.svg) ---
  if (extension == ".asm" || extension == ".s" || extension == ".inc" ||
      extension == ".nasm" || extension == ".a51") {
    return Detail::assembly_icon();
  }

  // Programming languages and build/configuration formats from vscode-symbols
  if (extension == ".c") {
    return Detail::symbol_icon("c");
  }
  if (extension == ".cc" || extension == ".cp" || extension == ".cpp" ||
      extension == ".cxx" || extension == ".ixx" || extension == ".cppm") {
    return Detail::symbol_icon("cplus");
  }
  if (extension == ".h") {
    return Detail::symbol_icon("h");
  }
  if (extension == ".hh" || extension == ".hpp" || extension == ".hxx" ||
      extension == ".inl") {
    return Detail::symbol_icon("cplus");
  }
  if (extension == ".m") {
    return Detail::symbol_icon("c");
  }
  if (extension == ".mm") {
    return Detail::symbol_icon("cplus");
  }
  if (extension == ".rs") {
    return Detail::symbol_icon("rust");
  }
  if (extension == ".js" || extension == ".mjs" || extension == ".cjs") {
    return Detail::symbol_icon("js");
  }
  if (extension == ".jsx") {
    return Detail::symbol_icon("react");
  }
  if (extension == ".ts" || extension == ".mts" || extension == ".cts") {
    return Detail::symbol_icon("ts");
  }
  if (extension == ".tsx") {
    return Detail::symbol_icon("react-ts");
  }
  if (extension == ".py" || extension == ".pyw") {
    return Detail::symbol_icon("python");
  }
  if (extension == ".ipynb") {
    return Detail::symbol_icon("notebook");
  }
  if (extension == ".ps1" || extension == ".psm1" || extension == ".psd1" ||
      extension == ".sh" || extension == ".bash" || extension == ".zsh" ||
      extension == ".fish" || extension == ".bat" || extension == ".cmd") {
    return Detail::symbol_icon("shell");
  }
  if (extension == ".go") {
    return Detail::symbol_icon("go");
  }
  if (extension == ".java") {
    return Detail::symbol_icon("java");
  }
  if (extension == ".kt" || extension == ".kts") {
    return Detail::symbol_icon("kotlin");
  }
  if (extension == ".swift") {
    return Detail::symbol_icon("swift");
  }
  if (extension == ".cs") {
    return Detail::symbol_icon("csharp");
  }
  if (extension == ".fs" || extension == ".fsx" || extension == ".fsi") {
    return Detail::symbol_icon("fsharp");
  }
  if (extension == ".scala") {
    return Detail::symbol_icon("scala");
  }
  if (extension == ".clj" || extension == ".cljs" || extension == ".cljc") {
    return Detail::symbol_icon("clojure");
  }
  if (extension == ".ml" || extension == ".mli") {
    return Detail::symbol_icon("ocaml");
  }
  if (extension == ".lua") {
    return Detail::symbol_icon("lua");
  }
  if (extension == ".luau") {
    return Detail::symbol_icon("luau");
  }
  if (extension == ".rb") {
    return Detail::symbol_icon("ruby");
  }
  if (extension == ".php" || extension == ".phtml" || extension == ".php4" ||
      extension == ".php5" || extension == ".php7" || extension == ".php8" ||
      extension == ".phps") {
    return Detail::symbol_icon("php");
  }
  if (extension == ".dart") {
    return Detail::symbol_icon("dart");
  }
  if (extension == ".zig") {
    return Detail::symbol_icon("zig");
  }
  if (extension == ".hs") {
    return Detail::symbol_icon("haskell");
  }
  if (extension == ".erl" || extension == ".hrl") {
    return Detail::symbol_icon("erlang");
  }
  if (extension == ".ex" || extension == ".exs") {
    return Detail::symbol_icon("elixir");
  }
  if (extension == ".pl" || extension == ".pm") {
    return Detail::symbol_icon("perl");
  }
  if (extension == ".r") {
    return Detail::symbol_icon("r");
  }
  if (extension == ".jl") {
    return Detail::symbol_icon("julia");
  }
  if (extension == ".nim") {
    return Detail::symbol_icon("nim");
  }
  if (extension == ".cr") {
    return Detail::symbol_icon("crystal");
  }
  if (extension == ".f" || extension == ".for" || extension == ".f90" ||
      extension == ".f95" || extension == ".f03" || extension == ".f08") {
    return Detail::symbol_icon("fortran");
  }
  if (extension == ".cu" || extension == ".cuh") {
    return Detail::symbol_icon("cuda");
  }
  if (extension == ".sol") {
    return Detail::symbol_icon("solidity");
  }
  if (extension == ".graphql" || extension == ".gql") {
    return Detail::symbol_icon("graphql");
  }
  if (extension == ".proto") {
    return Detail::symbol_icon("proto");
  }
  if (extension == ".tf" || extension == ".tfvars") {
    return Detail::symbol_icon("terraform");
  }
  if (extension == ".prisma") {
    return Detail::symbol_icon("prisma");
  }
  if (extension == ".sln" || extension == ".slnx" || extension == ".csproj" ||
      extension == ".fsproj" || extension == ".vbproj" ||
      extension == ".vcxproj" || extension == ".props" ||
      extension == ".targets") {
    return Detail::symbol_icon("visual-studio");
  }
  if (extension == ".cmake") {
    return Detail::symbol_icon("cmake");
  }

  // Markup, data, styles, and project configuration
  if (extension == ".md" || extension == ".markdown" || extension == ".mdown" ||
      extension == ".mkdn") {
    return Detail::symbol_icon("markdown");
  }
  if (extension == ".mdx") {
    return Detail::symbol_icon("mdx");
  }
  if (extension == ".html" || extension == ".htm" || extension == ".xhtml") {
    return Detail::symbol_icon("code-orange");
  }
  if (extension == ".css") {
    return Detail::symbol_icon("code-sky");
  }
  if (extension == ".scss" || extension == ".sass") {
    return Detail::symbol_icon("sass");
  }
  if (extension == ".postcss" || extension == ".pcss") {
    return Detail::symbol_icon("postcss");
  }
  if (extension == ".xml" || extension == ".xsd" || extension == ".xsl" ||
      extension == ".xslt") {
    return Detail::symbol_icon("xml");
  }
  if (extension == ".json" || extension == ".jsonc" || extension == ".json5") {
    return Detail::symbol_icon("brackets-yellow");
  }
  if (extension == ".yml" || extension == ".yaml") {
    return Detail::symbol_icon("yaml");
  }
  if (extension == ".toml" || extension == ".ini" || extension == ".cfg" ||
      extension == ".conf" || extension == ".properties") {
    return Detail::symbol_icon("gear");
  }
  if (extension == ".sql" || extension == ".db" || extension == ".sqlite" ||
      extension == ".sqlite3") {
    return Detail::symbol_icon("database");
  }
  if (extension == ".csv" || extension == ".tsv") {
    return Detail::symbol_icon("csv");
  }
  if (extension == ".vue") {
    return Detail::symbol_icon("vue");
  }
  if (extension == ".svelte") {
    return Detail::symbol_icon("svelte");
  }
  if (extension == ".astro") {
    return Detail::symbol_icon("astro");
  }

  // Binary, media, archive, and plain-text files
  if (extension == ".dll" || extension == ".exe" || extension == ".bin") {
    return Detail::symbol_icon("exe");
  }
  if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
      extension == ".webp" || extension == ".bmp" || extension == ".ico" ||
      extension == ".tif" || extension == ".tiff") {
    return Detail::symbol_icon("image");
  }
  if (extension == ".gif") {
    return Detail::symbol_icon("gif");
  }
  if (extension == ".svg") {
    return Detail::symbol_icon("svg");
  }
  if (extension == ".mp3" || extension == ".wav" || extension == ".ogg" ||
      extension == ".flac") {
    return Detail::symbol_icon("audio");
  }
  if (extension == ".mp4" || extension == ".webm" || extension == ".mkv" ||
      extension == ".mov" || extension == ".avi") {
    return Detail::symbol_icon("video");
  }
  if (extension == ".ttf" || extension == ".otf" || extension == ".woff" ||
      extension == ".woff2") {
    return Detail::symbol_icon("font");
  }
  if (extension == ".zip" || extension == ".7z" || extension == ".rar" ||
      extension == ".tar" || extension == ".gz" || extension == ".bz2" ||
      extension == ".xz") {
    return Detail::symbol_icon("compressed");
  }
  if (extension == ".lock") {
    return Detail::symbol_icon("lock");
  }
  if (extension == ".pdf") {
    return Detail::symbol_icon("pdf");
  }
  if (extension == ".txt" || extension == ".text" ||
      extension == ".plaintext" || extension == ".log" || extension == ".rtf") {
    return Detail::symbol_icon("document");
  }

  return Detail::symbol_icon("document");
}

[[nodiscard]] inline std::string
folder_icon_asset_for_path(const std::filesystem::path &path,
                           bool expanded = false,
                           bool use_material_icons = false) {
  if (use_material_icons) {
    const std::string folder_name = Detail::lowercase_ascii(path.filename().string());

    if (folder_name == ".git") {
      return "material-icon-theme/folder-git.svg";
    }
    if (folder_name == ".github") {
      return "material-icon-theme/folder-github.svg";
    }
    if (folder_name == ".vscode") {
      return "material-icon-theme/folder-vscode.svg";
    }
    if (folder_name == "node_modules") {
      return "material-icon-theme/folder-node.svg";
    }
    if (folder_name == "assets" || folder_name == "asset" || folder_name == "resources" || folder_name == "resource") {
      return "material-icon-theme/folder-resource.svg";
    }
    if (folder_name == "build" || folder_name == "builds" || folder_name == "bin" || folder_name == "out" || folder_name == "target" || folder_name == "dist") {
      return "material-icon-theme/folder-dist.svg";
    }
    if (folder_name == "cmake") {
      return "material-icon-theme/folder-config.svg";
    }
    if (folder_name == "config" || folder_name == "configs" || folder_name == "configuration" || folder_name == "conf" || folder_name == "settings" || folder_name == "manifest" || folder_name == "manifests") {
      return "material-icon-theme/folder-config.svg";
    }
    if (folder_name == "docs" || folder_name == "doc" || folder_name == "documentation") {
      return "material-icon-theme/folder-docs.svg";
    }
    if (folder_name == "drivers" || folder_name == "driver") {
      return "material-icon-theme/folder-tools.svg";
    }
    if (folder_name == "example" || folder_name == "examples" || folder_name == "sample" || folder_name == "samples") {
      return "material-icon-theme/folder-examples.svg";
    }
    if (folder_name == "lib" || folder_name == "libs" || folder_name == "library" || folder_name == "libraries") {
      return "material-icon-theme/folder-lib.svg";
    }
    if (folder_name == "scripts" || folder_name == "script") {
      return "material-icon-theme/folder-scripts.svg";
    }
    if (folder_name == "source" || folder_name == "sources" || folder_name == "src") {
      return "material-icon-theme/folder-src.svg";
    }
    if (folder_name == "test" || folder_name == "tests" || folder_name == "testing" || folder_name == "spec" || folder_name == "specs") {
      return "material-icon-theme/folder-test.svg";
    }
    if (folder_name == "include" || folder_name == "includes") {
      return "material-icon-theme/folder-include.svg";
    }
    if (folder_name == "images" || folder_name == "image" || folder_name == "img" || folder_name == "icons" || folder_name == "icon" || folder_name == "textures") {
      return "material-icon-theme/folder-images.svg";
    }
    if (folder_name == "fonts" || folder_name == "font") {
      return "material-icon-theme/folder-font.svg";
    }
    if (folder_name == "app" || folder_name == "apps") {
      return "material-icon-theme/folder-app.svg";
    }
    if (folder_name == "core") {
      return "material-icon-theme/folder-core.svg";
    }
    if (folder_name == "database" || folder_name == "db" || folder_name == "data") {
      return "material-icon-theme/folder-database.svg";
    }
    if (folder_name == "models" || folder_name == "model" || folder_name == "entities" || folder_name == "entity") {
      return "vscode-symbols/icons/folders/folder-models.svg";
    }
    if (folder_name == "modules" || folder_name == "module" || folder_name == "packages" || folder_name == "package") {
      return "material-icon-theme/folder-packages.svg";
    }
    if (folder_name == "components" || folder_name == "component" || folder_name == "widgets" || folder_name == "widget") {
      return "material-icon-theme/folder-components.svg";
    }
    if (folder_name == "views" || folder_name == "view" || folder_name == "pages" || folder_name == "page") {
      return "material-icon-theme/folder-views.svg";
    }
    if (folder_name == "utils" || folder_name == "util" || folder_name == "utility" || folder_name == "utilities" || folder_name == "helpers" || folder_name == "helper") {
      return "material-icon-theme/folder-utils.svg";
    }
    if (folder_name == "routes" || folder_name == "route" || folder_name == "router") {
      return "material-icon-theme/folder-routes.svg";
    }
    if (folder_name == "ui" || folder_name == "gui") {
      return "material-icon-theme/folder-ui.svg";
    }
    if (folder_name == "tools" || folder_name == "tool") {
      return "material-icon-theme/folder-tools.svg";
    }
    if (folder_name == "temp" || folder_name == "tmp" || folder_name == "cache") {
      return "material-icon-theme/folder-temp.svg";
    }
    if (folder_name == "logs" || folder_name == "log") {
      return "material-icon-theme/folder-log.svg";
    }
    if (folder_name == "docker" || folder_name == ".docker") {
      return "material-icon-theme/folder-docker.svg";
    }
    if (folder_name == "rules" || folder_name == ".agents" || folder_name == "skills") {
      return "material-icon-theme/folder-rules.svg";
    }
  }

  // VS Code standard outline folder icons by default
  if (expanded) {
    return "folder-open.svg";
  }
  return "folder.svg";
}

} // namespace Zenvra::UI::Editor
