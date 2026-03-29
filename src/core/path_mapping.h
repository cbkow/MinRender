#pragma once

#include "core/config.h"

#include <string>
#include <vector>
#include <algorithm>

namespace MR {

/// Returns "win", "mac", or "lin" for the current OS.
inline std::string currentOsTag()
{
#if defined(_WIN32)
    return "win";
#elif defined(__APPLE__)
    return "mac";
#else
    return "lin";
#endif
}

/// Get the path prefix for the given OS tag from a PathMapping.
inline const std::string& mappingPrefix(const PathMapping& m, const std::string& os)
{
    if (os == "win") return m.win;
    if (os == "mac") return m.mac;
    return m.lin;
}

/// Normalize a path to forward slashes for comparison.
inline std::string normalizePath(const std::string& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

/// Convert a path to native OS separators.
inline std::string toNativeSeparators(const std::string& path, const std::string& os)
{
    std::string result = path;
    if (os == "win")
        std::replace(result.begin(), result.end(), '/', '\\');
    else
        std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

/// Translate a path from sourceOS to targetOS using the given mappings.
/// Returns the original path (with separator normalization) if no mapping matches.
inline std::string translatePath(
    const std::string& sourceOS,
    const std::string& targetOS,
    const std::string& path,
    const std::vector<PathMapping>& mappings)
{
    if (sourceOS == targetOS)
        return toNativeSeparators(path, targetOS);

    std::string normalized = normalizePath(path);

    for (const auto& m : mappings)
    {
        if (!m.enabled) continue;

        std::string srcPrefix = normalizePath(mappingPrefix(m, sourceOS));
        const std::string& tgtPrefix = mappingPrefix(m, targetOS);

        if (srcPrefix.empty() || tgtPrefix.empty()) continue;

        // Case-insensitive match on Windows source
        bool matches = false;
        if (sourceOS == "win")
        {
            std::string normLower = normalized;
            std::string srcLower = srcPrefix;
            std::transform(normLower.begin(), normLower.end(), normLower.begin(), ::tolower);
            std::transform(srcLower.begin(), srcLower.end(), srcLower.begin(), ::tolower);
            matches = normLower.rfind(srcLower, 0) == 0;
        }
        else
        {
            matches = normalized.rfind(srcPrefix, 0) == 0;
        }

        if (matches)
        {
            std::string remainder = normalized.substr(srcPrefix.size());
            // Strip leading slash from remainder
            if (!remainder.empty() && remainder[0] == '/')
                remainder = remainder.substr(1);

            std::string normTarget = normalizePath(tgtPrefix);
            // Strip trailing slash from target
            while (!normTarget.empty() && normTarget.back() == '/')
                normTarget.pop_back();

            std::string result = remainder.empty()
                ? normTarget
                : normTarget + "/" + remainder;

            return toNativeSeparators(result, targetOS);
        }
    }

    // No mapping matched — normalize separators only
    return toNativeSeparators(path, targetOS);
}

} // namespace MR
