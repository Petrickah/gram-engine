#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Scripting/HostAPI.hpp"

GRAM_NAMESPACE_BEGIN

GameAPILoader::~GameAPILoader()
{
    if (m_Game)
    {
        m_Game->OnUnload();
        m_Game = nullptr;
    }
    
    m_Systems.clear();
    m_GameLibrary.reset();

    std::error_code error;
    std::filesystem::remove_all(m_Workspace / "hot", error);
    GramCheckVoid(!error, "{}: couldn't remove the hot directory: {}", m_Name, error.message());
}

GameAPILoader::GameAPILoader(World& world, const std::filesystem::path& workspace, const std::string& libraryName)
    : m_World(world)
    , m_Name(libraryName)
    , m_Workspace(workspace)
    , m_StartTime(std::chrono::steady_clock::now())
    , m_LastFrame(std::chrono::steady_clock::now())
    , m_Host(Gram::HostAPI {
        .Host = this,
        .Log  = [](Gram::LogLevel level, const char* message)
        {
            switch (level)
            {
            case Gram::LogLevel::Info:
                GramInfo(message);
                break;
            case Gram::LogLevel::Trace:
                GramTrace(message);
                break;
            case Gram::LogLevel::Warning:
                GramWarning(message);
                break;
            case Gram::LogLevel::Error:
                GramError(message);
                break;
            default:
                GramError("Unknown logging level for: {}", message);
                break;
            }
        },
        .RegisterSystem = [](void* host, const System::Descriptor* desc)
        {
            Gram::GameAPILoader* loader = static_cast<Gram::GameAPILoader*>(host);
            GramCheckVoid(loader && desc, "RegisterSystem: invalid arguments, the loader and system descriptor were nullptr");

            System::Descriptor copy = *desc;           // borrow the descriptor from Rust over to C++ via copy
            copy.Name [sizeof(copy.Name)  - 1] = '\0'; // setup de null terminator, we can't trust Rust is giving it
            copy.Event[sizeof(copy.Event) - 1] = '\0'; // setup de null terminator, we can't trust Rust is giving it
            loader->m_Systems.push_back(copy);
        },
        .GetComponent = [](void* host, ObjectID entity, ComponentID component) -> void*
        {
            auto* loader = static_cast<Gram::GameAPILoader*>(host);
            return loader ? loader->m_World.GetComponent(entity, component) : nullptr;
        }
    })
{
    std::error_code error;
    std::filesystem::remove_all(m_Workspace / "hot", error);
    GramCheckVoid(!error, "{}: couldn't remove the hot directory: {}", m_Name, error.message());

    Reload();
}

const GameAPILoader::GameAPI* GameAPILoader::FindGameAPI()
{
    GameAPILoader::GetGameAPIFunction getGameAPIFunc = m_GameLibrary->FindSymbol<GameAPILoader::GetGameAPIFunction>("game_get_api");
    GramCheckNull(getGameAPIFunc, "no valid game api: game_get_api not found");
    if (const GameAPI* gameLogic = getGameAPIFunc())
    {
        GramCheckNull(gameLogic->Version == GameAPILoader::GameAPIVersion, "no valid game api: wrong version");
        return gameLogic->OnLoad(&m_Host);
    }

    return nullptr;
}

bool GameAPILoader::OnDrawFrame()
{
    const auto now = std::chrono::steady_clock::now();

    System::Context ctx {
        .DeltaTime = std::chrono::duration<float>(now - m_LastFrame).count(),
        .Time      = std::chrono::duration<double>(now - m_StartTime).count(),
    };

    m_LastFrame = now;
    auto events = std::exchange(m_PendingEvents, {});

    if (m_Game)
    {
        for (const System::Descriptor& desc : m_Systems)
        {
            if (desc.Stage != System::Stage::Update) continue;

            m_Game->RunSystem(desc.Index, &ctx);
        }

        for (const PendingEvent& event : events)
        {
            for (const System::Descriptor& desc : m_Systems)
            {
                if (desc.Stage != System::Stage::Event || event.Name != desc.Event) continue;

                ctx.EventTarget = event.Target;
                m_Game->RunSystem(desc.Index, &ctx);
            }
        }
    }

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
}

void GameAPILoader::Emit(std::string_view name, ObjectID target)
{
    m_PendingEvents.push_back(PendingEvent { .Name = std::string(name), .Target = target });
}

void GameAPILoader::CopyOnLoad()
{
    m_Systems.clear();

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
    
    m_Game = FindGameAPI();

    // Clear the systems after a failed attempt to reload
    if (!m_Game && !m_Systems.empty()) m_Systems.clear();
}

GRAM_NAMESPACE_END
