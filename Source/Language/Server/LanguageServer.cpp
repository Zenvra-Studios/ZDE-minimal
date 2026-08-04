#include "Server/LanguageServer.h"

namespace Zenvra::Language::Server
{

struct LanguageServer::Impl
{
    std::string workspace_path;
};

LanguageServer::LanguageServer() = default;

LanguageServer::~LanguageServer() = default;

bool LanguageServer::initialize(const std::string& workspace_path)
{
    m_impl = std::make_unique<Impl>();
    m_impl->workspace_path = workspace_path;
    return true;
}

void LanguageServer::shutdown()
{
    m_impl.reset();
}

} // namespace Zenvra::Language::Server
