#include "Scene.h"
#include "../common/Logging.h"
#include "../drawing/LoadingScreen.h"
#include "../managers/InputManager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void  *current_scene_ptr;
Scene *current_scene;
Scene *next_scene;

void Scene_InitializeManager(void) {
    current_scene     = NULL;
    next_scene        = NULL;
    current_scene_ptr = NULL;
}

void Scene_DestroyManager(void) {
    if (current_scene != NULL) {
        current_scene->free(&current_scene_ptr);
    }

    current_scene_ptr = NULL;
    current_scene     = NULL;
    next_scene        = NULL;
}

void Scene_RenderCurrentScene(void) {
    if (current_scene == NULL) {
        return;
    }

    current_scene->render(current_scene_ptr);
}

void Scene_UpdateCurrentScene(uint64_t delta) {
    if (next_scene != NULL) {
        if (current_scene != NULL) {
            current_scene->free(&current_scene_ptr);
        }

        InputManager_ResetMouseButtons();

        current_scene = next_scene;
        next_scene    = NULL;
        // "FreeRoam" -> "Loading Free Roam..."
        char   caption[96] = "Loading ";
        size_t length      = strlen(caption);
        for (const char *c = current_scene->name; c != NULL && *c != '\0' && length + 5 < sizeof(caption); c++) {
            if (c != current_scene->name && isupper((unsigned char)*c)) {
                caption[length++] = ' ';
            }
            caption[length++] = *c;
        }
        snprintf(caption + length, sizeof(caption) - length, "...");
        LoadingScreen_Begin(caption);
        current_scene_ptr = current_scene->create();
        LoadingScreen_End();
    }

    if (current_scene == NULL) {
        return;
    }

    current_scene->update(current_scene_ptr, delta);
}

Scene *Scene_Current(void **scene_ref) {
    if (scene_ref != NULL) {
        *scene_ref = current_scene_ptr;
    }
    return current_scene;
}

void Scene_Set(Scene *scene) {
    if (next_scene != NULL) {
        LOG_FATAL("Attempted to set a new Scene when one was already queued!");
    }

    next_scene = scene;
}
