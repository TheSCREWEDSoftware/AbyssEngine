#include "Items.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_MISC  "/data/global/excel/misc.txt"
#define EXCEL_BELTS "/data/global/excel/belts.txt"

static const struct {
    const char *code; // bodylocs.txt
    const char *name;
} slot_info[EQUIP_COUNT] = {
    {"head", "Head"      },
    {"neck", "Neck"      },
    {"tors", "Torso"     },
    {"rarm", "Right Hand"},
    {"larm", "Left Hand" },
    {"rrin", "Right Ring"},
    {"lrin", "Left Ring" },
    {"belt", "Belt"      },
    {"feet", "Feet"      },
    {"glov", "Gloves"    },
};

static ItemBase *bases      = NULL;
static int       base_count = 0;
static bool      loaded     = false;

const char *Items_SlotName(const EquipSlot slot) { return slot < EQUIP_COUNT ? slot_info[slot].name : "?"; }

bool Items_IsPotion(const ItemBase *base) {
    return base != NULL && (base->life > 0 || base->mana > 0 || base->rejuvenate_pct > 0);
}

// Only what the game's fonts draw
static void Items__Sanitize(char *out, const size_t out_len, const char *in) {
    size_t o = 0;
    for (const unsigned char *ch = (const unsigned char *)in; *ch != '\0' && o + 1 < out_len; ch++) {
        if (*ch == 0xFF && ch[1] == 'c' && ch[2] != '\0') {
            ch += 2;
            continue;
        }
        if (*ch >= 0x20 && *ch < 0x7F) {
            out[o++] = (char)*ch;
        }
    }
    out[o] = '\0';
}

// itemtypes.txt: a type's row, -1 if none
static int Items__TypeRow(const DataTable *types, const int c_code, const char *code) {
    for (int r = 0; r < DataTable_GetRowCount(types) && code[0] != '\0'; r++) {
        if (strcmp(DataTable_GetString(types, r, c_code), code) == 0) {
            return r;
        }
    }
    return -1;
}

// Where a type is worn, and whether it goes in the belt: from the type, or what it counts as (Equiv1/2), up the chain
static void Items__TypeInfo(const DataTable *types, const char *code, ItemBase *base, const int depth) {
    if (depth > 8) {
        return;
    }
    const int c_code     = DataTable_GetColumnIndex(types, "Code");
    const int row        = Items__TypeRow(types, c_code, code);
    const int c_body     = DataTable_GetColumnIndex(types, "Body");
    const int c_loc[2]   = {DataTable_GetColumnIndex(types, "BodyLoc1"), DataTable_GetColumnIndex(types, "BodyLoc2")};
    const int c_equiv[2] = {DataTable_GetColumnIndex(types, "Equiv1"), DataTable_GetColumnIndex(types, "Equiv2")};
    const int c_belt     = DataTable_GetColumnIndex(types, "Beltable");
    if (row < 0) {
        return;
    }
    for (int k = 0; k < base->kind_count; k++) {
        if (strcmp(base->kinds[k], code) == 0) {
            return; // (already counted, through another way up)
        }
    }
    if (base->kind_count < 12) {
        snprintf(base->kinds[base->kind_count++], sizeof(base->kinds[0]), "%s", code);
    }
    base->beltable = base->beltable || DataTable_GetInt(types, row, c_belt) == 1;
    if (DataTable_GetInt(types, row, c_body) == 1) {
        for (int l = 0; l < 2; l++) {
            const char *loc = DataTable_GetString(types, row, c_loc[l]);
            for (int s = 0; s < EQUIP_COUNT; s++) {
                base->slots[s] = base->slots[s] || strcmp(loc, slot_info[s].code) == 0;
            }
        }
    }
    for (int e = 0; e < 2; e++) {
        Items__TypeInfo(types, DataTable_GetString(types, row, c_equiv[e]), base, depth + 1);
    }
}

static void Items__LoadTable(const char *path, const ItemKind kind, const DataTable *types, const DataTable *belts) {
    DataTable *t = DataTable_Load(path);
    if (t == NULL) {
        return;
    }
#define COL(n) DataTable_GetColumnIndex(t, n)
    const int c_code = COL("code"), c_name = COL("name"), c_namestr = COL("namestr"), c_type = COL("type"),
              c_w = COL("invwidth"), c_h = COL("invheight"), c_inv = COL("invfile"), c_uinv = COL("uniqueinvfile"),
              c_min = COL("mindam"), c_max = COL("maxdam"), c_2min = COL("2handmindam"), c_2max = COL("2handmaxdam"),
              c_2h = COL("2handed"), c_strb = COL("StrBonus"), c_dexb = COL("DexBonus"), c_minac = COL("minac"),
              c_maxac = COL("maxac"), c_block = COL("block"), c_str = COL("reqstr"), c_dex = COL("reqdex"),
              c_lvl = COL("levelreq"), c_belt = COL("belt"), c_stack = COL("stackable"), c_maxstack = COL("maxstack"),
              c_stat1 = COL("stat1"), c_sound = COL("usesound"), c_calc1 = COL("calc1"), c_stat2 = COL("stat2"),
              c_len = COL("len"), c_flippy = COL("flippyfile"), c_drop = COL("dropsound"),
              c_drop_frame = COL("dropsfxframe"), c_level = COL("level"), c_spawnable = COL("spawnable"),
              c_gfx = COL("alternategfx"), c_wclass = COL("wclass"), c_wclass2 = COL("2handedwclass"),
              c_1or2 = COL("1or2handed"), c_rarm = COL("rArm"), c_larm = COL("lArm"), c_torso = COL("Torso"),
              c_legs = COL("Legs"), c_rpad = COL("rSPad"), c_lpad = COL("lSPad"), c_sockets = COL("gemsockets"),
              c_cost = COL("cost"), c_dur = COL("durability"), c_nodur = COL("nodurability"),
              c_gamble = COL("gamble cost"), c_type2 = COL("type2");
#undef COL
    const int rows  = DataTable_GetRowCount(t);
    ItemBase *grown = realloc(bases, sizeof(ItemBase) * (size_t)(base_count + rows));
    FAIL_IF_NULL(grown);
    bases = grown;
    for (int row = 0; row < rows; row++) {
        const char *code = DataTable_GetString(t, row, c_code);
        if (code[0] == '\0') {
            continue; // "Expansion" separators
        }
        ItemBase *base = &bases[base_count++];
        memset(base, 0, sizeof(ItemBase));
        base->kind = kind;
        snprintf(base->code, sizeof(base->code), "%s", code);
        snprintf(base->type, sizeof(base->type), "%s", DataTable_GetString(t, row, c_type));
        const char *name = c_namestr >= 0 ? StringTable_Find(DataTable_GetString(t, row, c_namestr)) : NULL;
        name             = name != NULL ? name : StringTable_Find(code);
        Items__Sanitize(base->name, sizeof(base->name), name != NULL ? name : DataTable_GetString(t, row, c_name));
        snprintf(base->inv_file, sizeof(base->inv_file), "%s", DataTable_GetString(t, row, c_inv));
        snprintf(base->unique_inv_file, sizeof(base->unique_inv_file), "%s", DataTable_GetString(t, row, c_uinv));
        snprintf(base->use_sound, sizeof(base->use_sound), "%s", DataTable_GetString(t, row, c_sound));
        snprintf(base->flippy_file, sizeof(base->flippy_file), "%s", DataTable_GetString(t, row, c_flippy));
        snprintf(base->drop_sound, sizeof(base->drop_sound), "%s", DataTable_GetString(t, row, c_drop));
        base->drop_sound_frame   = DataTable_GetInt(t, row, c_drop_frame);
        base->level              = DataTable_GetInt(t, row, c_level);
        base->spawnable          = DataTable_GetInt(t, row, c_spawnable) == 1;
        base->width              = DataTable_GetInt(t, row, c_w) > 0 ? DataTable_GetInt(t, row, c_w) : 1;
        base->height             = DataTable_GetInt(t, row, c_h) > 0 ? DataTable_GetInt(t, row, c_h) : 1;
        base->min_damage         = DataTable_GetInt(t, row, c_min);
        base->max_damage         = DataTable_GetInt(t, row, c_max);
        base->two_hand_min       = DataTable_GetInt(t, row, c_2min);
        base->two_hand_max       = DataTable_GetInt(t, row, c_2max);
        base->two_handed         = DataTable_GetInt(t, row, c_2h) == 1;
        base->str_bonus          = DataTable_GetInt(t, row, c_strb);
        base->dex_bonus          = DataTable_GetInt(t, row, c_dexb);
        base->min_defense        = DataTable_GetInt(t, row, c_minac);
        base->max_defense        = DataTable_GetInt(t, row, c_maxac);
        base->block              = DataTable_GetInt(t, row, c_block);
        base->required_strength  = DataTable_GetInt(t, row, c_str);
        base->required_dexterity = DataTable_GetInt(t, row, c_dex);
        base->required_level     = DataTable_GetInt(t, row, c_lvl);
        base->stackable          = DataTable_GetInt(t, row, c_stack) == 1;
        base->max_stack          = DataTable_GetInt(t, row, c_maxstack);
        snprintf(base->gfx, sizeof(base->gfx), "%s", c_gfx >= 0 ? DataTable_GetString(t, row, c_gfx) : code);
        if (base->gfx[0] == '\0') {
            snprintf(base->gfx, sizeof(base->gfx), "%s", code);
        }
        snprintf(base->wclass, sizeof(base->wclass), "%s", c_wclass >= 0 ? DataTable_GetString(t, row, c_wclass) : "");
        snprintf(base->wclass_2h, sizeof(base->wclass_2h), "%s",
                 c_wclass2 >= 0 ? DataTable_GetString(t, row, c_wclass2) : "");
        base->one_or_two = c_1or2 >= 0 && DataTable_GetInt(t, row, c_1or2) == 1;
        // Armor's body parts: blank for helms, shields and the like (they don't change the body)
#define LOOK(c) ((c) >= 0 && DataTable_GetString(t, row, c)[0] != '\0' ? DataTable_GetInt(t, row, c) : -1)
        base->look_right_arm = LOOK(c_rarm);
        base->look_left_arm  = LOOK(c_larm);
        base->look_torso     = LOOK(c_torso);
        base->look_legs      = LOOK(c_legs);
        base->look_right_pad = LOOK(c_rpad);
        base->look_left_pad  = LOOK(c_lpad);
#undef LOOK
        base->max_sockets = c_sockets >= 0 ? DataTable_GetInt(t, row, c_sockets) : 0;
        base->cost        = c_cost >= 0 ? DataTable_GetInt(t, row, c_cost) : 0;
        base->gamble_cost = c_gamble >= 0 ? DataTable_GetInt(t, row, c_gamble) : 0;
        // Durability: weapons and armor wear down (not stacks like javelins, nor what the table says lasts)
        base->durability = kind != ITEM_KIND_MISC && !base->stackable && c_dur >= 0 &&
                                   !(c_nodur >= 0 && DataTable_GetInt(t, row, c_nodur) == 1)
                               ? DataTable_GetInt(t, row, c_dur)
                               : 0;
        Items__TypeInfo(types, base->type, base, 0);
        if (c_type2 >= 0 && DataTable_GetString(t, row, c_type2)[0] != '\0') {
            Items__TypeInfo(types, DataTable_GetString(t, row, c_type2), base, 0); // (a gem's grade: "gem4")
        }
        if (base->slots[EQUIP_BELT] && belts != NULL && c_belt >= 0) {
            // armor.txt's "belt" is a row of belts.txt, which says how many potions it holds
            const int belt_row = DataTable_GetInt(t, row, c_belt);
            const int boxes    = DataTable_GetInt(belts, belt_row, DataTable_GetColumnIndex(belts, "numboxes"));
            base->belt_rows    = boxes > 0 ? boxes / 4 : 1;
        }
        if (kind == ITEM_KIND_MISC && c_stat1 >= 0) {
            // Potions: healing over time (stat "hpregen" / "manarecovery" with the amount, "len" in frames), or
            // rejuvenation (a percentage of life and mana at once)
            const char *stat1  = DataTable_GetString(t, row, c_stat1);
            const char *stat2  = DataTable_GetString(t, row, c_stat2);
            const int   amount = DataTable_GetInt(t, row, c_calc1);
            const int   frames = DataTable_GetInt(t, row, c_len);
            if (strcmp(stat1, "hpregen") == 0) {
                base->life    = amount;
                base->seconds = (frames + 24) / 25;
            } else if (strcmp(stat1, "manarecovery") == 0) {
                base->mana    = amount;
                base->seconds = (frames + 24) / 25;
            } else if (strcmp(stat1, "hitpoints") == 0 && strcmp(stat2, "mana") == 0) {
                base->rejuvenate_pct = amount;
            }
        }
    }
    DataTable_Destroy(&t);
}

static void Items__Load(void) {
    if (loaded) {
        return;
    }
    loaded         = true;
    DataTable *ty  = DataTable_Load(EXCEL_ITEMTYPES);
    DataTable *blt = DataTable_Load(EXCEL_BELTS);
    if (ty != NULL) {
        Items__LoadTable(EXCEL_WEAPONS, ITEM_KIND_WEAPON, ty, blt);
        Items__LoadTable(EXCEL_ARMOR, ITEM_KIND_ARMOR, ty, blt);
        Items__LoadTable(EXCEL_MISC, ITEM_KIND_MISC, ty, blt);
        DataTable_Destroy(&ty);
    }
    if (blt != NULL) {
        DataTable_Destroy(&blt);
    }
    LOG_INFO("Items: %d base items", base_count);
}

const ItemBase *Items_Find(const char *code) {
    Items__Load();
    for (int i = 0; i < base_count; i++) {
        if (strcmp(bases[i].code, code) == 0) {
            return &bases[i];
        }
    }
    return NULL;
}

int Items_Count(void) {
    Items__Load();
    return base_count;
}

const ItemBase *Items_Get(const int index) {
    Items__Load();
    return index >= 0 && index < base_count ? &bases[index] : NULL;
}

bool Items_IsA(const ItemBase *base, const char *type) {
    for (int k = 0; base != NULL && k < base->kind_count; k++) {
        if (strcmp(base->kinds[k], type) == 0) {
            return true;
        }
    }
    return false;
}

void Items_Release(void) {
    free(bases);
    bases      = NULL;
    base_count = 0;
    loaded     = false;
}
