#include "Cube.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "Affixes.h"
#include "Inventory.h"
#include "Items.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ---------------------------------------------------------------------------------------------------------------------
// The inside

int Cube_ItemAt(const Character *character, const int cell_x, const int cell_y) {
    for (int i = 0; i < character->cube_count; i++) {
        const InventoryItem *it   = &character->cube[i];
        const ItemBase      *base = Items_Find(it->item.code);
        const int            w    = base != NULL ? base->width : 1;
        const int            h    = base != NULL ? base->height : 1;
        if (cell_x >= it->x && cell_x < it->x + w && cell_y >= it->y && cell_y < it->y + h) {
            return i;
        }
    }
    return -1;
}

int Cube_Covered(const Character *character, const ItemBase *base, const int x, const int y) {
    if (base == NULL || x < 0 || y < 0 || x + base->width > CUBE_W || y + base->height > CUBE_H ||
        strcmp(base->code, CUBE_ITEM) == 0) {
        return -2; // (and the cube doesn't go in itself)
    }
    int found = -1;
    for (int cy = y; cy < y + base->height; cy++) {
        for (int cx = x; cx < x + base->width; cx++) {
            const int at = Cube_ItemAt(character, cx, cy);
            if (at >= 0 && found >= 0 && at != found) {
                return -2;
            }
            found = at >= 0 ? at : found;
        }
    }
    return found;
}

bool Cube_Place(Character *character, const Item *item, const int x, const int y) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL || character->cube_count >= CUBE_MAX || Cube_Covered(character, base, x, y) != -1) {
        return false;
    }
    InventoryItem *slot = &character->cube[character->cube_count++];
    slot->item          = *item;
    slot->x             = x;
    slot->y             = y;
    slot->page          = 0;
    return true;
}

bool Cube_Add(Character *character, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return false;
    }
    for (int x = 0; x + base->width <= CUBE_W; x++) {
        for (int y = 0; y + base->height <= CUBE_H; y++) {
            if (Cube_Place(character, item, x, y)) {
                return true;
            }
        }
    }
    return false;
}

void Cube_Remove(Character *character, const int index) {
    if (index < 0 || index >= character->cube_count) {
        return;
    }
    for (int i = index; i + 1 < character->cube_count; i++) {
        character->cube[i] = character->cube[i + 1];
    }
    character->cube_count--;
}

bool Cube_Carried(const Character *character) {
    for (int i = 0; i < character->inventory_count; i++) {
        if (strcmp(character->inventory[i].item.code, CUBE_ITEM) == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Recipes (cubemain.txt)

#define RECIPE_INPUTS 7
#define RECIPE_MODS   5

enum {
    NEED_NORMAL      = 1,
    NEED_MAGIC       = 2,
    NEED_RARE        = 4,
    NEED_UNIQUE      = 8,
    NEED_SET         = 16,
    NEED_SOCKETS     = 32,
    NEED_NO_SOCK     = 64,
    NEED_NORMAL_TIER = 128, // bas: a normal version (not exceptional or elite)
    NEED_EXCEPTIONAL = 256,
    NEED_ELITE       = 512,
    NEED_ANY_TIER    = 1024,  // upg: the exceptional and elite versions of it do too
    NEED_LOW         = 2048,  // low: low quality (cracked, crude, ...)
    NEED_SUPERIOR    = 4096,  // hiq
    NEED_ETHEREAL    = 8192,  // eth
    NEED_NOT_ETH     = 16384, // noe
};

typedef struct RecipeInput {
    char code[8]; // an item's code or an item type ("gem2", "weap", "any")
    int  qty;
    int  needs;
} RecipeInput;

typedef enum OutputKind { OUT_NEW, OUT_USETYPE, OUT_USEITEM, OUT_COW_PORTAL } OutputKind;

static bool cube_portal = false; // Cube_TakePortal

typedef struct RecipeMod {
    char code[16];
    char param[32];
    int  chance;
    int  min;
    int  max;
} RecipeMod;

typedef struct Recipe {
    char        description[96];
    RecipeInput inputs[RECIPE_INPUTS];
    int         input_count;
    OutputKind  kind;
    char        code[8]; // OUT_NEW: what's made (a code or an item type)
    int         quality; // -1: as made (plain), else ItemQuality
    int         prefix;  // pre=, suf= (rows), -1 none
    int         suffix;
    int         qty;
    int         upgrade; // useitem made exceptional (1) or elite (2), keeping its magic; 0 none
    bool        repair;
    bool        unsocket;
    int         lvl;
    int         plvl;
    int         ilvl;
    RecipeMod   mods[RECIPE_MODS];
    int         mod_count;
} Recipe;

static Recipe *recipes      = NULL;
static int     recipe_count = 0;
static bool    loaded       = false;

// One "input N" ("hpot,qty=3", "weap,mag", "gem2,qty=3"): false when it asks for what the engine doesn't have
static bool Cube__ParseInput(const char *text, RecipeInput *input) {
    memset(input, 0, sizeof(*input));
    input->qty = 1;
    char copy[96];
    snprintf(copy, sizeof(copy), "%s", text);
    char *save = copy;
    int   part = 0;
    for (char *token = save; token != NULL; part++) {
        char *comma = strchr(token, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        if (part == 0) {
            if (strchr(token, ' ') != NULL || strlen(token) >= sizeof(input->code)) {
                return false; // a unique by name ("The Stone of Jordan")
            }
            snprintf(input->code, sizeof(input->code), "%.7s", token);
        } else if (strncmp(token, "qty=", 4) == 0) {
            input->qty = atoi(token + 4) > 0 ? atoi(token + 4) : 1;
        } else if (strcmp(token, "nor") == 0) {
            input->needs |= NEED_NORMAL;
        } else if (strcmp(token, "mag") == 0) {
            input->needs |= NEED_MAGIC;
        } else if (strcmp(token, "rar") == 0) {
            input->needs |= NEED_RARE;
        } else if (strcmp(token, "uni") == 0) {
            input->needs |= NEED_UNIQUE;
        } else if (strcmp(token, "set") == 0) {
            input->needs |= NEED_SET;
        } else if (strcmp(token, "sock") == 0) {
            input->needs |= NEED_SOCKETS;
        } else if (strcmp(token, "nos") == 0) {
            input->needs |= NEED_NO_SOCK;
        } else if (strcmp(token, "bas") == 0) {
            input->needs |= NEED_NORMAL_TIER;
        } else if (strcmp(token, "exc") == 0) {
            input->needs |= NEED_EXCEPTIONAL;
        } else if (strcmp(token, "eli") == 0) {
            input->needs |= NEED_ELITE;
        } else if (strcmp(token, "upg") == 0) {
            input->needs |= NEED_ANY_TIER;
        } else if (strcmp(token, "low") == 0) {
            input->needs |= NEED_LOW;
        } else if (strcmp(token, "hiq") == 0) {
            input->needs |= NEED_SUPERIOR;
        } else if (strcmp(token, "eth") == 0) {
            input->needs |= NEED_ETHEREAL;
        } else if (strcmp(token, "noe") == 0) {
            input->needs |= NEED_NOT_ETH;
        } else {
            return false; // crf, ...: not in this engine
        }
        token = comma != NULL ? comma + 1 : NULL;
    }
    return input->code[0] != '\0';
}

static bool Cube__ParseOutput(const char *text, Recipe *recipe) {
    recipe->quality = -1;
    recipe->prefix  = -1;
    recipe->suffix  = -1;
    recipe->qty     = 1;
    char copy[96];
    snprintf(copy, sizeof(copy), "%s", text);
    int part = 0;
    for (char *token = copy; token != NULL; part++) {
        char *comma = strchr(token, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        if (part == 0) {
            if (strcmp(token, "usetype") == 0) {
                recipe->kind = OUT_USETYPE;
            } else if (strcmp(token, "useitem") == 0) {
                recipe->kind = OUT_USEITEM;
            } else if (strcmp(token, "Cow Portal") == 0) {
                recipe->kind = OUT_COW_PORTAL; // Wirt's leg and a Tome of Town Portal: the Moo Moo Farm
            } else if (strchr(token, ' ') == NULL && strlen(token) < sizeof(recipe->code) && token[0] != '\0') {
                recipe->kind = OUT_NEW;
                snprintf(recipe->code, sizeof(recipe->code), "%.7s", token);
            } else {
                return false; // the portals ("Cow Portal")
            }
        } else if (strcmp(token, "nor") == 0) {
            recipe->quality = ITEM_NORMAL;
        } else if (strcmp(token, "mag") == 0) {
            recipe->quality = ITEM_MAGIC;
        } else if (strcmp(token, "rar") == 0) {
            recipe->quality = ITEM_RARE;
        } else if (strcmp(token, "uni") == 0) {
            recipe->quality = ITEM_UNIQUE;
        } else if (strcmp(token, "crf") == 0) {
            recipe->quality = ITEM_CRAFTED;
        } else if (strncmp(token, "pre=", 4) == 0) {
            recipe->prefix = atoi(token + 4);
        } else if (strncmp(token, "suf=", 4) == 0) {
            recipe->suffix = atoi(token + 4);
        } else if (strncmp(token, "qty=", 4) == 0) {
            recipe->qty = atoi(token + 4) > 0 ? atoi(token + 4) : 1;
        } else if (strcmp(token, "rep") == 0) {
            recipe->repair = true;
        } else if (strcmp(token, "uns") == 0) {
            recipe->unsocket = true;
        } else if (strcmp(token, "exc") == 0) {
            recipe->upgrade = 1;
        } else if (strcmp(token, "eli") == 0) {
            recipe->upgrade = 2;
        } else if (strcmp(token, "rch") != 0 && strcmp(token, "mod") != 0 && strncmp(token, "sock=", 5) != 0) {
            return false; // crf, exc, eli, mod, ...: not in this engine
        }
        token = comma != NULL ? comma + 1 : NULL;
    }
    return true;
}

static void Cube__Load(void) {
    if (loaded) {
        return;
    }
    loaded       = true;
    DataTable *t = DataTable_Load("/data/global/excel/cubemain.txt");
    if (t == NULL) {
        return;
    }
    const int rows = DataTable_GetRowCount(t);
    recipes        = calloc((size_t)(rows > 0 ? rows : 1), sizeof(Recipe));
    FAIL_IF_NULL(recipes);
#define STR(name) DataTable_GetString(t, r, DataTable_GetColumnIndex(t, name))
#define INT(name) DataTable_GetInt(t, r, DataTable_GetColumnIndex(t, name))
    for (int r = 0; r < rows; r++) {
        if (INT("enabled") != 1) {
            continue;
        }
        Recipe *recipe = &recipes[recipe_count];
        memset(recipe, 0, sizeof(*recipe));
        snprintf(recipe->description, sizeof(recipe->description), "%s", STR("description"));
        bool ok = true;
        char column[24];
        for (int i = 1; i <= RECIPE_INPUTS && ok; i++) {
            snprintf(column, sizeof(column), "input %d", i);
            const char *text = STR(column);
            if (text[0] != '\0') {
                ok = Cube__ParseInput(text, &recipe->inputs[recipe->input_count++]);
            }
        }
        ok = ok && recipe->input_count > 0 && Cube__ParseOutput(STR("output"), recipe);
        if (!ok) {
            continue;
        }
        recipe->lvl  = INT("lvl");
        recipe->plvl = INT("plvl");
        recipe->ilvl = INT("ilvl");
        for (int m = 1; m <= RECIPE_MODS; m++) {
            snprintf(column, sizeof(column), "mod %d", m);
            const char *code = STR(column);
            if (code[0] == '\0') {
                continue;
            }
            RecipeMod *mod = &recipe->mods[recipe->mod_count++];
            snprintf(mod->code, sizeof(mod->code), "%s", code);
            snprintf(column, sizeof(column), "mod %d param", m);
            snprintf(mod->param, sizeof(mod->param), "%s", STR(column));
            snprintf(column, sizeof(column), "mod %d chance", m);
            mod->chance = DataTable_HasValue(t, r, DataTable_GetColumnIndex(t, column)) ? INT(column) : 100;
            snprintf(column, sizeof(column), "mod %d min", m);
            mod->min = INT(column);
            snprintf(column, sizeof(column), "mod %d max", m);
            mod->max = INT(column);
        }
        recipe_count++;
    }
#undef STR
#undef INT
    DataTable_Destroy(&t);
}

bool Cube_TakePortal(void) {
    const bool portal = cube_portal;
    cube_portal       = false;
    return portal;
}

void Cube_Release(void) {
    free(recipes);
    recipes      = NULL;
    recipe_count = 0;
    loaded       = false;
}

static bool Cube__Matches(const RecipeInput *input, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return false;
    }
    const bool family = (input->needs & NEED_ANY_TIER) && strcmp(base->tiers[0], input->code) == 0;
    if (strcmp(input->code, item->code) != 0 && strcmp(input->code, "any") != 0 && !Items_IsA(base, input->code) &&
        !family) {
        return false;
    }
    const int needs = input->needs;
    if (((needs & NEED_NORMAL) && item->quality != ITEM_NORMAL) ||
        ((needs & NEED_MAGIC) && item->quality != ITEM_MAGIC) || ((needs & NEED_RARE) && item->quality != ITEM_RARE) ||
        ((needs & NEED_UNIQUE) && item->quality != ITEM_UNIQUE) || ((needs & NEED_SET) && item->quality != ITEM_SET) ||
        ((needs & NEED_SOCKETS) && item->sockets == 0) || ((needs & NEED_NO_SOCK) && item->sockets != 0) ||
        ((needs & NEED_NORMAL_TIER) && base->tier != 0) || ((needs & NEED_EXCEPTIONAL) && base->tier != 1) ||
        ((needs & NEED_ELITE) && base->tier != 2) || ((needs & NEED_LOW) && !Item_IsLowQuality(item)) ||
        ((needs & NEED_SUPERIOR) && item->grade != ITEM_GRADE_SUPERIOR) ||
        ((needs & NEED_ETHEREAL) && !item->ethereal) || ((needs & NEED_NOT_ETH) && item->ethereal)) {
        return false;
    }
    return true;
}

// Each input's items among what's inside, all of them used: a small search. first: the cube index the first input took
static bool Cube__Assign(const Character *character, const Recipe *recipe, const int input, const int taken_of_input,
                         bool *used, int *first) {
    if (input == recipe->input_count) {
        for (int i = 0; i < character->cube_count; i++) {
            if (!used[i]) {
                return false; // something inside that the recipe doesn't take
            }
        }
        return true;
    }
    const RecipeInput *in = &recipe->inputs[input];
    if (taken_of_input == in->qty) {
        return Cube__Assign(character, recipe, input + 1, 0, used, first);
    }
    for (int i = 0; i < character->cube_count; i++) {
        if (used[i] || !Cube__Matches(in, &character->cube[i].item)) {
            continue;
        }
        used[i] = true;
        if (input == 0 && taken_of_input == 0) {
            *first = i;
        }
        if (Cube__Assign(character, recipe, input, taken_of_input + 1, used, first)) {
            return true;
        }
        used[i] = false;
    }
    return false;
}

// A random base of an item type for a recipe that names a type ("pole"): one that drops at that level
static const ItemBase *Cube__BaseOfType(const char *type, const int level) {
    const ItemBase *chosen = NULL;
    int             found  = 0;
    for (int i = 0; i < Items_Count(); i++) {
        const ItemBase *base = Items_Get(i);
        if (base->spawnable && base->level <= level && Items_IsA(base, type) && rand() % ++found == 0) {
            chosen = base;
        }
    }
    return chosen;
}

bool Cube_Transmute(Character *character, char *message, const size_t size) {
    Cube__Load();
    if (character->cube_count == 0) {
        snprintf(message, size, "Put items in the cube first.");
        return false;
    }
    for (int r = 0; r < recipe_count; r++) {
        const Recipe *recipe = &recipes[r];
        bool          used[CUBE_MAX];
        memset(used, 0, sizeof(used));
        int first = -1;
        if (!Cube__Assign(character, recipe, 0, 0, used, &first) || first < 0) {
            continue;
        }
        if (recipe->kind == OUT_COW_PORTAL) {
            character->cube_count = 0;
            cube_portal           = true;
            snprintf(message, size, "A red portal opens...");
            return true;
        }
        // What it makes, at about your level
        const Item input = character->cube[first].item;
        int level = recipe->lvl + (character->level * recipe->plvl / 100) + (character->level * recipe->ilvl / 100);
        level     = level > 0 ? level : character->level;
        Item made;
        memset(&made, 0, sizeof(made));
        if (recipe->kind == OUT_USEITEM) {
            made = input;
        } else {
            const ItemBase *base = recipe->kind == OUT_USETYPE ? Items_Find(input.code) : Items_Find(recipe->code);
            if (base == NULL && recipe->kind == OUT_NEW) {
                base = Cube__BaseOfType(recipe->code, level);
            }
            if (base == NULL) {
                continue; // something the engine doesn't have
            }
            made = Inventory_NewItem(base->code);
        }
        if (recipe->prefix >= 0 || recipe->suffix >= 0) {
            Affixes_SetMagic(&made, recipe->prefix, recipe->suffix);
        } else if (recipe->quality > ITEM_NORMAL) {
            Affixes_RollQuality(&made, level, (ItemQuality)recipe->quality);
        }
        for (int m = 0; m < recipe->mod_count; m++) {
            const RecipeMod *mod = &recipe->mods[m];
            if (rand() % 100 < mod->chance) {
                Affixes_AddPropertyParam(&made, mod->code, mod->param, mod->min, mod->max);
            }
        }
        if (recipe->upgrade > 0) {
            // The same item a tier up: its code, defense and durability from the better base, its magic kept
            const ItemBase *from = Items_Find(made.code);
            const ItemBase *to   = from != NULL && from->tiers[recipe->upgrade][0] != '\0'
                                       ? Items_Find(from->tiers[recipe->upgrade])
                                       : NULL;
            if (to == NULL) {
                continue; // nothing better to become
            }
            const Item fresh = Inventory_NewItem(to->code);
            snprintf(made.code, sizeof(made.code), "%s", to->code);
            made.defense        = fresh.defense;
            made.durability     = fresh.durability;
            made.max_durability = fresh.max_durability;
        }
        if (recipe->repair) {
            made.durability = made.max_durability;
        }
        if (recipe->unsocket) {
            made.socketed_count = 0; // (what was in the sockets is gone)
        }
        made.unidentified = false;
        // Everything inside was used: out it goes, and what was made goes in ("qty": how many in a stack)
        const ItemBase *made_base = Items_Find(made.code);
        if (made_base != NULL && made_base->stackable && recipe->qty > 1) {
            made.quantity =
                recipe->qty < made_base->max_stack || made_base->max_stack <= 0 ? recipe->qty : made_base->max_stack;
        }
        character->cube_count = 0;
        Cube_Add(character, &made);
        char name1[96];
        char name2[96];
        Affixes_Name(&made, name1, sizeof(name1), name2, sizeof(name2));
        snprintf(message, size, "%s", name1);
        LOG_INFO("Cube: %s -> %s", recipe->description, name1);
        return true;
    }
    snprintf(message, size, "Nothing happens: that isn't a recipe the cube knows.");
    return false;
}
