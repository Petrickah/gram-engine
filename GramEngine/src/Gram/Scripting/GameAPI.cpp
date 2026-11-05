#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/GameAPI.hpp"
#include "Gram/Scripting/HostAPI.hpp"

GRAM_NAMESPACE_BEGIN

// The name of the system Run is running, so HostAPI::Log (which gets no host pointer) can log
// through that system's logger. nullptr between systems. Only the main thread runs systems.
static const char* s_RunningSystem = nullptr;

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
#ifdef GRAM_LOGGER_ENABLED
            // Inside a system, its own logger ("System.tick"); outside (on_load, the panic hook
            // before any system ran), the editor's.
            spdlog::logger* logger = s_RunningSystem ? GetSystemLogger(s_RunningSystem) : GetEditorLogger();
            if (!logger) return;

            switch (level)
            {
            case Gram::LogLevel::Trace:   logger->log(spdlog::level::trace, message); break;
            case Gram::LogLevel::Info:    logger->log(spdlog::level::info,  message); break;
            case Gram::LogLevel::Warning: logger->log(spdlog::level::warn,  message); break;
            case Gram::LogLevel::Error:   logger->log(spdlog::level::err,   message); break;
            default:                      logger->error("Unknown logging level for: {}", message); break;
            }
#else
            (void)level; (void)message;
#endif // #ifdef GRAM_LOGGER_ENABLED
        },
        .RegisterSystem = [](void* host, const System::Descriptor* desc)
        {
            Gram::GameAPILoader* loader = static_cast<Gram::GameAPILoader*>(host);
            GramCheckVoid(loader && desc, "RegisterSystem: invalid arguments, the loader and system descriptor were nullptr");

            System::Descriptor copy = *desc;           // borrow the descriptor from Rust over to C++ via copy
            copy.Name [sizeof(copy.Name)  - 1] = '\0'; // setup de null terminator, we can't trust Rust is giving it
            copy.Event[sizeof(copy.Event) - 1] = '\0'; // setup de null terminator, we can't trust Rust is giving it
            loader->m_Systems.push_back(RegisteredSystem { .Descriptor = copy, .Enabled = !loader->m_DisabledSystems.contains(copy.Name) });
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
        .Time        = std::chrono::duration<double>(now - m_StartTime).count(),
        .EventTarget = InvalidObjectID,   // left out, it would be {0, 0}: a live entity for the Update systems
    };

    m_LastFrame = now;
    auto events = std::exchange(m_PendingEvents, {});

    if (m_Game)
    {
        for (RegisteredSystem& system : m_Systems)
        {
            if (system.Descriptor.Stage != System::Stage::Update) continue;

            Run(system, ctx);
        }

        for (const PendingEvent& event : events)
        {
            for (RegisteredSystem& system : m_Systems)
            {
                if (system.Descriptor.Stage != System::Stage::Event || event.Name != system.Descriptor.Event) continue;

                ctx.EventTarget = event.Target;
                Run(system, ctx);
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

void GameAPILoader::Run(RegisteredSystem& system, System::Context& ctx)
{
    if (!system.Enabled) return;

    const auto start = std::chrono::steady_clock::now();
    s_RunningSystem = system.Descriptor.Name;
    m_Game->RunSystem(system.Descriptor.Index, &ctx);
    s_RunningSystem = nullptr;
    system.LastRunMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void GameAPILoader::SetSystemEnabled(std::string_view name, bool enabled)
{
    if (enabled) m_DisabledSystems.erase(std::string(name));
    else         m_DisabledSystems.emplace(name);

    for (RegisteredSystem& system : m_Systems)
        if (name == system.Descriptor.Name) system.Enabled = enabled;
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
