#ifndef ABYSS_LOADING_SCREEN_H
#define ABYSS_LOADING_SCREEN_H

// The game's animated loading screen, shown while something slow happens on the main thread (opening a scene,
// building the Bestiary, loading a map). Between Begin and End, every file read from the MPQs redraws it (at most
// every few frames), so the window keeps animating and responding instead of freezing. Quick loads (under about a
// tenth of a second) never show it. Calls nest.
void LoadingScreen_Begin(const char *text);
void LoadingScreen_Tick(void); // for long loops that don't read files
void LoadingScreen_End(void);
void LoadingScreen_Release(void);

#endif // ABYSS_LOADING_SCREEN_H
