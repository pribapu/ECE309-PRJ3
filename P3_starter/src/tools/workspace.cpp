// src/tools/workspace.cpp — Workspace::resolve (spec §4.4).
//
// A purely lexical check: maps a model-supplied, workspace-relative path to
// an absolute path inside root(), without touching the filesystem. Symbolic
// links are handled separately by the provided check_symlinks(), which must
// run afterward.

#include "tools/workspace.h"

namespace fs = std::filesystem;

bool Workspace::resolve(std::string_view user_path, fs::path* out,
                        std::string* reason) const {
    // 1. Empty path.
    if (user_path.empty()) {
        *reason = "empty path";
        return false;
    }

    // 2. A NUL byte would silently truncate the path at the OS level.
    if (user_path.find('\0') != std::string_view::npos) {
        *reason = "path contains a NUL byte";
        return false;
    }

    // 3. Only workspace-relative paths are allowed, even if an absolute path
    //    happens to point inside root.
    fs::path p{std::string(user_path)};
    if (p.is_absolute() || p.has_root_path()) {
        *reason = "absolute paths are not allowed";
        return false;
    }

    // 4. Remove "." components, duplicate slashes, and fold "x/.." pairs.
    fs::path n = p.lexically_normal();

    // 5. After normalization, a leading ".." means the path climbs above
    //    root at some point, even if it comes back inside afterward.
    if (n.begin() != n.end() && *n.begin() == "..") {
        *reason = "path escapes the workspace";
        return false;
    }

    // 6. Build the absolute path inside root().
    if (n == ".") {
        *out = root();
    } else {
        *out = root() / n;
    }
    return true;
}
