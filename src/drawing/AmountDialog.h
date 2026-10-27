#ifndef ABYSS_AMOUNT_DIALOG_H
#define ABYSS_AMOUNT_DIALOG_H

#include <stdbool.h>

// "How much?" over the screen (dropping gold, putting it in the stash, offering it in a trade): a title, a line of
// text, the amount (typed, or the mouse wheel and the arrow keys: up and down by one, page up and down by a
// thousand), OK and Cancel. Enter is OK, Esc Cancel. It takes the mouse and keyboard while it's open.

typedef struct AmountDialog AmountDialog;

typedef enum AmountAnswer { AMOUNT_NONE = -1, AMOUNT_OK, AMOUNT_CANCEL } AmountAnswer;

AmountDialog *AmountDialog_Create(void);
void          AmountDialog_Destroy(AmountDialog **dialog);
void AmountDialog_Open(AmountDialog *dialog, const char *title, const char *text, int maximum, int start, int tag);
bool AmountDialog_IsOpen(const AmountDialog *dialog);
int  AmountDialog_Tag(const AmountDialog *dialog); // what it was opened for (the caller's own number)
// Once a frame while open: OK (AmountDialog_Value has the amount) or Cancel, which close it; AMOUNT_NONE otherwise
AmountAnswer AmountDialog_Update(AmountDialog *dialog);
int          AmountDialog_Value(const AmountDialog *dialog);
void         AmountDialog_Draw(AmountDialog *dialog);

#endif // ABYSS_AMOUNT_DIALOG_H
