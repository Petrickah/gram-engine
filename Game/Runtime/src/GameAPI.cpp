#include "Gram/Application/Common.hpp"
#include "Gram/Scripting/HostAPI.hpp"

#include "GameAPI.hpp"

GRAM_NAMESPACE_BEGIN

const GameAPILoader::GameAPI* GameAPILoader::FindGameAPI()
{
    GameAPILoader::GetGameAPIFunction getGameAPIFunc = m_GameLibrary->FindSymbol<GameAPILoader::GetGameAPIFunction>("game_get_api");
    GramCheckNull(getGameAPIFunc, "no valid game api: game_get_api not found");
    if (const GameAPI* gameLogic = getGameAPIFunc())
    {
        GramCheckNull(gameLogic->Version == GameAPILoader::GameAPIVersion, "no valid game api: wrong version");
        return gameLogic->OnLoad(&Gram::g_Host);
    }

    return nullptr;
}

bool GameAPILoader::OnDrawFrame()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_LastCheck < std::chrono::milliseconds(500)) return true;
    m_LastCheck = now;

    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(m_Workspace / Gram::GetDynamicLibraryFileName(m_Name), error);
    if (!error && writeTime != m_LoadedWriteTime) Reload();

    return true;
}

void GameAPILoader::Reload()
{
    if (m_Game)
    {
        m_Game->OnUnload();
        m_Game = nullptr;
    }
    
    CopyOnLoad();
    m_Game = FindGameAPI();
}

void GameAPILoader::CopyOnLoad()
{
    const std::filesystem::path original = m_Workspace / Gram::GetDynamicLibraryFileName(m_Name);
    const std::filesystem::path hotDir   = m_Workspace / "hot";

    std::error_code error;
    if (!m_LoadedCopy.empty()) std::filesystem::remove(m_LoadedCopy, error);
    std::filesystem::create_directories(hotDir, error);

    m_LoadedWriteTime = std::filesystem::last_write_time(original, error);

    // libGameLogic.dylib → hot/libGameLogic_3.dylib
    const std::filesystem::path copy = hotDir / std::format("{}_{}{}",
        original.stem().string(), ++m_LoadCount, original.extension().string());

    std::filesystem::copy_file(original, copy, std::filesystem::copy_options::overwrite_existing, error);

    GramCheckVoid(!error, "{}: couldn't copy {} to {}: {}", m_Name, original.string(), copy.string(), error.message());

    m_LoadedCopy  = copy;
    m_GameLibrary.reset();
    m_GameLibrary = std::make_unique<Gram::DynamicLibrary>(copy);
    GramCheckVoid(m_GameLibrary->IsValid(), "{}: error loading the game logic library", m_Name);
}

GRAM_NAMESPACE_END
