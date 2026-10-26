#pragma once

GRAM_NAMESPACE_BEGIN

// Directory holding the running executable, resolved through the OS rather than argv[0]:
// argv[0] can be a bare name (PATH lookup, or a Windows .exe launched through WSL interop),
// which would turn every asset path into one relative to the working directory.
// Falls back to argv0's parent only if the OS query fails.
std::filesystem::path GetExecutableDirectory(const char* argv0 = nullptr);

GRAM_NAMESPACE_END
