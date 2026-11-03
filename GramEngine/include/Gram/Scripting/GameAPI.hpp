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
    
private:
    void CopyOnLoad();
    void Run(RegisteredSystem& system, System::Context& ctx);
    const GameAPI* FindGameAPI();

    virtual bool OnDrawFrame() override;
    
private:
    World&                                m_World;
    std::string                           m_Name;                  // the name of the loaded library
    std::filesystem::path                 m_Workspace;             // the workspace location given from the game
    std::chrono::steady_clock::time_point m_StartTime;             // for Context::Time
    std::chrono::steady_clock::time_point m_LastFrame;             // for Context::DeltaTime
    HostAPI                               m_Host;                  // the HostAPI object shared with Rust

    std::vector<PendingEvent>             m_PendingEvents;
    std::vector<RegisteredSystem>         m_Systems;
    std::set<std::string, std::less<>>    m_DisabledSystems;       // by name, survives reloads
    const GameAPI*                        m_Game        = nullptr;
    std::unique_ptr<Gram::DynamicLibrary> m_GameLibrary = nullptr;
    std::chrono::steady_clock::time_point m_LastCheck;             // last check
    std::filesystem::file_time_type       m_LoadedWriteTime;       // the date of the original at last load
    std::filesystem::path m_LoadedCopy;                            // the currently loaded copy, it gets deleted at next reload
    uint32_t m_LoadCount = 0;                                      // gives the unique name of the copy
};

GRAM_NAMESPACE_END