#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Zenvra::Language::Server
{

class LanguageServer
{
public:
    LanguageServer();
    ~LanguageServer();

    bool initialize(const std::string& workspace_path);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Zenvra::Language::Server
