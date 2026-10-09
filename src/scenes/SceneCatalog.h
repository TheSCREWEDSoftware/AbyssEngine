#ifndef ABYSS_SCENE_CATALOG_H
#define ABYSS_SCENE_CATALOG_H

#include "Scene.h"

// Browse the game's items (weapons, armor) using the data tables, strings and inventory pictures from the MPQs.
DESCRIBE_SCENE_CALLBACKS(Catalog);

void *Catalog_Create(void);
void  Catalog_Render(void *scene_ref);
void  Catalog_Update(void *scene_ref, uint64_t delta);
void  Catalog_Free(void **scene_ref);

#endif // ABYSS_SCENE_CATALOG_H
