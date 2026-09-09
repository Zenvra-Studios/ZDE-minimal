#include "Plugins/Marketplace/MarketplaceClient.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace Zenvra::Plugins::Marketplace
{

namespace
{

std::string to_lower(std::string_view s)
{
    std::string res(s);
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return res;
}

#ifdef _WIN32
bool download_http_file(const std::string& url_str, const std::filesystem::path& dest_path)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url_str.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring w_url(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url_str.c_str(), -1, w_url.data(), wlen);
    if (!w_url.empty() && w_url.back() == L'\0') w_url.pop_back();

    URL_COMPONENTSW url_comp{};
    url_comp.dwStructSize = sizeof(url_comp);
    wchar_t host_name[512] = {0};
    wchar_t url_path[2048] = {0};
    url_comp.lpszHostName = host_name;
    url_comp.dwHostNameLength = sizeof(host_name) / sizeof(wchar_t);
    url_comp.lpszUrlPath = url_path;
    url_comp.dwUrlPathLength = sizeof(url_path) / sizeof(wchar_t);

    if (!WinHttpCrackUrl(w_url.c_str(), static_cast<DWORD>(w_url.length()), 0, &url_comp))
    {
        return false;
    }

    HINTERNET h_session = WinHttpOpen(L"ZDE-Marketplace/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h_session) return false;

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(h_session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

    INTERNET_PORT port = url_comp.nPort ? url_comp.nPort :
        ((url_comp.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);

    HINTERNET h_connect = WinHttpConnect(h_session, host_name, port, 0);
    if (!h_connect) {
        WinHttpCloseHandle(h_session);
        return false;
    }

    DWORD flags = (url_comp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET h_request = WinHttpOpenRequest(h_connect, L"GET", url_path,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!h_request) {
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
        return false;
    }

    bool ok = false;
    if (WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(h_request, nullptr))
    {
        DWORD status_code = 0;
        DWORD size = sizeof(status_code);
        WinHttpQueryHeaders(h_request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status_code, &size, WINHTTP_NO_HEADER_INDEX);

        if (status_code == 200)
        {
            std::error_code ec;
            std::filesystem::create_directories(dest_path.parent_path(), ec);
            std::filesystem::path tmp_path = dest_path;
            tmp_path += ".tmp";

            std::ofstream out(tmp_path, std::ios::binary);
            if (out.is_open())
            {
                DWORD bytes_available = 0;
                while (WinHttpQueryDataAvailable(h_request, &bytes_available) && bytes_available > 0)
                {
                    std::vector<char> buffer(bytes_available);
                    DWORD bytes_read = 0;
                    if (WinHttpReadData(h_request, buffer.data(), bytes_available, &bytes_read) && bytes_read > 0)
                    {
                        out.write(buffer.data(), bytes_read);
                    }
                    else
                    {
                        break;
                    }
                }
                out.close();

                std::filesystem::rename(tmp_path, dest_path, ec);
                if (ec)
                {
                    std::filesystem::copy_file(tmp_path, dest_path, std::filesystem::copy_options::overwrite_existing, ec);
                    std::filesystem::remove(tmp_path, ec);
                }
                ok = true;
            }
        }
    }

    WinHttpCloseHandle(h_request);
    WinHttpCloseHandle(h_connect);
    WinHttpCloseHandle(h_session);
    return ok;
}
#endif

} // namespace

bool MarketplaceClient::load_catalog_from_file(const std::filesystem::path& file_path)
{
    std::ifstream stream(file_path);
    if (!stream.is_open())
    {
        return false;
    }

    try
    {
        nlohmann::json root;
        stream >> root;
        return load_catalog_from_json(root);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[MarketplaceClient] Error reading " << file_path << ": " << e.what() << '\n';
    }
    return false;
}

bool MarketplaceClient::load_catalog_from_json(const nlohmann::json& j)
{
    if (!j.is_object() && !j.is_array()) return false;

    std::vector<MarketplacePluginEntry> new_entries;
    const nlohmann::json* list = &j;
    if (j.is_object() && j.contains("plugins") && j["plugins"].is_array())
    {
        list = &j["plugins"];
    }

    for (const auto& item : *list)
    {
        if (!item.is_object()) continue;

        MarketplacePluginEntry entry;
        if (item.contains("id") && item["id"].is_string()) entry.id = item["id"].get<std::string>();
        if (item.contains("name") && item["name"].is_string()) entry.name = item["name"].get<std::string>();
        if (item.contains("version") && item["version"].is_string()) entry.version = item["version"].get<std::string>();
        if (item.contains("description") && item["description"].is_string()) entry.description = item["description"].get<std::string>();
        if (item.contains("publisher") && item["publisher"].is_string()) entry.publisher = item["publisher"].get<std::string>();
        if (item.contains("repository") && item["repository"].is_string()) entry.repository_url = item["repository"].get<std::string>();
        if (item.contains("downloadUrl") && item["downloadUrl"].is_string()) entry.download_url = item["downloadUrl"].get<std::string>();
        if (item.contains("iconUrl") && item["iconUrl"].is_string()) entry.icon_url = item["iconUrl"].get<std::string>();
        else if (item.contains("icon") && item["icon"].is_string()) entry.icon_url = item["icon"].get<std::string>();
        if (item.contains("sha256") && item["sha256"].is_string()) entry.sha256 = item["sha256"].get<std::string>();
        if (item.contains("category") && item["category"].is_string()) entry.category = item["category"].get<std::string>();

        if (item.contains("tags") && item["tags"].is_array())
        {
            for (const auto& tag : item["tags"])
            {
                if (tag.is_string()) entry.tags.push_back(tag.get<std::string>());
            }
        }

        if (entry.category.empty() || entry.category == "general")
        {
            std::string lower_id = to_lower(entry.id);
            std::string lower_name = to_lower(entry.name);
            std::string lower_desc = to_lower(entry.description);
            bool deduced = false;
            for (const auto& tag : entry.tags)
            {
                std::string lt = to_lower(tag);
                if (lt == "lsp" || lt == "language" || lt == "languageserver") { entry.category = "lsp"; deduced = true; break; }
                if (lt == "emulator" || lt == "virtualmachine" || lt == "debugger" || lt == "qemu") { entry.category = "emulators"; deduced = true; break; }
                if (lt == "theme" || lt == "icons") { entry.category = "themes"; deduced = true; break; }
                if (lt == "tool" || lt == "tools" || lt == "git" || lt == "docker" || lt == "lint") { entry.category = "tools"; deduced = true; break; }
            }
            if (!deduced)
            {
                if (lower_id.find("lsp") != std::string::npos || lower_id.find("clangd") != std::string::npos ||
                    lower_id.find("analyzer") != std::string::npos || lower_id.find("pyright") != std::string::npos ||
                    lower_id.find("gopls") != std::string::npos || lower_id.find("zls") != std::string::npos ||
                    lower_name.find("lsp") != std::string::npos || lower_name.find("language") != std::string::npos)
                {
                    entry.category = "lsp";
                }
                else if (lower_id.find("emulator") != std::string::npos || lower_id.find("qemu") != std::string::npos ||
                         lower_id.find("gdb") != std::string::npos || lower_id.find("wasm") != std::string::npos ||
                         lower_name.find("emulator") != std::string::npos || lower_desc.find("emulator") != std::string::npos)
                {
                    entry.category = "emulators";
                }
                else if (lower_id.find("theme") != std::string::npos || lower_name.find("theme") != std::string::npos)
                {
                    entry.category = "themes";
                }
                else if (lower_id.find("git") != std::string::npos || lower_id.find("docker") != std::string::npos ||
                         lower_id.find("lint") != std::string::npos || lower_id.find("tool") != std::string::npos)
                {
                    entry.category = "tools";
                }
            }
        }

        if (!entry.id.empty())
        {
            new_entries.push_back(std::move(entry));
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries = std::move(new_entries);
    return true;
}

std::vector<MarketplacePluginEntry> MarketplaceClient::search(std::string_view query) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (query.empty())
    {
        return m_entries;
    }

    std::string lq = to_lower(query);
    std::vector<MarketplacePluginEntry> results;

    // Check for category prefix query: "@category:lsp", "category:emulators", etc.
    std::string target_category;
    if (lq.starts_with("@category:") || lq.starts_with("category:"))
    {
        auto colon_pos = lq.find(':');
        target_category = lq.substr(colon_pos + 1);
        while (!target_category.empty() && target_category.front() == ' ') target_category.erase(0, 1);
        while (!target_category.empty() && target_category.back() == ' ') target_category.pop_back();
    }

    // Direct Git / GitHub repository detection
    const bool is_explicit_git = (lq.find("github.com") != std::string::npos ||
                                  lq.starts_with("http://") || lq.starts_with("https://") ||
                                  lq.starts_with("git@") || lq.ends_with(".git"));
    const bool is_repo_shorthand = (!is_explicit_git && lq.find('/') != std::string::npos &&
                                    lq.find(' ') == std::string::npos && lq.front() != '/' && lq.back() != '/');

    if (is_explicit_git || is_repo_shorthand)
    {
        std::string raw_url = std::string(query);
        std::string full_url = raw_url;
        if (!full_url.starts_with("http://") && !full_url.starts_with("https://") && !full_url.starts_with("git@"))
        {
            if (full_url.starts_with("github.com/")) {
                full_url = "https://" + full_url;
            } else {
                full_url = "https://github.com/" + full_url;
            }
        }
        if (!full_url.ends_with(".git") && !full_url.starts_with("git@")) {
            full_url += ".git";
        }

        std::string repo_name = raw_url;
        std::string owner = "GitHub";
        auto slash_pos = raw_url.rfind('/');
        if (slash_pos != std::string::npos && slash_pos + 1 < raw_url.size())
        {
            repo_name = raw_url.substr(slash_pos + 1);
            if (repo_name.ends_with(".git")) repo_name = repo_name.substr(0, repo_name.size() - 4);
            auto prev_slash = raw_url.rfind('/', slash_pos - 1);
            if (prev_slash != std::string::npos) {
                owner = raw_url.substr(prev_slash + 1, slash_pos - prev_slash - 1);
            } else {
                owner = raw_url.substr(0, slash_pos);
            }
        }

        std::string git_icon_url = "https://raw.githubusercontent.com/" + owner + "/" + repo_name + "/HEAD/icon.png";
        if (raw_url.find("github.com") != std::string::npos) {
            git_icon_url = "https://avatars.githubusercontent.com/" + owner + "?s=128";
        }

        MarketplacePluginEntry git_entry{
            .id = "git." + owner + "." + repo_name,
            .name = repo_name,
            .version = "head",
            .description = "Direct Git repository from " + full_url,
            .publisher = owner,
            .repository_url = full_url,
            .download_url = "",
            .icon_url = git_icon_url,
            .sha256 = "",
            .category = "lsp",
            .tags = {"git", "github", "lsp"},
            .downloads = "Direct Git",
            .rating = "5"
        };
        results.push_back(git_entry);
    }

    for (const auto& entry : m_entries)
    {
        if (!target_category.empty())
        {
            if (to_lower(entry.category) == target_category ||
                to_lower(entry.category).find(target_category) != std::string::npos)
            {
                results.push_back(entry);
            }
            continue;
        }

        if (to_lower(entry.id).find(lq) != std::string::npos ||
            to_lower(entry.name).find(lq) != std::string::npos ||
            to_lower(entry.description).find(lq) != std::string::npos ||
            to_lower(entry.publisher).find(lq) != std::string::npos ||
            to_lower(entry.category).find(lq) != std::string::npos)
        {
            results.push_back(entry);
            continue;
        }

        for (const auto& tag : entry.tags)
        {
            if (to_lower(tag).find(lq) != std::string::npos)
            {
                results.push_back(entry);
                break;
            }
        }
    }

    return results;
}

std::vector<MarketplacePluginEntry> MarketplaceClient::get_all_entries() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

std::optional<MarketplacePluginEntry> MarketplaceClient::find_entry(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& e : m_entries)
    {
        if (e.id == id) return e;
    }

    if (id.starts_with("git."))
    {
        std::string s_id(id);
        std::string owner = "GitHub";
        std::string repo = s_id.substr(4);
        auto dot_pos = repo.find('.');
        if (dot_pos != std::string::npos)
        {
            owner = repo.substr(0, dot_pos);
            repo = repo.substr(dot_pos + 1);
        }
        std::string url = "https://github.com/" + owner + "/" + repo + ".git";
        return MarketplacePluginEntry{
            .id = s_id,
            .name = repo,
            .version = "head",
            .description = "Direct Git repository from " + url,
            .publisher = owner,
            .repository_url = url,
            .download_url = "",
            .icon_url = "https://avatars.githubusercontent.com/" + owner + "?s=128",
            .sha256 = "",
            .category = "lsp",
            .tags = {"git", "github", "lsp"},
            .downloads = "Direct Git",
            .rating = "5"
        };
    }
    return std::nullopt;
}

Installer::InstallResult MarketplaceClient::install_from_catalog(
    std::string_view plugin_id,
    Installer::PluginInstaller& installer,
    std::string_view current_zde_version,
    std::function<void(std::string_view)> log_callback)
{
    auto entry = find_entry(plugin_id);
    if (!entry.has_value())
    {
        Installer::InstallResult res;
        res.error_message = "Plugin '" + std::string(plugin_id) + "' not found in marketplace catalog.";
        return res;
    }

    if (!entry->repository_url.empty())
    {
        return installer.install_from_git(entry->repository_url, "", current_zde_version, log_callback);
    }

    Installer::InstallResult res;
    res.error_message = "No repository URL specified in catalog entry for '" + std::string(plugin_id) + "'.";
    return res;
}

void MarketplaceClient::ensure_default_catalog_if_empty()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_entries.empty()) return;

    m_entries = {
        {
            .id = "llvm-vs-code-extensions.vscode-clangd",
            .name = "clangd",
            .version = "0.1.33",
            .description = "C/C++ completion, navigation, diagnostics and semantic tokens via clangd LSP",
            .publisher = "llvm-vs-code-extensions",
            .repository_url = "https://github.com/clangd/vscode-clangd.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/clangd/vscode-clangd/master/icon.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"cpp", "c", "clangd", "llvm", "lsp"},
            .downloads = "6.8M",
            .rating = "4.9"
        },
        {
            .id = "ms-vscode.cmake-tools",
            .name = "CMake Tools",
            .version = "1.19.52",
            .description = "Extended CMake support and language integration for ZDE Studio",
            .publisher = "ms-vscode",
            .repository_url = "https://github.com/microsoft/vscode-cmake-tools.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/microsoft/vscode-cmake-tools/main/res/images/CMake_logo.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"cmake", "build", "c++", "lsp"},
            .downloads = "12M",
            .rating = "4.8"
        },
        {
            .id = "rust-lang.rust-analyzer",
            .name = "rust-analyzer",
            .version = "0.3.2208",
            .description = "Rust language support using the rust-analyzer Language Server Protocol server",
            .publisher = "rust-lang",
            .repository_url = "https://github.com/rust-lang/rust-analyzer.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/rust-lang/rust-analyzer/master/crates/rust-analyzer/res/icons/rust-analyzer.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"rust", "cargo", "rust-analyzer", "lsp"},
            .downloads = "3.4M",
            .rating = "4.9"
        },
        {
            .id = "microsoft.pyright",
            .name = "Pyright",
            .version = "1.1.390",
            .description = "Fast, comprehensive static type checker and Language Server for Python",
            .publisher = "Microsoft",
            .repository_url = "https://github.com/microsoft/pyright.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/microsoft/pyright/main/packages/vscode-pyright/images/pyright-icon.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"python", "pyright", "lsp", "types"},
            .downloads = "4.2M",
            .rating = "4.8"
        },
        {
            .id = "golang.go",
            .name = "Go",
            .version = "0.44.0",
            .description = "Rich Go language support using gopls language server",
            .publisher = "golang",
            .repository_url = "https://github.com/golang/vscode-go.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/golang/vscode-go/master/images/go-logo-blue.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"go", "golang", "gopls", "lsp"},
            .downloads = "11M",
            .rating = "4.7"
        },
        {
            .id = "zigtools.zls",
            .name = "ZLS (Zig Language Server)",
            .version = "0.14.0",
            .description = "Zig Language Server providing code completion, hover, and diagnostics",
            .publisher = "zigtools",
            .repository_url = "https://github.com/zigtools/zls.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/zigtools/zls/master/assets/icon.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"zig", "zls", "lsp"},
            .downloads = "540K",
            .rating = "4.9"
        },
        {
            .id = "sumneko.lua",
            .name = "Lua Language Server",
            .version = "3.13.5",
            .description = "Language Server for Lua with autocomplete, type checking, and diagnostics",
            .publisher = "sumneko",
            .repository_url = "https://github.com/LuaLS/lua-language-server.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/LuaLS/lua-language-server/master/images/icon.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"lua", "lsp", "luals"},
            .downloads = "2.8M",
            .rating = "4.9"
        },
        {
            .id = "typescript-language-server",
            .name = "TypeScript Language Server",
            .version = "4.3.3",
            .description = "Language Server Protocol implementation for TypeScript and JavaScript using tsserver",
            .publisher = "TypeFox",
            .repository_url = "https://github.com/typescript-language-server/typescript-language-server.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/typescript-language-server/typescript-language-server/master/packages/vscode/icon.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"typescript", "javascript", "lsp", "tsserver"},
            .downloads = "5.1M",
            .rating = "4.8"
        },
        {
            .id = "Shopify.ruby-lsp",
            .name = "Ruby LSP",
            .version = "0.7.15",
            .description = "Ruby Language Server Protocol support from Shopify",
            .publisher = "Shopify",
            .repository_url = "https://github.com/Shopify/ruby-lsp.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/Shopify/ruby-lsp/main/images/ruby-lsp-icon.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"ruby", "lsp", "shopify"},
            .downloads = "1.2M",
            .rating = "4.7"
        },
        {
            .id = "ms-python.python",
            .name = "Python",
            .version = "2024.18.0",
            .description = "Python language support with virtual environment & pyright",
            .publisher = "ms-python",
            .repository_url = "https://github.com/microsoft/vscode-python.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/microsoft/vscode-python/main/images/python.png",
            .sha256 = "",
            .category = "lsp",
            .tags = {"python", "pyright", "debug", "lsp"},
            .downloads = "120M",
            .rating = "4.7"
        },
        {
            .id = "ms-python.python-debugger",
            .name = "Python Debugger",
            .version = "2024.12.0",
            .description = "Python Debugger extension using debugpy",
            .publisher = "ms-python",
            .repository_url = "https://github.com/microsoft/vscode-python-debugger.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/microsoft/vscode-python-debugger/main/images/python.png",
            .sha256 = "",
            .category = "tools",
            .tags = {"python", "debugger", "debugpy"},
            .downloads = "48M",
            .rating = "4.8"
        },
        {
            .id = "DavidAnson.vscode-markdownlint",
            .name = "markdownlint",
            .version = "0.57.0",
            .description = "Markdown linting and style checking for Visual Studio Code & ZDE",
            .publisher = "DavidAnson",
            .repository_url = "https://github.com/DavidAnson/vscode-markdownlint.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/DavidAnson/vscode-markdownlint/main/images/markdownlint.png",
            .sha256 = "",
            .category = "tools",
            .tags = {"markdown", "lint", "docs"},
            .downloads = "1.7M",
            .rating = "5"
        },
        {
            .id = "donjayamanne.githistory",
            .name = "Git History",
            .version = "0.6.20",
            .description = "View git log, file history, compare branches and commits",
            .publisher = "donjayamanne",
            .repository_url = "https://github.com/donjayamanne/gitHistoryVSCode.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/donjayamanne/gitHistoryVSCode/master/images/icon.png",
            .sha256 = "",
            .category = "tools",
            .tags = {"git", "sourcecontrol", "history"},
            .downloads = "629K",
            .rating = "5"
        },
        {
            .id = "GitKraken.gitlens",
            .name = "GitLens — Git supercharged",
            .version = "15.0.0",
            .description = "Supercharge Git within Visual Studio Code & ZDE",
            .publisher = "GitKraken",
            .repository_url = "https://github.com/gitkraken/vscode-gitlens.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/gitkraken/vscode-gitlens/main/images/icon.png",
            .sha256 = "",
            .category = "tools",
            .tags = {"git", "gitlens", "vcs"},
            .downloads = "16M",
            .rating = "4.8"
        },
        {
            .id = "ms-azuretools.vscode-docker",
            .name = "Docker",
            .version = "1.29.0",
            .description = "Makes it easy to create, manage, and debug containerized applications",
            .publisher = "Microsoft",
            .repository_url = "https://github.com/microsoft/vscode-docker.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/microsoft/vscode-docker/main/resources/docker.png",
            .sha256 = "",
            .category = "tools",
            .tags = {"docker", "containers"},
            .downloads = "24M",
            .rating = "4.5"
        },
        {
            .id = "org.zenvra.emulator.qemu",
            .name = "QEMU Virtual Machine & System Emulator",
            .version = "8.2.0",
            .description = "Run, debug and emulate x86, ARM, RISC-V and MIPS targets directly from ZDE",
            .publisher = "QEMU Project",
            .repository_url = "https://github.com/qemu/qemu.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/qemu/qemu/master/ui/icons/qemu_32x32.png",
            .sha256 = "",
            .category = "emulators",
            .tags = {"emulator", "qemu", "vm", "virtualization", "system"},
            .downloads = "890K",
            .rating = "4.9"
        },
        {
            .id = "org.zenvra.emulator.gdb",
            .name = "GDB Embedded Target Emulator & Debugger",
            .version = "14.1.0",
            .description = "Embedded microcontroller emulator, JTAG stub and remote GDB target integration",
            .publisher = "GNU Toolchain",
            .repository_url = "https://github.com/bminor/binutils-gdb.git",
            .download_url = "",
            .icon_url = "https://avatars.githubusercontent.com/u/1027025?s=128",
            .sha256 = "",
            .category = "emulators",
            .tags = {"emulator", "debugger", "gdb", "embedded", "arm"},
            .downloads = "410K",
            .rating = "4.8"
        },
        {
            .id = "org.zenvra.emulator.wasm",
            .name = "Wasmtime WebAssembly Sandbox & Emulator",
            .version = "19.0.0",
            .description = "Fast and secure WebAssembly emulator runtime with WASI preview 2 execution",
            .publisher = "Bytecode Alliance",
            .repository_url = "https://github.com/bytecodealliance/wasmtime.git",
            .download_url = "",
            .icon_url = "https://raw.githubusercontent.com/bytecodealliance/wasmtime/main/docs/logo.png",
            .sha256 = "",
            .category = "emulators",
            .tags = {"emulator", "wasm", "webassembly", "runtime", "sandbox"},
            .downloads = "320K",
            .rating = "4.9"
        }
    };
}

void MarketplaceClient::clear() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

std::optional<std::filesystem::path> MarketplaceClient::get_cached_icon(
    const std::string& plugin_id,
    const std::string& icon_url,
    std::function<void()> on_downloaded) const
{
    if (plugin_id.empty())
    {
        return std::nullopt;
    }

    std::string target_url = icon_url;
    if (target_url.empty())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& entry : m_entries)
        {
            if (entry.id == plugin_id)
            {
                target_url = entry.icon_url;
                break;
            }
        }
    }

    if (target_url.empty())
    {
        return std::nullopt;
    }

    std::string ext = ".png";
    std::string lower_url = to_lower(target_url);
    auto query_pos = lower_url.find('?');
    if (query_pos != std::string::npos)
    {
        lower_url = lower_url.substr(0, query_pos);
    }
    if (lower_url.ends_with(".svg"))
    {
        ext = ".svg";
    }
    else if (lower_url.ends_with(".jpg") || lower_url.ends_with(".jpeg"))
    {
        ext = ".jpg";
    }

    std::filesystem::path cache_dir = m_cache_dir;
    if (cache_dir.empty())
    {
        const char* userprofile = std::getenv("USERPROFILE");
        if (userprofile)
        {
            cache_dir = std::filesystem::path(userprofile) / ".zde" / "cache" / "icons";
        }
        else
        {
            cache_dir = "cache/icons";
        }
    }

    std::string sanitized_id = plugin_id;
    for (char& c : sanitized_id)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            c = '_';
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);

    std::filesystem::path cached_path = cache_dir / (sanitized_id + ext);
    if (std::filesystem::exists(cached_path, ec) && std::filesystem::is_regular_file(cached_path, ec))
    {
        auto sz = std::filesystem::file_size(cached_path, ec);
        if (!ec && sz > 0)
        {
            return cached_path;
        }
    }

    // Not yet cached; initiate background download if not already pending
    {
        std::lock_guard<std::mutex> lock(m_icon_mutex);
        if (std::find(m_pending_downloads.begin(), m_pending_downloads.end(), plugin_id) != m_pending_downloads.end())
        {
            return std::nullopt;
        }
        m_pending_downloads.push_back(plugin_id);
    }

    std::thread([this, plugin_id, target_url, cached_path, on_downloaded = std::move(on_downloaded)]() {
#ifdef _WIN32
        bool ok = download_http_file(target_url, cached_path);
#else
        bool ok = false;
#endif
        {
            std::lock_guard<std::mutex> lock(m_icon_mutex);
            auto it = std::find(m_pending_downloads.begin(), m_pending_downloads.end(), plugin_id);
            if (it != m_pending_downloads.end())
            {
                m_pending_downloads.erase(it);
            }
        }

        if (ok && on_downloaded)
        {
            try
            {
                on_downloaded();
            }
            catch (...)
            {
            }
        }
    }).detach();

    return std::nullopt;
}

} // namespace Zenvra::Plugins::Marketplace
