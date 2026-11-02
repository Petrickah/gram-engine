#pragma once

GRAM_NAMESPACE_BEGIN

struct Greeting;

class GameAPILoader : public Layer<GLFWApplication>
{
public:
    struct GameAPI
    {
        uint32_t Version;
        const GameAPI* (*OnLoad)(const Gram::HostAPI* host);
        void (*OnUnload)();
        void (*Greet)(Greeting* greeting);
    };

    static const uint32_t GameAPIVersion = 1;
    using GetGameAPIFunction = const GameAPI* (*)();

public:
    ~GameAPILoader()
    {
        if (m_Game)
        {
            m_Game->OnUnload();
            m_Game = nullptr;
        }
        
        m_GameLibrary.reset();

        std::error_code error;
        std::filesystem::remove_all(m_Workspace / "hot", error);
        GramCheckVoid(!error, "{}: couldn't remove the hot directory: {}", m_Name, error.message());
    }

    GameAPILoader(const Gram::GLFWApplicationSpecification &spec, const std::string& libraryName)
        : m_Name(libraryName)
        , m_Workspace(spec.m_Workspace)
    {
        std::error_code error;
        std::filesystem::remove_all(m_Workspace / "hot", error);
        GramCheckVoid(!error, "{}: couldn't remove the hot directory: {}", m_Name, error.message());

        Reload();
    }

    void Reload();
    const GameAPI* GetGameAPI() { return m_Game; }
    
private:
    void CopyOnLoad();
    const GameAPI* FindGameAPI();

    virtual bool OnDrawFrame() override;
    
private:
    std::filesystem::path                 m_Workspace;
    const GameAPI*                        m_Game        = nullptr;
    std::unique_ptr<Gram::DynamicLibrary> m_GameLibrary = nullptr;
    std::chrono::steady_clock::time_point m_LastCheck;          // last check
    std::filesystem::file_time_type       m_LoadedWriteTime;    // the date of the original at last load
    std::string m_Name;                   // the name of the loaded library
    std::filesystem::path m_LoadedCopy;   // the currently loaded copy, it gets deleted at next reload
    uint32_t m_LoadCount = 0;             // gives the unique name of the copy
};

GRAM_NAMESPACE_END