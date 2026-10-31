#pragma once

GRAM_NAMESPACE_BEGIN

struct HostAPI
{
    void (*Log)(LogLevel level, const char* message);
};

extern "C" const Gram::HostAPI g_Host;

GRAM_NAMESPACE_END