#ifndef ABYSS_CONFIRM_DIALOG_H
#define ABYSS_CONFIRM_DIALOG_H

#include <stdbool.h>

// A question over the screen with two or three answers ("Return to the main menu?" Yes / No). Enter picks the first
// answer, Esc the last. While it's open it takes the mouse and keyboard; the scene under it just draws.
typedef struct ConfirmDialog ConfirmDialog;

typedef enum ConfirmAnswer {
    CONFIRM_NONE = -1, // still open (or not open)
    CONFIRM_FIRST,
    CONFIRM_SECOND,
    CONFIRM_THIRD,
} ConfirmAnswer;

ConfirmDialog *ConfirmDialog_Create(void);
void           ConfirmDialog_Destroy(ConfirmDialog **dialog);

// Opens it: a title, a line or two of text (wrapped), and the answers (third may be NULL for two)
void ConfirmDialog_Open(ConfirmDialog *dialog, const char *title, const char *text, const char *first,
                        const char *second, const char *third);
bool ConfirmDialog_IsOpen(const ConfirmDialog *dialog);
// Esc closes it without an answer (CONFIRM_NONE, and it's no longer open), instead of picking the last one
void ConfirmDialog_EscapeCloses(ConfirmDialog *dialog, bool closes);

// Once a frame while open: the answer picked this frame (which closes it), or CONFIRM_NONE
ConfirmAnswer ConfirmDialog_Update(ConfirmDialog *dialog);
void          ConfirmDialog_Draw(ConfirmDialog *dialog);

#endif // ABYSS_CONFIRM_DIALOG_H
