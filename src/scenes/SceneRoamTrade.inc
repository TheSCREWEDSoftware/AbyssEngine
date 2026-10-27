// SceneRoam: trading with another player (included by SceneRoam.c). The party screen (P) asks them; when they say yes
// both get the game's trade screen: their offer above, yours below, the gold each puts in. Changing your offer takes
// back both acceptances; when both have accepted the offers as they stand, each game makes the swap.

enum { TRADE_STATE_NONE, TRADE_STATE_ASKING, TRADE_STATE_OPEN };

static void Roam__CloseShop(SceneRoam *roam);

static void Roam__TradeSend(SceneRoam *roam, const int to, const TradeOp op, NetWriter *body) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_U8(writer, (uint8_t)to);
    NetWriter_U8(writer, (uint8_t)op);
    if (body != NULL) {
        NetWriter_Bytes(writer, body->data, body->size);
    }
    NetClient_Send(MSG_TRADE, writer);
    free(writer);
    (void)roam;
}

static void Roam__TradeSendOffer(SceneRoam *roam) {
    NetWriter *body = malloc(sizeof(NetWriter));
    if (body == NULL) {
        return;
    }
    NetWriter_Begin(body);
    NetWriter_U32(body, roam->trade_my_version);
    NetWriter_I32(body, roam->trade->my_gold);
    NetWriter_U8(body, (uint8_t)roam->trade->mine_count);
    for (int i = 0; i < roam->trade->mine_count; i++) {
        NetWriter_U8(body, (uint8_t)roam->trade->mine[i].x);
        NetWriter_U8(body, (uint8_t)roam->trade->mine[i].y);
        NetItem_Write(body, &roam->trade->mine[i].item);
    }
    Roam__TradeSend(roam, roam->trade_with, TRADE_OFFER, body);
    free(body);
}

static void Roam__TradeOpen(SceneRoam *roam, const int with) {
    if (roam->trade == NULL) {
        roam->trade = calloc(1, sizeof(TradeView));
        FAIL_IF_NULL(roam->trade);
    }
    memset(roam->trade, 0, sizeof(TradeView));
    const NetPlayer *player = NetClient_Player(with);
    snprintf(roam->trade->their_name, sizeof(roam->trade->their_name), "%s", player != NULL ? player->name : "?");
    roam->trade_with          = with;
    roam->trade_state         = TRADE_STATE_OPEN;
    roam->trade_my_version    = 1;
    roam->trade_their_version = 0;
    roam->trade_asked_by      = 0;
    Roam__CloseShop(roam);
    GameInventory_SetStash(roam->inventory_ui, false);
    GameInventory_SetTrade(roam->inventory_ui, roam->trade);
    roam->inventory_panel = true;
    roam->skill_panel     = false;
    roam->char_panel      = false;
    roam->panel           = PANEL_NONE;
    roam->party_panel     = false;
}

// Closing: what you offered goes back in your inventory (or at your feet)
static void Roam__TradeClose(SceneRoam *roam, const bool tell) {
    if (roam->trade_state == TRADE_STATE_OPEN && roam->trade != NULL) {
        GameInventory_PutBack(roam->inventory_ui, roam->character); // (what's on the mouse first)
        for (int i = 0; i < roam->trade->mine_count; i++) {
            if (!Inventory_Add(roam->character, &roam->trade->mine[i].item)) {
                Roam__PutOnGround(roam, &roam->trade->mine[i].item, roam->player.x, roam->player.y);
            }
        }
        roam->trade->mine_count = 0;
    }
    if (tell && roam->trade_with > 0 && roam->trade_state != TRADE_STATE_NONE) {
        Roam__TradeSend(roam, roam->trade_with, TRADE_CANCEL, NULL);
    }
    GameInventory_SetTrade(roam->inventory_ui, NULL);
    roam->trade_state = TRADE_STATE_NONE;
    roam->trade_with  = 0;
}

// Whether everything they offer fits in your inventory
static bool Roam__TradeFits(SceneRoam *roam) {
    Character *copy = malloc(sizeof(Character));
    if (copy == NULL) {
        return false;
    }
    *copy     = *roam->character;
    bool fits = true;
    for (int i = 0; i < roam->trade->their_count && fits; i++) {
        fits = Inventory_Add(copy, &roam->trade->theirs[i].item);
    }
    free(copy);
    return fits;
}

// Both have accepted the offers as they stand: the swap
static void Roam__TradeFinish(SceneRoam *roam) {
    Character *c = roam->character;
    TradeView *t = roam->trade;
    c->gold      = c->gold - t->my_gold + t->their_gold;
    c->gold      = c->gold > Roam__GoldLimit(roam) ? Roam__GoldLimit(roam) : (c->gold < 0 ? 0 : c->gold);
    for (int i = 0; i < t->their_count; i++) {
        if (!Inventory_Add(c, &t->theirs[i].item)) {
            Roam__PutOnGround(roam, &t->theirs[i].item, roam->player.x, roam->player.y);
        }
    }
    t->mine_count = 0; // (yours went to them)
    char text[96];
    snprintf(text, sizeof(text), "You traded with %s.", t->their_name);
    Roam__ShowMessage(roam, text);
    Sounds_Play("item_gold", SOUND_KIND_SFX);
    Roam__TradeClose(roam, false);
    Roam__StoreCharacter(roam);
}

static void Roam__TradeCheckDone(SceneRoam *roam) {
    if (roam->trade_state == TRADE_STATE_OPEN && roam->trade->my_accept && roam->trade->their_accept) {
        Roam__TradeFinish(roam);
    }
}

// Your offer changed: both acceptances go, the other side is told
static void Roam__TradeChanged(SceneRoam *roam) {
    if (roam->trade_state != TRADE_STATE_OPEN) {
        return;
    }
    roam->trade_my_version++;
    roam->trade->my_accept    = false;
    roam->trade->their_accept = false;
    Roam__TradeSendOffer(roam);
}

static void Roam__TradeAccept(SceneRoam *roam) {
    if (roam->trade_state != TRADE_STATE_OPEN || roam->trade->my_accept) {
        return;
    }
    if (!Roam__TradeFits(roam)) {
        Roam__ShowMessage(roam, "You don't have room for what they offer.");
        return;
    }
    roam->trade->my_accept = true;
    NetWriter *body        = malloc(sizeof(NetWriter));
    if (body == NULL) {
        return;
    }
    NetWriter_Begin(body);
    NetWriter_U32(body, roam->trade_their_version);
    NetWriter_U32(body, roam->trade_my_version);
    Roam__TradeSend(roam, roam->trade_with, TRADE_ACCEPT, body);
    free(body);
    Roam__TradeCheckDone(roam);
}

// The party screen's Trade button: ask them, or say yes when they asked first
static void Roam__TradeButton(SceneRoam *roam, const NetPlayer *player) {
    if (roam->trade_state == TRADE_STATE_OPEN) {
        Roam__ShowMessage(roam, "You're trading already.");
        return;
    }
    if (roam->trade_asked_by == player->id) {
        Roam__TradeSend(roam, player->id, TRADE_YES, NULL);
        Roam__TradeOpen(roam, player->id);
        return;
    }
    roam->trade_state = TRADE_STATE_ASKING;
    roam->trade_with  = player->id;
    Roam__TradeSend(roam, player->id, TRADE_ASK, NULL);
    char text[96];
    snprintf(text, sizeof(text), "You asked %s to trade.", player->name);
    Roam__ShowMessage(roam, text);
}

// MSG_TRADE_FROM
static void Roam__TradeMessage(SceneRoam *roam, NetReader *reader) {
    const int        from   = NetReader_U8(reader);
    const int        to     = NetReader_U8(reader);
    const int        op     = NetReader_U8(reader);
    const NetPlayer *player = NetClient_Player(from);
    char             text[96];
    if (to != NetClient_MyId() || player == NULL || reader->failed) {
        return;
    }
    switch (op) {
    case TRADE_ASK:
        if (roam->trade_state == TRADE_STATE_OPEN || roam->character->dead) {
            Roam__TradeSend(roam, from, TRADE_NO, NULL);
            break;
        }
        roam->trade_asked_by = from;
        snprintf(text, sizeof(text), "%s wants to trade with you (P to answer).", player->name);
        Roam__ShowMessage(roam, text);
        break;
    case TRADE_YES:
        if (roam->trade_state == TRADE_STATE_ASKING && roam->trade_with == from) {
            Roam__TradeOpen(roam, from);
        }
        break;
    case TRADE_NO:
        if (roam->trade_with == from && roam->trade_state == TRADE_STATE_ASKING) {
            roam->trade_state = TRADE_STATE_NONE;
            roam->trade_with  = 0;
            snprintf(text, sizeof(text), "%s can't trade now.", player->name);
            Roam__ShowMessage(roam, text);
        }
        break;
    case TRADE_OFFER: {
        if (roam->trade_state != TRADE_STATE_OPEN || roam->trade_with != from) {
            break;
        }
        TradeView *t              = roam->trade;
        roam->trade_their_version = NetReader_U32(reader);
        t->their_gold             = NetReader_I32(reader);
        t->their_count            = 0;
        const int count           = NetReader_U8(reader);
        for (int i = 0; i < count && !reader->failed && i < TRADE_ITEMS; i++) {
            const int x = NetReader_U8(reader);
            const int y = NetReader_U8(reader);
            Item      item;
            NetItem_Read(reader, &item);
            GameInventory_TradePlace(t->theirs, &t->their_count, &item, x, y);
        }
        t->my_accept    = false; // what you accepted isn't what's there now
        t->their_accept = false;
        break;
    }
    case TRADE_ACCEPT: {
        if (roam->trade_state != TRADE_STATE_OPEN || roam->trade_with != from) {
            break;
        }
        const uint32_t mine       = NetReader_U32(reader); // (they accept my offer at this version ...)
        const uint32_t theirs     = NetReader_U32(reader); // (... with theirs at this one)
        roam->trade->their_accept = mine == roam->trade_my_version && theirs == roam->trade_their_version;
        Roam__TradeCheckDone(roam);
        break;
    }
    case TRADE_CANCEL:
        if (roam->trade_with == from) {
            snprintf(text, sizeof(text), "%s closed the trade.", player->name);
            Roam__TradeClose(roam, false);
            Roam__ShowMessage(roam, text);
        }
        break;
    default:
        break;
    }
}

// Every frame while trading: they left, or the trade screen was closed with the inventory
static void Roam__KeepTrade(SceneRoam *roam) {
    if (roam->trade_state == TRADE_STATE_NONE) {
        return;
    }
    if (NetClient_Player(roam->trade_with) == NULL) {
        Roam__TradeClose(roam, false); // they've gone
        return;
    }
    if (roam->trade_state == TRADE_STATE_OPEN && !roam->inventory_panel) {
        Roam__TradeClose(roam, true);
    }
}
