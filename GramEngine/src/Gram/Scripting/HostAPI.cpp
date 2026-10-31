#include "Gram/Scripting/Common.hpp"
#include "Gram/Scripting/HostAPI.hpp"

GRAM_NAMESPACE_BEGIN

const Gram::HostAPI g_Host
{
    .Log = [](Gram::LogLevel level, const char* message)
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
    }
};

GRAM_NAMESPACE_END