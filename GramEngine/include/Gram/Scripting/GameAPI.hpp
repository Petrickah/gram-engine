#pragma once

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

    static const uint32_t GameAPIVersion = 2;
    using GetGameAPIFunction = const GameAPI* (*)();

public:
    ~GameAPILoader();
    GameAPILoader(const std::filesystem::path& workspace, const std::string& libraryName);

    void Reload();
    void Emit(std::string_view name);
    const GameAPI* GetGameAPI() { return m_Game; }
    
private:
    void CopyOnLoad();
    const GameAPI* FindGameAPI();

    virtual bool OnDrawFrame() override;
    
private:
    std::string                           m_Name;                  // the name of the loaded library
    std::filesystem::path                 m_Workspace;             // the workspace location given from the game
    std::chrono::steady_clock::time_point m_StartTime;             // for Context::Time
    std::chrono::steady_clock::time_point m_LastFrame;             // for Context::DeltaTime
    HostAPI                               m_Host;                  // the HostAPI object shared with Rust
    
    std::vector<std::string>              m_PendingEvents;
    std::vector<System::Descriptor>       m_Systems;
    const GameAPI*                        m_Game        = nullptr;
    std::unique_ptr<Gram::DynamicLibrary> m_GameLibrary = nullptr;
    std::chrono::steady_clock::time_point m_LastCheck;             // last check
    std::filesystem::file_time_type       m_LoadedWriteTime;       // the date of the original at last load
    std::filesystem::path m_LoadedCopy;                            // the currently loaded copy, it gets deleted at next reload
    uint32_t m_LoadCount = 0;                                      // gives the unique name of the copy
};

GRAM_NAMESPACE_END