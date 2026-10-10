#ifndef ABYSS_LOADING_SCREEN_H
#define ABYSS_LOADING_SCREEN_H

// The game's loading screen, shown while something slow happens on the main thread (opening a scene, building the
// Bestiary, loading a map). Between Begin and End every file read from the MPQs redraws it (at most every few
// frames), so the window keeps responding instead of freezing. Quick loads (under about a tenth of a second) never
// show it. Calls nest; the outermost caption is shown.
//
// Progress (the bar, and how far the picture's animation has played) is estimated from the number of files read,
// compared with how many the same load read last time (remembered in loading.txt next to abyss.ini). The very first
// time, it creeps towards the end without reaching it.
void LoadingScreen_Begin(const char *text);
void LoadingScreen_Tick(void); // for long loops that don't read files
void LoadingScreen_End(void);
void LoadingScreen_Release(void);

#endif // ABYSS_LOADING_SCREEN_H
