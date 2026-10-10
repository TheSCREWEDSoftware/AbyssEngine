#include "Revision.h"
#include "revision.h"

const char *Revision_GetHash(void) { return ABYSS_REVISION_HASH; }

const char *Revision_GetDate(void) { return ABYSS_REVISION_DATE; }

const char *Revision_GetBranch(void) { return ABYSS_REVISION_BRANCH; }

const char *Revision_GetFullVersion(void) {
    return ABYSS_REVISION_HASH " (" ABYSS_REVISION_BRANCH ", " ABYSS_REVISION_DATE ")";
}
