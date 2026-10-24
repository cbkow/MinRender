#pragma once

#include <filesystem>
#include <string>

namespace MR {

class FarmInit
{
public:
    struct Result { bool success = false; std::string error; };

    // Initialize farm directory structure at farmPath.
    // Creates dirs + farm.json on first run; copies example templates + plugins when this app is newer than the farm's copy.
    static Result init(const std::filesystem::path& farmPath, const std::string& nodeId);
};

} // namespace MR
