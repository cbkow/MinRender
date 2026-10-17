#pragma once

#include <filesystem>
#include <string>

namespace MR {

// Returns platform app data directory: %LOCALAPPDATA%\MinRender\ on Windows
std::filesystem::path getAppDataDir();

// Creates directory tree if it doesn't exist. Returns true on success.
bool ensureDir(const std::filesystem::path& path);

// Returns "windows", "linux", or "macos"
std::string getOS();

// Returns machine hostname
std::string getHostname();

// Opens a folder in the platform file manager (Explorer, Finder, etc.)
void openFolderInExplorer(const std::filesystem::path& folder);

// Opens a URL in the default browser
void openUrl(const std::string& url);

// Requests elevated privileges to add Windows Firewall rules (TCP + optional UDP).
// Returns true if ShellExecute succeeded (user accepted UAC). No-op on non-Windows.
bool addFirewallRule(const std::string& ruleName, uint16_t tcpPort, uint16_t udpPort = 0);

// True when `seg` is a plain filename usable as a single path component:
// non-empty, not "." or "..", and free of '/', '\' and ':'. Every id that
// the API or the UI turns into a filesystem path (job ids, node ids, log
// names) must pass this; all of them are program-generated and plain, so
// rejecting anything else loses nothing. Shared by the HTTP layer and
// MonitorApp so the check is the same no matter which entry point is used.
bool isSafePathSegment(const std::string& seg);

// True when `p` lies strictly inside `base` after lexical normalisation
// (so "base/jobs/../.." does not qualify). Both are made absolute first.
// Used to confine recursive deletes to the directories the farm owns.
bool isPathWithin(const std::filesystem::path& base, const std::filesystem::path& p);

// Publish this node's live state for out-of-process consumers on the same
// machine (Windows: HKCU\Software\MinRender\Node — RenderState, NodeState,
// Pid, UpdatedMs; no-op elsewhere). `alive` false writes Pid = 0, which is
// the clean-exit marker: a consumer that sees a non-zero Pid that is not
// running knows the app died rather than quit. The app writes facts only
// and never reads this key; it exists so a watchdog can wait on a
// RegNotifyChangeKeyValue instead of polling /api/status.
void publishNodeState(const std::string& renderState, const std::string& nodeState, bool alive);

} // namespace MR
