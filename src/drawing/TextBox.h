#ifndef ABYSS_TEXT_BOX_H
#define ABYSS_TEXT_BOX_H

#include <stdbool.h>

// Single line text entry. While focused it takes the keyboard (SDL text input); Escape clears the text.
typedef struct TextBox TextBox;

TextBox    *TextBox_Create(const char *font_path, int x, int y, int width, int height, const char *placeholder);
void        TextBox_Destroy(TextBox **text_box);
void        TextBox_Focus(TextBox *text_box);
void        TextBox_Blur(TextBox *text_box);
const char *TextBox_GetText(const TextBox *text_box);
void        TextBox_Clear(TextBox *text_box);

// Processes input. Returns true when the text changed.
bool TextBox_Update(TextBox *text_box);
void TextBox_Draw(const TextBox *text_box);

#endif // ABYSS_TEXT_BOX_H
