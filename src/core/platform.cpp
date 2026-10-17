#include "core/platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <netfw.h>
#include <comdef.h>
#else
#include <unistd.h>
#include <cstdlib>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>

#ifdef _WIN32
#pragma comment(lib, "advapi32.lib")
#endif

namespace MR {

std::filesystem::path getAppDataDir()
{
#ifdef _WIN32
    wchar_t* appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
    {
        std::filesystem::path dir = std::filesystem::path(appData) / L"MinRender";
        CoTaskMemFree(appData);
        ensureDir(dir);
        return dir;
    }
    CoTaskMemFree(appData);
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home)
    {
        auto dir = std::filesystem::path(home) / "Library" / "Application Support" / "MinRender";
        ensureDir(dir);
        return dir;
    }
#elif defined(__linux__)
    // XDG_DATA_HOME or ~/.local/share
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg)
    {
        auto dir = std::filesystem::path(xdg) / "MinRender";
        ensureDir(dir);
        return dir;
    }
    const char* home = std::getenv("HOME");
    if (home)
    {
        auto dir = std::filesystem::path(home) / ".local" / "share" / "MinRender";
        ensureDir(dir);
        return dir;
    }
#endif
    // Fallback
    auto dir = std::filesystem::current_path() / "MinRender_data";
    ensureDir(dir);
    return dir;
}

bool ensureDir(const std::filesystem::path& path)
{
    try
    {
        if (!std::filesystem::exists(path))
        {
            return std::filesystem::create_directories(path);
        }
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Platform] Failed to create directory: " << e.what() << std::endl;
        return false;
    }
}

std::string getOS()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

std::string getHostname()
{
#ifdef _WIN32
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buf) / sizeof(buf[0]);
    if (GetComputerNameW(buf, &size))
    {
        // Convert wide to narrow via WideCharToMultiByte
        int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(size), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(size), result.data(), len, nullptr, nullptr);
        return result;
    }
#else
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0)
        return buf;
#endif
    return "unknown";
}

void openFolderInExplorer(const std::filesystem::path& folder)
{
#ifdef _WIN32
    ShellExecuteW(nullptr, L"explore", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + folder.string() + "\"";
    std::system(cmd.c_str());
#elif defined(__linux__)
    std::string cmd = "xdg-open \"" + folder.string() + "\"";
    std::system(cmd.c_str());
#endif
}

void openUrl(const std::string& url)
{
#ifdef _WIN32
    std::wstring wurl(url.begin(), url.end());
    ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#elif defined(__linux__)
    std::string cmd = "xdg-open \"" + url + "\"";
    std::system(cmd.c_str());
#endif
}

bool addFirewallRule(const std::string& ruleName, uint16_t tcpPort, uint16_t udpPort)
{
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool weInitialized = SUCCEEDED(hr);
    // RPC_E_CHANGED_MODE means COM was already initialized — that's fine
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    bool success = false;
    INetFwPolicy2* policy = nullptr;
    INetFwRules* rules = nullptr;

    hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                          __uuidof(INetFwPolicy2), reinterpret_cast<void**>(&policy));
    if (FAILED(hr))
        goto cleanup;

    hr = policy->get_Rules(&rules);
    if (FAILED(hr))
        goto cleanup;

    {
        // Remove old rules by name (ignore errors — rule may not exist)
        _bstr_t bstrName(ruleName.c_str());
        rules->Remove(bstrName);
        _bstr_t bstrNameUdp((ruleName + " UDP").c_str());
        rules->Remove(bstrNameUdp);

        // Add TCP rule
        {
            INetFwRule* rule = nullptr;
            hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwRule), reinterpret_cast<void**>(&rule));
            if (SUCCEEDED(hr))
            {
                rule->put_Name(bstrName);
                rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
                rule->put_LocalPorts(_bstr_t(std::to_wstring(tcpPort).c_str()));
                rule->put_Direction(NET_FW_RULE_DIR_IN);
                rule->put_Action(NET_FW_ACTION_ALLOW);
                rule->put_Enabled(VARIANT_TRUE);

                hr = rules->Add(rule);
                success = SUCCEEDED(hr);
                rule->Release();
            }
        }

        // Add UDP rule
        if (success && udpPort > 0)
        {
            INetFwRule* rule = nullptr;
            hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwRule), reinterpret_cast<void**>(&rule));
            if (SUCCEEDED(hr))
            {
                rule->put_Name(bstrNameUdp);
                rule->put_Protocol(NET_FW_IP_PROTOCOL_UDP);
                rule->put_LocalPorts(_bstr_t(std::to_wstring(udpPort).c_str()));
                rule->put_Direction(NET_FW_RULE_DIR_IN);
                rule->put_Action(NET_FW_ACTION_ALLOW);
                rule->put_Enabled(VARIANT_TRUE);

                hr = rules->Add(rule);
                success = success && SUCCEEDED(hr);
                rule->Release();
            }
        }
    }

cleanup:
    if (rules) rules->Release();
    if (policy) policy->Release();
    if (weInitialized) CoUninitialize();
    return success;
#else
    (void)ruleName;
    (void)tcpPort;
    (void)udpPort;
    return false;
#endif
}

bool isSafePathSegment(const std::string& seg)
{
    if (seg.empty() || seg == "." || seg == "..")
        return false;
    return seg.find('/') == std::string::npos
        && seg.find('\\') == std::string::npos
        && seg.find(':') == std::string::npos;
}

bool isPathWithin(const std::filesystem::path& base, const std::filesystem::path& p)
{
    std::error_code ec;
    std::string b = std::filesystem::absolute(base, ec).lexically_normal().generic_string();
    std::string c = std::filesystem::absolute(p, ec).lexically_normal().generic_string();
    if (b.empty() || c.empty())
        return false;
    while (b.size() > 1 && b.back() == '/') b.pop_back();
    while (c.size() > 1 && c.back() == '/') c.pop_back();
#ifdef _WIN32
    // Windows paths compare case-insensitively.
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return s;
    };
    b = lower(std::move(b));
    c = lower(std::move(c));
#endif
    // Strictly inside: longer than base and the next char is a separator,
    // so "/farm/jobs" is not within "/farm/jobs" and "/farm/jobsX" is not
    // within "/farm/jobs".
    if (c.size() <= b.size() + 1)
        return false;
    return c.compare(0, b.size(), b) == 0 && c[b.size()] == '/';
}

void publishNodeState(const std::string& renderState, const std::string& nodeState, bool alive)
{
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MinRender\\Node", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr)
        != ERROR_SUCCESS)
        return;

    auto setString = [&](const wchar_t* name, const std::string& value) {
        std::wstring w(value.begin(), value.end());   // ASCII state words only
        RegSetValueExW(key, name, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(w.c_str()),
            static_cast<DWORD>((w.size() + 1) * sizeof(wchar_t)));
    };
    setString(L"RenderState", renderState);
    setString(L"NodeState", nodeState);

    DWORD pid = alive ? GetCurrentProcessId() : 0;
    RegSetValueExW(key, L"Pid", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&pid), sizeof(pid));

    ULONGLONG nowMs = static_cast<ULONGLONG>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    RegSetValueExW(key, L"UpdatedMs", 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&nowMs), sizeof(nowMs));

    RegCloseKey(key);
#else
    (void)renderState;
    (void)nodeState;
    (void)alive;
#endif
}

} // namespace MR
