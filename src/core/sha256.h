#pragma once

#include <string>

namespace MR {

// SHA-256 of `input`, returned as 64 lowercase hex chars.
//
// Self-contained rather than platform crypto (BCrypt / CommonCrypto) so the
// same code path runs on every target and stays available to the headless
// build. Currently used only to fingerprint the farm secret for display; if
// heartbeat signing lands it can back an HMAC-SHA256 too.
std::string sha256Hex(const std::string& input);

// Short, non-reversible identifier for a secret, safe to show in the UI and
// publish on /api/status: the first 8 hex chars of its SHA-256. Enough to
// tell whether two nodes agree, useless for recovering a 256-bit secret.
// Returns an empty string for an empty secret.
std::string secretFingerprint(const std::string& secret);

} // namespace MR
