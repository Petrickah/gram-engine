#pragma once

#include "Gram/Scripting/ECS/World.hpp"

GRAM_NAMESPACE_BEGIN

class  GLFWApplication;

class GameAPILoader : public Layer<GLFWApplication>
{
public:
    struct GameAPI
    {
        uint32_t       Version;
        const GameAPI* (*OnLoad)(const Gram::HostAPI* host);
        void           (*OnUnload)();
        void           (*RunSystem)(uint32_t index, System::Context* ctx);
    };

    struct PendingEvent
    {
        std::string Name;
        ObjectID Target;
    };

    // A registered system plus what only C++ keeps about it.
    struct RegisteredSystem
    {
        System::Descriptor Descriptor;
        bool               Enabled   = true;
        float              LastRunMs = -1.0f;  // how long its last run took; negative: never ran
    };

    enum class ReplayState
    {
		Stopped,
        Building,
        Playing,
        Paused
    };

    static const uint32_t GameAPIVersion = 3;
    using GetGameAPIFunction = const GameAPI* (*)();

public:
    ~GameAPILoader();
    GameAPILoader(World& world, const std::filesystem::path& workspace, const std::string& libraryName);

    void Reload();
    void Emit(std::string_view name, ObjectID target = InvalidObjectID);
    const GameAPI* GetGameAPI() { return m_Game; }
    std::span<const RegisteredSystem> GetSystems() const { return m_Systems; }
    // Kept by name, so a disabled system stays disabled across reloads.
    void SetSystemEnabled(std::string_view name, bool enabled);

    void Play();    // Stopped/Paused/Playing → Building → Playing: rebuilds and reloads the game's library first
                    // (from Playing, the game keeps running while it builds)
    void Pause();   // Playing → Paused
    void Stop();    // Playing/Paused → Stopped, the World back to how it was at Play
    ReplayState GetReplayState() const { return m_ReplayState; }

private:
    void CopyOnLoad();
    void Run(RegisteredSystem& system, System::Context& ctx);
    const GameAPI* FindGameAPI();
    void FinishBuild();    // while Building: once gram is done, reload and play (or go back)
    void EnterPlaying();

    virtual bool OnDrawFrame() override;

private:
    World&                                m_World;
    std::string                           m_Name;                        // the name of the loaded library
    std::filesystem::path                 m_Workspace;                   // the workspace location given from the game
    std::chrono::steady_clock::time_point m_LastFrame;                   // for Context::DeltaTime
    HostAPI                               m_Host;                        // the HostAPI object shared with Rust

    std::vector<PendingEvent>             m_PendingEvents;
    std::vector<RegisteredSystem>         m_Systems;
    std::set<std::string, std::less<>>    m_DisabledSystems;             // by name, survives reloads
	const GameAPI*                        m_Game        = nullptr;
    std::unique_ptr<Gram::DynamicLibrary> m_GameLibrary = nullptr;
    std::chrono::steady_clock::time_point m_LastCheck;                   // last check
    std::filesystem::file_time_type       m_LoadedWriteTime;             // the date of the original at last load
    std::filesystem::path                 m_LoadedCopy;                  // the currently loaded copy, it gets deleted at next reload
    uint32_t                              m_LoadCount = 0;                               // gives the unique name of the copy
    std::optional<World::Snapshot>        m_Snapshot;                                    // the World at Play, restored by Stop
    std::future<int>                      m_Build;                                       // the running `gram build`, while Building
    double                                m_PlayTime = 0.0;                              // Context::Time: only the time spent Playing
    ReplayState                           m_PreviousReplayState = ReplayState::Stopped;  // where a failed build goes back to
#ifdef GRAM_EDITOR_ENABLED                
    ReplayState                           m_ReplayState = ReplayState::Stopped;          // the editor starts the game with Play
#else                                     
    ReplayState                           m_ReplayState = ReplayState::Playing;          // no toolbar: the game runs from the start
#endif // #ifdef GRAM_EDITOR_ENABLED

};

GRAM_NAMESPACE_END