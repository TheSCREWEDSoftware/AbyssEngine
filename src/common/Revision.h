#ifndef ABYSS_COMMON_REVISION_H
#define ABYSS_COMMON_REVISION_H

// The git commit the engine was built from (generated at build time, like AzerothCore's GitRevision).
const char *Revision_GetHash(void);      // "8e0201c3a1b2", "+" suffix for uncommitted changes, "unknown" without git
const char *Revision_GetDate(void);      // commit date, "2026-10-10 12:34:56 +0100"
const char *Revision_GetBranch(void);    // "main"
const char *Revision_GetPlatform(void);  // "Win64", "Win32", "Linux", "macOS"
const char *Revision_GetBuildType(void); // "Debug", "Release", "RelWithDebInfo", ...
const char *Revision_GetLinkage(void);   // "Static" or "Dynamic" (how SDL/FFmpeg are linked)
// "AbyssEngine rev. 8e0201c3a1b2 2026-10-10 12:34:56 +0100 (main branch) (Win64, RelWithDebInfo, Dynamic)"
const char *Revision_GetFullVersion(void);

#endif // ABYSS_COMMON_REVISION_H
