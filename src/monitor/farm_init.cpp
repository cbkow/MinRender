#include "monitor/farm_init.h"
#include "core/config.h"
#include "core/platform.h"
#include "core/monitor_log.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace MR {

namespace fs = std::filesystem;

static std::string generateApiSecret()
{
    unsigned char buf[32] = {};
#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(nullptr, buf, sizeof(buf), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status))
        return {};
#else
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(buf), sizeof(buf)))
        return {};
#endif
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : buf)
        oss << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

static std::string getExeDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
#else
    return ".";
#endif
}

static fs::path findBundledTemplatesDir()
{
    fs::path dir = fs::path(getExeDir()) / "resources" / "templates";
    if (fs::is_directory(dir))
        return dir;
    return {};
}

static fs::path findBundledPluginsDir()
{
    fs::path dir = fs::path(getExeDir()) / "resources" / "plugins";
    if (fs::is_directory(dir))
        return dir;
    return {};
}

// Delete any .json in dest that isn't present in bundle. Safe because these
// dirs (templates/examples and templates/plugins) are bundle-managed; user
// overrides live in templates/ (non-recursive).
static void pruneStaleJson(const fs::path& destDir, const fs::path& bundleDir)
{
    std::error_code ec;
    if (!fs::is_directory(destDir, ec) || !fs::is_directory(bundleDir, ec))
        return;

    std::set<std::string> expected;
    for (auto& entry : fs::directory_iterator(bundleDir, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            expected.insert(entry.path().filename().string());
    }

    for (auto& entry : fs::directory_iterator(destDir, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json")
            continue;
        auto name = entry.path().filename().string();
        if (expected.count(name) == 0)
        {
            fs::remove(entry.path(), ec);
            if (!ec)
                MonitorLog::instance().info("farm", "Pruned stale template: " + name);
        }
    }
}

static void copyExampleTemplates(const fs::path& farmPath)
{
    auto bundled = findBundledTemplatesDir();
    if (bundled.empty())
    {
        MonitorLog::instance().warn("farm", "No bundled templates found, skipping example copy");
        return;
    }

    auto destDir = farmPath / "templates" / "examples";
    std::error_code ec;

    // Copy top-level *.json files
    for (auto& entry : fs::directory_iterator(bundled, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            auto dest = destDir / entry.path().filename();
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                MonitorLog::instance().info("farm", "Copied template: " + entry.path().filename().string());
        }
    }

    // Remove .json files that no longer ship in the bundle (e.g. renamed templates)
    pruneStaleJson(destDir, bundled);

    // Copy plugins/*.json into templates/plugins/ (separate from examples so
    // they don't appear in the Monitor's template picker — only DCC plugins scan this dir)
    auto pluginTemplatesDir = bundled / "plugins";
    if (fs::is_directory(pluginTemplatesDir, ec))
    {
        auto pluginDestDir = farmPath / "templates" / "plugins";
        fs::create_directories(pluginDestDir, ec);

        for (auto& entry : fs::directory_iterator(pluginTemplatesDir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                auto dest = pluginDestDir / entry.path().filename();
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
                if (!ec)
                    MonitorLog::instance().info("farm", "Copied plugin template: " + entry.path().filename().string());
            }
        }

        pruneStaleJson(pluginDestDir, pluginTemplatesDir);
    }
}

static void copyPlugins(const fs::path& farmPath)
{
    auto bundled = findBundledPluginsDir();
    if (bundled.empty())
    {
        MonitorLog::instance().warn("farm", "No bundled plugins found, skipping plugin copy");
        return;
    }

    std::error_code ec;
    for (auto& appDir : fs::directory_iterator(bundled, ec))
    {
        if (!appDir.is_directory(ec)) continue;

        auto destDir = farmPath / "plugins" / appDir.path().filename();
        fs::create_directories(destDir, ec);

        for (auto& entry : fs::directory_iterator(appDir.path(), ec))
        {
            if (!entry.is_regular_file(ec)) continue;
            auto dest = destDir / entry.path().filename();
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                MonitorLog::instance().info("farm", "Copied plugin: " +
                    appDir.path().filename().string() + "/" + entry.path().filename().string());
        }
    }
}

FarmInit::Result FarmInit::init(const fs::path& farmPath, const std::string& nodeId)
{
    Result result;
    std::error_code ec;

    auto farmJsonPath = farmPath / "farm.json";
    bool hasFarmJson = fs::exists(farmJsonPath, ec);

    if (!hasFarmJson)
    {
        // First run — write farm.json + copy templates + plugins
        MonitorLog::instance().info("farm", "Creating farm.json at: " + farmPath.string());

        fs::create_directories(farmPath / "templates" / "examples", ec);
        fs::create_directories(farmPath / "plugins", ec);

        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string apiSecret = generateApiSecret();

        nlohmann::json farmJson = {
            {"_version", 1},
            {"protocol_version", PROTOCOL_VERSION},
            {"created_by", nodeId},
            {"created_at_ms", nowMs},
            {"last_example_update", APP_VERSION},
            {"api_secret", apiSecret}
        };

        std::ofstream ofs(farmJsonPath);
        if (ofs.is_open())
        {
            ofs << farmJson.dump(2);
            ofs.close();
        }
        else
        {
            result.error = "Failed to write farm.json";
            return result;
        }

        copyExampleTemplates(farmPath);
        copyPlugins(farmPath);

        MonitorLog::instance().info("farm", "Farm initialized");
    }
    else
    {
        // Check if example templates need update + ensure api_secret exists
        try
        {
            std::ifstream ifs(farmJsonPath);
            nlohmann::json fj = nlohmann::json::parse(ifs);
            ifs.close();

            bool needsWrite = false;

            // Upgrade path: generate api_secret if missing
            if (!fj.contains("api_secret") || !fj["api_secret"].is_string() ||
                fj["api_secret"].get<std::string>().empty())
            {
                fj["api_secret"] = generateApiSecret();
                needsWrite = true;
                MonitorLog::instance().info("farm", "Generated api_secret for existing farm");
            }

            std::string lastUpdate;
            if (fj.contains("last_example_update"))
                lastUpdate = fj["last_example_update"].get<std::string>();

            if (lastUpdate != APP_VERSION)
            {
                MonitorLog::instance().info("farm",
                    "Updating examples (" + lastUpdate + " -> " + std::string(APP_VERSION) + ")");

                copyExampleTemplates(farmPath);
                copyPlugins(farmPath);

                fj["last_example_update"] = APP_VERSION;
                needsWrite = true;
            }

            if (needsWrite)
            {
                std::ofstream ofsUpdate(farmJsonPath);
                if (ofsUpdate.is_open())
                    ofsUpdate << fj.dump(2);
            }
        }
        catch (const std::exception& e)
        {
            MonitorLog::instance().warn("farm", "Failed to read farm.json: " + std::string(e.what()));
        }
    }

    result.success = true;
    return result;
}

} // namespace MR
