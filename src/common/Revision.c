#include "Revision.h"
#include "revision_data.h" // generated in the build folder (not "revision.h": Windows would pick Revision.h)

#if defined(_WIN64)
#define ABYSS_PLATFORM_STR "Win64"
#elif defined(_WIN32)
#define ABYSS_PLATFORM_STR "Win32"
#elif defined(__APPLE__)
#define ABYSS_PLATFORM_STR "macOS"
#elif defined(__linux__)
#define ABYSS_PLATFORM_STR "Linux"
#else
#define ABYSS_PLATFORM_STR "Unix"
#endif

// Set per configuration by the build (Visual Studio builds Debug/Release/... from one project)
#ifndef ABYSS_BUILD_TYPE
#define ABYSS_BUILD_TYPE "Unknown"
#endif

const char *Revision_GetHash(void) { return ABYSS_REVISION_HASH; }

const char *Revision_GetDate(void) { return ABYSS_REVISION_DATE; }

const char *Revision_GetBranch(void) { return ABYSS_REVISION_BRANCH; }

const char *Revision_GetPlatform(void) { return ABYSS_PLATFORM_STR; }

const char *Revision_GetBuildType(void) { return ABYSS_BUILD_TYPE; }

const char *Revision_GetLinkage(void) { return ABYSS_REVISION_LINKAGE; }

const char *Revision_GetFullVersion(void) {
    return "AbyssEngine rev. " ABYSS_REVISION_HASH " " ABYSS_REVISION_DATE " (" ABYSS_REVISION_BRANCH
           " branch) (" ABYSS_PLATFORM_STR ", " ABYSS_BUILD_TYPE ", " ABYSS_REVISION_LINKAGE ")";
}
