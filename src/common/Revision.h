#ifndef ABYSS_COMMON_REVISION_H
#define ABYSS_COMMON_REVISION_H

// The git commit the engine was built from (generated at build time).
const char *Revision_GetHash(void);   // "8e0201c3a1b2", "+" suffix for uncommitted changes, "unknown" without git
const char *Revision_GetDate(void);   // commit date, "2026-10-10"
const char *Revision_GetBranch(void); // "main"
// "8e0201c3a1b2 (main, 2026-10-10)"
const char *Revision_GetFullVersion(void);

#endif // ABYSS_COMMON_REVISION_H
