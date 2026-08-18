#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::UI::Editor
{

namespace Detail
{

inline std::string lowercase_ascii(std::string value)
{
    for (char& character : value)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

inline std::string material_icon(std::string_view icon_name)
{
    std::string asset = "material-icon-theme/";
    asset.append(icon_name);
    asset.append(".svg");
    return asset;
}

} // namespace Detail

// Returns a path relative to Assets/icons. UI chrome and folder icons continue
// to use the legacy assets at the root of that directory.
[[nodiscard]] inline std::string file_icon_asset_for_path(
    const std::filesystem::path& path)
{
    const std::string filename = Detail::lowercase_ascii(path.filename().string());
    const std::string extension = Detail::lowercase_ascii(path.extension().string());

    // Well-known project files are more useful than their generic extension.
    if (filename == "cmakelists.txt")
    {
        return Detail::material_icon("cmake");
    }
    if (filename == "dockerfile" || filename.starts_with("dockerfile."))
    {
        return Detail::material_icon("docker");
    }
    if (filename == ".dockerignore")
    {
        return Detail::material_icon("docker");
    }
    if (filename == "makefile" || filename.starts_with("makefile."))
    {
        return Detail::material_icon("makefile");
    }
    if (filename == ".editorconfig")
    {
        return Detail::material_icon("editorconfig");
    }
    if (filename == ".gitignore" || filename == ".gitattributes" ||
        filename == ".gitmodules")
    {
        return Detail::material_icon("git");
    }
    if (filename.starts_with("readme"))
    {
        return Detail::material_icon("readme");
    }
    if (filename.starts_with("license") || filename.starts_with("licence"))
    {
        return Detail::material_icon("license");
    }
    if (filename.starts_with("changelog") || filename.starts_with("changes"))
    {
        return Detail::material_icon("changelog");
    }
    if (filename == "package.json" || filename == "package-lock.json" ||
        filename == "npm-shrinkwrap.json")
    {
        return Detail::material_icon("npm");
    }
    if (filename == "yarn.lock")
    {
        return Detail::material_icon("yarn");
    }
    if (filename == "pnpm-lock.yaml")
    {
        return Detail::material_icon("pnpm");
    }
    if (filename == "cargo.toml" || filename == "cargo.lock")
    {
        return Detail::material_icon("rust");
    }
    if (filename == "go.mod" || filename == "go.sum")
    {
        return Detail::material_icon("go-mod");
    }
    if (filename == "pom.xml")
    {
        return Detail::material_icon("maven");
    }
    if (filename == "build.gradle" || filename == "settings.gradle" ||
        filename == "gradle.properties")
    {
        return Detail::material_icon("gradle");
    }
    if (filename == "androidmanifest.xml")
    {
        return Detail::material_icon("android");
    }
    if (filename == ".eslintrc" || filename.starts_with(".eslintrc."))
    {
        return Detail::material_icon("eslint");
    }
    if (filename == ".prettierrc" || filename.starts_with(".prettierrc."))
    {
        return Detail::material_icon("prettier");
    }
    if (filename.starts_with("vite.config."))
    {
        return Detail::material_icon("vite");
    }
    if (filename.starts_with("webpack.config."))
    {
        return Detail::material_icon("webpack");
    }
    if (filename.starts_with("rollup.config."))
    {
        return Detail::material_icon("rollup");
    }
    if (filename.starts_with("babel.config."))
    {
        return Detail::material_icon("babel");
    }
    if (filename == "prisma.schema")
    {
        return Detail::material_icon("prisma");
    }
    if (filename.starts_with("tsconfig") && extension == ".json")
    {
        return Detail::material_icon("tsconfig");
    }
    if (filename.starts_with("jsconfig") && extension == ".json")
    {
        return Detail::material_icon("jsconfig");
    }
    if (filename.ends_with(".d.ts") || filename.ends_with(".d.mts") || filename.ends_with(".d.cts"))
    {
        return Detail::material_icon("typescript-def");
    }
    if (filename == ".env" || filename.starts_with(".env."))
    {
        return Detail::material_icon("settings");
    }

    // Programming languages and build/configuration formats.
    if (extension == ".c")
    {
        return Detail::material_icon("c");
    }
    if (extension == ".cc" || extension == ".cp" || extension == ".cpp" ||
        extension == ".cxx")
    {
        return Detail::material_icon("cpp");
    }
    if (extension == ".h")
    {
        return Detail::material_icon("h");
    }
    if (extension == ".hh" || extension == ".hpp" || extension == ".hxx" ||
        extension == ".inl")
    {
        return Detail::material_icon("hpp");
    }
    if (extension == ".m")
    {
        return Detail::material_icon("objective-c");
    }
    if (extension == ".mm")
    {
        return Detail::material_icon("objective-cpp");
    }
    if (extension == ".rs")
    {
        return Detail::material_icon("rust");
    }
    if (extension == ".js" || extension == ".mjs" || extension == ".cjs")
    {
        return Detail::material_icon("javascript");
    }
    if (extension == ".jsx")
    {
        return Detail::material_icon("react");
    }
    if (extension == ".ts" || extension == ".mts" || extension == ".cts")
    {
        return Detail::material_icon("typescript");
    }
    if (extension == ".tsx")
    {
        return Detail::material_icon("react_ts");
    }
    if (extension == ".py" || extension == ".pyw")
    {
        return Detail::material_icon("python");
    }
    if (extension == ".ps1" || extension == ".psm1" || extension == ".psd1")
    {
        return Detail::material_icon("pwsh");
    }
    if (extension == ".sh" || extension == ".bash" || extension == ".zsh" ||
        extension == ".fish" || extension == ".bat" || extension == ".cmd")
    {
        return Detail::material_icon("console");
    }
    if (extension == ".go")
    {
        return Detail::material_icon("go");
    }
    if (extension == ".java")
    {
        return Detail::material_icon("java");
    }
    if (extension == ".kt" || extension == ".kts")
    {
        return Detail::material_icon("kotlin");
    }
    if (extension == ".swift")
    {
        return Detail::material_icon("swift");
    }
    if (extension == ".cs")
    {
        return Detail::material_icon("csharp");
    }
    if (extension == ".fs" || extension == ".fsx" || extension == ".fsi")
    {
        return Detail::material_icon("fsharp");
    }
    if (extension == ".scala")
    {
        return Detail::material_icon("scala");
    }
    if (extension == ".groovy")
    {
        return Detail::material_icon("groovy");
    }
    if (extension == ".clj" || extension == ".cljs" || extension == ".cljc")
    {
        return Detail::material_icon("clojure");
    }
    if (extension == ".ml" || extension == ".mli")
    {
        return Detail::material_icon("ocaml");
    }
    if (extension == ".mojo")
    {
        return Detail::material_icon("mojo");
    }
    if (extension == ".lua")
    {
        return Detail::material_icon("lua");
    }
    if (extension == ".rb")
    {
        return Detail::material_icon("ruby");
    }
    if (extension == ".php")
    {
        return Detail::material_icon("php");
    }
    if (extension == ".dart")
    {
        return Detail::material_icon("dart");
    }
    if (extension == ".zig")
    {
        return Detail::material_icon("zig");
    }
    if (extension == ".hs")
    {
        return Detail::material_icon("haskell");
    }
    if (extension == ".erl" || extension == ".hrl")
    {
        return Detail::material_icon("erlang");
    }
    if (extension == ".ex" || extension == ".exs")
    {
        return Detail::material_icon("elixir");
    }
    if (extension == ".pl" || extension == ".pm")
    {
        return Detail::material_icon("perl");
    }
    if (extension == ".r")
    {
        return Detail::material_icon("r");
    }
    if (extension == ".mat")
    {
        return Detail::material_icon("matlab");
    }
    if (extension == ".jl")
    {
        return Detail::material_icon("julia");
    }
    if (extension == ".nim")
    {
        return Detail::material_icon("nim");
    }
    if (extension == ".cr")
    {
        return Detail::material_icon("crystal");
    }
    if (extension == ".pas")
    {
        return Detail::material_icon("pascal");
    }
    if (extension == ".f" || extension == ".for" || extension == ".f90" ||
        extension == ".f95" || extension == ".f03" || extension == ".f08")
    {
        return Detail::material_icon("fortran");
    }
    if (extension == ".asm" || extension == ".s")
    {
        return Detail::material_icon("assembly");
    }
    if (extension == ".wat" || extension == ".wasm")
    {
        return Detail::material_icon("webassembly");
    }
    if (extension == ".glsl" || extension == ".hlsl" || extension == ".wgsl" ||
        extension == ".vert" || extension == ".frag" || extension == ".shader")
    {
        return Detail::material_icon("shader");
    }
    if (extension == ".sol")
    {
        return Detail::material_icon("solidity");
    }
    if (extension == ".graphql" || extension == ".gql")
    {
        return Detail::material_icon("graphql");
    }
    if (extension == ".proto")
    {
        return Detail::material_icon("proto");
    }
    if (extension == ".bicep")
    {
        return Detail::material_icon("bicep");
    }
    if (extension == ".tf" || extension == ".tfvars")
    {
        return Detail::material_icon("terraform");
    }
    if (extension == ".prisma")
    {
        return Detail::material_icon("prisma");
    }
    if (extension == ".sln" || extension == ".slnx" || extension == ".csproj" ||
        extension == ".fsproj" || extension == ".vbproj" || extension == ".vcxproj" ||
        extension == ".props" || extension == ".targets")
    {
        return Detail::material_icon("visualstudio");
    }
    if (extension == ".jar")
    {
        return Detail::material_icon("jar");
    }

    // Markup, data, styles, and project configuration.
    if (extension == ".md" || extension == ".markdown" || extension == ".mdown" ||
        extension == ".mkdn")
    {
        return Detail::material_icon("markdown");
    }
    if (extension == ".mdx")
    {
        return Detail::material_icon("mdx");
    }
    if (extension == ".html" || extension == ".htm" || extension == ".xhtml")
    {
        return Detail::material_icon("html");
    }
    if (extension == ".css")
    {
        return Detail::material_icon("css");
    }
    if (extension == ".scss" || extension == ".sass")
    {
        return Detail::material_icon("sass");
    }
    if (extension == ".less")
    {
        return Detail::material_icon("less");
    }
    if (extension == ".xml" || extension == ".xsd" || extension == ".xsl" ||
        extension == ".xslt")
    {
        return Detail::material_icon("xml");
    }
    if (extension == ".json" || extension == ".jsonc" || extension == ".json5")
    {
        return Detail::material_icon("json");
    }
    if (extension == ".yml" || extension == ".yaml")
    {
        return Detail::material_icon("yaml");
    }
    if (extension == ".toml")
    {
        return Detail::material_icon("toml");
    }
    if (extension == ".ini" || extension == ".cfg" || extension == ".conf" ||
        extension == ".properties" || extension == ".env")
    {
        return Detail::material_icon("settings");
    }
    if (extension == ".log")
    {
        return Detail::material_icon("log");
    }
    if (extension == ".sql")
    {
        return Detail::material_icon("database");
    }
    if (extension == ".csv" || extension == ".tsv")
    {
        return Detail::material_icon("table");
    }
    if (extension == ".vue")
    {
        return Detail::material_icon("vue");
    }
    if (extension == ".svelte")
    {
        return Detail::material_icon("svelte");
    }
    if (extension == ".astro")
    {
        return Detail::material_icon("astro");
    }

    // Binary, media, archive, and plain-text files.
    if (extension == ".dll")
    {
        return Detail::material_icon("dll");
    }
    if (extension == ".exe")
    {
        return Detail::material_icon("exe");
    }
    if (extension == ".lib")
    {
        return Detail::material_icon("lib");
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".gif" || extension == ".webp" || extension == ".bmp" ||
        extension == ".ico" || extension == ".tif" || extension == ".tiff")
    {
        return Detail::material_icon("image");
    }
    if (extension == ".svg")
    {
        return Detail::material_icon("svg");
    }
    if (extension == ".mp3" || extension == ".wav" || extension == ".ogg" ||
        extension == ".flac")
    {
        return Detail::material_icon("audio");
    }
    if (extension == ".mp4" || extension == ".webm" || extension == ".mkv" ||
        extension == ".mov")
    {
        return Detail::material_icon("video");
    }
    if (extension == ".ttf" || extension == ".otf" || extension == ".woff" ||
        extension == ".woff2")
    {
        return Detail::material_icon("font");
    }
    if (extension == ".zip" || extension == ".7z" || extension == ".rar" ||
        extension == ".tar" || extension == ".gz" || extension == ".bz2" ||
        extension == ".xz")
    {
        return Detail::material_icon("zip");
    }
    if (extension == ".lock")
    {
        return Detail::material_icon("lock");
    }
    if (extension == ".txt" || extension == ".text" || extension == ".plaintext" ||
        extension == ".rtf")
    {
        return Detail::material_icon("document");
    }

    return Detail::material_icon("document");
}

} // namespace Zenvra::UI::Editor
