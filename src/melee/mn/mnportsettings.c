/// Retail-style "Port Settings" screen (native port only), reached from
/// Options > fourth slot. Rows are plain SIS text drawn where the Options
/// bars were, so no disc asset is modified; the table below is the whole
/// screen definition and a new option is one more entry.
#include "mnportsettings.h"

#ifdef MELEE_NATIVE

#include "inlines.h"
#include "mnmain.h"
#include "types.h"
#include <dolphin/os.h>
#include <stdio.h>
#include <string.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/it/forward.h>
#include <melee/lb/lbcardgame.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/sislib.h>
#include "melee_settings.h"

#define PORT_MENU_KIND MENU_KIND_22

Vec3 mn_NativeSettingsBars[6];
u8 mn_NativeSettingsBarsValid;
const char mn_NativePortDescription[] = "Settings for this port.";

/// Give printf'd text the look of the retail description strings, whose SIS
/// control codes select kerning, a light grey colour and a 0.70 glyph scale
/// (dumped from SdMenu.usd string 0x9A). The glyph scale is re-derived from
/// font_size when the text is laid out, so the 0.70 goes into font_size
/// (0.0521 * 0.7). They also centre and fit, but the SIS measurement of
/// printf'd text comes out far too wide and pushes it off the left edge, so
/// the text is centred here from its length instead: about 12.2 px per
/// kerned glyph at that size, 20 px per world unit.
void mnPort_StyleBarText(HSD_Text* text, const char* string)
{
    static const GXColor grey = { 0xAA, 0xAA, 0xAA, 0xFF };
    f32 width_px = 12.2f * (f32) strlen(string);
    f32 margin_px = (364.68332f - width_px) * 0.5f;
    text->font_size.x = 0.0521f * 0.7f;
    text->font_size.y = 0.0521f * 0.7f;
    text->x78.x = 0.0f;
    text->x78.y = 0.0f;
    text->default_alignment = 0;
    text->alignment = 0;
    text->default_fitting = 0;
    text->fitting = 0;
    text->default_kerning = 1;
    text->kerning = 1;
    text->text_color = grey;
    text->active_color = grey;
    text->pos_x = -9.5f + (margin_px > 0.0f ? margin_px / 20.0f : 0.0f);
}

enum PortRowKind {
    PortRow_Toggle,   ///< left/right/A flip an int between two choices
    PortRow_Action,   ///< A runs a function
    PortRow_Disabled, ///< shown greyed with a fixed value, no input
    PortRow_Number,   ///< left/right step an int within [min, max]
};

/// Actions are an enum rather than function pointers so the online page can
/// build its rows at run time (one per host found on the network).
enum PortAction {
    Act_None,
    Act_UnlockAll,
    Act_OpenOnline,
    Act_Host,
    Act_Join0,
    Act_Join1,
    Act_Join2,
    Act_OnlineBack,
};

struct PortRow {
    const char* label;
    enum PortRowKind kind;
    int* value;
    const char* const* choices; ///< two entries for toggles
    enum PortAction action;
    const char* fixed_value; ///< PortRow_Disabled, or a live value for actions
    int min, max;            ///< PortRow_Number
};

static const char* const onoff[2] = { "Off", "On" };

/// Same writes as the development build's new-save path
/// (gmMainLib_8015FA34): every character and stage bit, the feature bitmask,
/// and all 66 special-message bonuses marked achieved and already shown,
/// so the next menu visit does not queue dozens of "special message"
/// pop-ups for the unlocks. Pending pop-ups from earlier are cleared too.
static void unlock_all(void)
{
    int i;
    gmMainLib_GetCardData()->save_data.x186C = 0xFF;
    gm_80164F18();
    gm_8016468C();
    gm_8017297C();
    for (i = 0; i < 0x42; i++) {
        gmMainLib_8015D8B0(i);
    }
    lbCardGame_RequestSave();
    OSReport("[port-settings] all characters and stages unlocked\n");
}

/// The SIS font renders space, digits, A-Z, a-z, comma, period, hyphen,
/// colon and quotes; keep labels inside that set.
static const struct PortRow rows[] = {
    { "Debug Menu, Y on title", PortRow_Toggle,
      &MeleeNativeSettingsData.debug_menu, onoff, Act_None, NULL, 0, 0 },
    { "Debug Overlays", PortRow_Toggle, &MeleeNativeSettingsData.debug_overlays,
      onoff, Act_None, NULL, 0, 0 },
    { "Unlock All Characters and Stages", PortRow_Action, NULL, NULL,
      Act_UnlockAll, NULL, 0, 0 },
    { "Online Play", PortRow_Action, NULL, NULL, Act_OpenOnline, NULL, 0, 0 },
};
#define ROW_COUNT ((int) ARRAY_SIZE(rows))
#define MAX_ROWS 5 ///< bars 1..5 below the title

/// Online Play page: host, one row per host announced on the LAN (the two
/// most recent), the input delay this device proposes when hosting, back.
/// Text lives in these buffers because the SIS text keeps the pointer.
static char online_join_labels[3][40];
static char online_delay_value[4];
static char online_host_value[20];
static struct PortRow online_rows[MAX_ROWS];
static int online_row_count;

int MeleeNativeNetplayHosting(void);
int MeleeNativeNetplayHostCount(void);
const char* MeleeNativeNetplayHostLabel(int index);
const char* MeleeNativeNetplayMenuStatus(void);
const char* MeleeNativeNetplayLocalAddress(void);
void MeleeNativeNetplayMenuEnter(void);
void MeleeNativeNetplayMenuLeave(void);
void MeleeNativeNetplayHost(void);
void MeleeNativeNetplayJoin(int index);
void MeleeNativeNetplayMenuTick(void);

static void build_online_rows(void)
{
    int hosts = MeleeNativeNetplayHostCount();
    int i, n = 0;
    if (hosts > 2) {
        hosts = 2;
    }
    if (MeleeNativeNetplayHosting()) {
        strncpy(online_host_value, MeleeNativeNetplayLocalAddress(),
                sizeof(online_host_value) - 1);
    } else {
        online_host_value[0] = '\0';
    }
    online_rows[n++] = (struct PortRow){ "Host a match", PortRow_Action, NULL, NULL,
                                         Act_Host, online_host_value, 0, 0 };
    for (i = 0; i < hosts; i++) {
        snprintf(online_join_labels[i], sizeof(online_join_labels[i]), "Join %s",
                 MeleeNativeNetplayHostLabel(i));
        online_rows[n++] = (struct PortRow){ online_join_labels[i], PortRow_Action, NULL,
                                             NULL, (enum PortAction) (Act_Join0 + i), NULL,
                                             0, 0 };
    }
    snprintf(online_delay_value, sizeof(online_delay_value), "%d",
             MeleeNativeSettingsData.online_input_delay);
    online_rows[n++] = (struct PortRow){ "Input delay, frames", PortRow_Number,
                                         &MeleeNativeSettingsData.online_input_delay, NULL,
                                         Act_None, online_delay_value, 1, 15 };
    online_rows[n++] = (struct PortRow){ "Back", PortRow_Action, NULL, NULL, Act_OnlineBack,
                                         NULL, 0, 0 };
    online_row_count = n;
}

typedef struct PortMenuData {
    u8 cursor;
    u8 flash; ///< frames left of the "done" colour on an action row
    u8 page;  ///< 0 = settings, 1 = Online Play
    u8 refresh; ///< frames until the online page redraws its live text
    HSD_Text* title;
    HSD_Text* labels[MAX_ROWS];
    HSD_Text* values[MAX_ROWS];
    HSD_Text* hint;
} PortMenuData;

static const struct PortRow* current_rows(const PortMenuData* data, int* count)
{
    if (data->page == 1) {
        *count = online_row_count;
        return online_rows;
    }
    *count = ROW_COUNT;
    return rows;
}

static HSD_GObj* port_gobj;
static u8 port_ready;

// Layout in the menu camera's world units (about 20 px per unit on a 640-wide
// frame). SIS text y grows downwards while the jobj world y grows upwards, so
// a bar's text position is (bar.x - half bar width, -bar.y). The bar origin is
// its centre; the bars are about 13.5 units wide.
static const Vec3 kLabelOffset = { -6.3f, -0.55f, 0.0f };
static const float kValueX = 17.5f;
// The Options bars are staggered horizontally; every row uses this bar's x.
#define REFERENCE_BAR 3
static const float kRowFont = 0.036f;
static const float kTitleFont = 0.05f;
static const GXColor kTitleColor = { 0xFF, 0xE0, 0x60, 0xFF };
static const GXColor kRowColor = { 0xFF, 0xFF, 0xFF, 0xFF };
static const GXColor kCursorColor = { 0xFF, 0xD0, 0x20, 0xFF };
static const GXColor kDisabledColor = { 0xA0, 0xA0, 0xA0, 0xFF };
static const GXColor kDoneColor = { 0x60, 0xFF, 0x80, 0xFF };

static HSD_Text* make_text(f32 x, f32 y, f32 z, f32 size, GXColor color,
                           const char* string)
{
    HSD_Text* text = HSD_SisLib_803A6754(0, 1);
    text->pos_x = x;
    text->pos_y = y;
    text->pos_z = z;
    text->font_size.x = size;
    text->font_size.y = size;
    text->text_color = color;
    text->active_color = color;
    // Proportional glyph advance; the disc's SIS strings switch this on with
    // an embedded code that printf-style text lacks.
    text->default_kerning = 1;
    text->kerning = 1;
    HSD_SisLib_803A6B98(text, 0.0f, 0.0f, "%s", string);
    return text;
}

static void free_text(HSD_Text** text)
{
    if (*text != NULL) {
        HSD_SisLib_803A5CC4(*text);
        *text = NULL;
    }
}

static void free_all(PortMenuData* data)
{
    int i;
    free_text(&data->title);
    free_text(&data->hint);
    for (i = 0; i < MAX_ROWS; i++) {
        free_text(&data->labels[i]);
        free_text(&data->values[i]);
    }
}

static Vec3 bar_position(int index)
{
    Vec3 pos;
    if (mn_NativeSettingsBarsValid) {
        // Even spacing from the first to the last bar; the bars themselves
        // are unevenly spaced and staggered.
        const Vec3* bars = mn_NativeSettingsBars;
        pos.x = bars[REFERENCE_BAR].x;
        pos.y = bars[0].y + (bars[5].y - bars[0].y) * (f32) index / 5.0f;
        pos.z = bars[REFERENCE_BAR].z;
    } else {
        // Fallback when the list was never idle (should not happen): stack
        // rows below the description text.
        pos.x = -3.0f;
        pos.y = 5.5f - 2.1f * (f32) index;
        pos.z = 17.0f;
    }
    pos.x += kLabelOffset.x;
    pos.y = -pos.y + kLabelOffset.y;
    pos.z += kLabelOffset.z;
    return pos;
}

static void rebuild(PortMenuData* data)
{
    int i, count;
    const struct PortRow* table;
    Vec3 pos;
    free_all(data);
    if (data->page == 1) {
        build_online_rows();
    }
    table = current_rows(data, &count);
    if (data->cursor >= count) {
        data->cursor = (u8) (count - 1);
    }
    pos = bar_position(0);
    {
        // Title centred on the Options panel (its centre is about 5 px right
        // of the screen centre); ~360 * font px per kerned glyph.
        const char* title = data->page == 1 ? "Online Play" : "Port Settings";
        f32 width_units = (f32) strlen(title) * kTitleFont * 360.0f / 20.0f;
        data->title = make_text(0.25f - width_units * 0.5f, pos.y, pos.z,
                                kTitleFont, kTitleColor, title);
    }
    for (i = 0; i < count; i++) {
        const struct PortRow* row = &table[i];
        GXColor color = kRowColor;
        const char* value = "";
        if (row->kind == PortRow_Disabled) {
            color = kDisabledColor;
            value = row->fixed_value;
        } else if (row->kind == PortRow_Toggle) {
            value = row->choices[*row->value != 0];
        } else if (row->kind == PortRow_Number || row->fixed_value != NULL) {
            value = row->fixed_value;
        }
        if (i == data->cursor) {
            color = data->flash ? kDoneColor : kCursorColor;
        }
        pos = bar_position(i + 1);
        data->labels[i] =
            make_text(pos.x, pos.y, pos.z, kRowFont, color, row->label);
        if (value[0] != '\0') {
            data->values[i] = make_text(pos.x + kValueX, pos.y, pos.z,
                                        kRowFont, color, value);
        }
    }
    // Same box as the Options description bar (mn_80229A7C).
    {
        // Same box and style as the Options description bar. The online page
        // shows the lobby status there instead.
        const char* hint_text = data->page == 1 ? MeleeNativeNetplayMenuStatus()
                                                : "Left, right change. B saves.";
        HSD_Text* hint = HSD_SisLib_803A6754(0, 1);
        hint->pos_y = 9.1f;
        hint->pos_z = 17.0f;
        hint->box_size_x = 364.68332f;
        hint->box_size_y = 38.38772f;
        hint->font_size.x = 0.0521f;
        hint->font_size.y = 0.0521f;
        mnPort_StyleBarText(hint, hint_text);
        HSD_SisLib_803A6B98(hint, 0.0f, 0.0f, "%s", hint_text);
        data->hint = hint;
    }
}

static void move_cursor(PortMenuData* data, int delta)
{
    int count;
    current_rows(data, &count);
    data->cursor = (u8) ((data->cursor + count + delta) % count);
    data->flash = 0;
    sfxMove();
    rebuild(data);
}

static void open_online(PortMenuData* data)
{
    data->page = 1;
    data->cursor = 0;
    data->refresh = 0;
    MeleeNativeNetplayMenuEnter();
}

static void close_online(PortMenuData* data)
{
    MeleeNativeNetplayMenuLeave();
    MeleeNativeSettingsSave();
    data->page = 0;
    data->cursor = ROW_COUNT - 1;
}

static void run_action(PortMenuData* data, enum PortAction action)
{
    switch (action) {
    case Act_UnlockAll:
        unlock_all();
        break;
    case Act_OpenOnline:
        open_online(data);
        break;
    case Act_Host:
        MeleeNativeNetplayHost();
        break;
    case Act_Join0:
    case Act_Join1:
    case Act_Join2:
        MeleeNativeNetplayJoin(action - Act_Join0);
        break;
    case Act_OnlineBack:
        close_online(data);
        break;
    case Act_None:
        break;
    }
}

/// Input handling on its own GObj, as the other settings screens do.
static void think(HSD_GObj* gobj)
{
    u64 events;
    PortMenuData* data;
    const struct PortRow* row;
    int count;
    if (mn_804D6BC8.cooldown != 0) {
        Menu_DecrementAnimTimer();
        return;
    }
    if (port_gobj == NULL || !port_ready) {
        return;
    }
    data = port_gobj->user_data;
    if (data->page == 1) {
        // A finished handshake relaunches the game from inside this call.
        MeleeNativeNetplayMenuTick();
        if (++data->refresh >= 20) {
            data->refresh = 0;
            rebuild(data);
        }
    }
    row = &current_rows(data, &count)[data->cursor];
    events = Menu_GetAllInputs();
    if (events & MenuInput_Back) {
        sfxBack();
        if (data->page == 1) {
            close_online(data);
            rebuild(data);
            return;
        }
        MeleeNativeSettingsSave();
        mn_804A04F0.entering_menu = 0;
        mn_80229894(MENU_KIND_SETTINGS, SEL_SETTINGS_3, 3);
        return;
    }
    if (events & MenuInput_Up) {
        move_cursor(data, -1);
    } else if (events & MenuInput_Down) {
        move_cursor(data, 1);
    } else if (events & (MenuInput_Left | MenuInput_Right | MenuInput_Confirm |
                         MenuInput_AButton))
    {
        switch (row->kind) {
        case PortRow_Toggle:
            *row->value = !*row->value;
            MeleeNativeSettingsSave();
            sfxMove();
            rebuild(data);
            break;
        case PortRow_Number:
            if (events & MenuInput_Left && *row->value > row->min) {
                (*row->value)--;
            } else if (events & (MenuInput_Right | MenuInput_Confirm | MenuInput_AButton) &&
                       *row->value < row->max)
            {
                (*row->value)++;
            }
            MeleeNativeSettingsSave();
            sfxMove();
            rebuild(data);
            break;
        case PortRow_Action:
            if (events & (MenuInput_Confirm | MenuInput_AButton)) {
                sfxForward();
                run_action(data, row->action);
                data->flash = 45;
                rebuild(data);
            }
            break;
        case PortRow_Disabled:
            break;
        }
    } else if (data->flash != 0 && --data->flash == 0) {
        rebuild(data);
    }
}

/// Display GObj: owns the texts; leaves when the menu flow moves on.
static void display_proc(HSD_GObj* gobj)
{
    PortMenuData* data = gobj->user_data;
    if (mn_804A04F0.cur_menu != PORT_MENU_KIND) {
        if (data->page == 1) {
            MeleeNativeNetplayMenuLeave();
        }
        free_all(data);
        port_gobj = NULL;
        port_ready = 0;
        HSD_GObjFree(gobj);
        return;
    }
    if (!port_ready) {
        // The Options list needs a few frames to slide away.
        if (mn_804D6BC8.cooldown == 0) {
            port_ready = 1;
            rebuild(data);
        }
    }
}

static void free_user_data(void* user_data)
{
    HSD_Free(user_data);
}

void mnPort_Init(void)
{
    HSD_GObj* gobj;
    HSD_GObjProc* proc;
    PortMenuData* data;

    mn_804D6BC8.cooldown = 5;
    mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;
    mn_804A04F0.cur_menu = PORT_MENU_KIND;
    mn_804A04F0.hovered_selection = 0;
    port_ready = 0;

    gobj = GObj_Create(HSD_GOBJ_CLASS_ITEM, 7U, 0x80);
    port_gobj = gobj;
    data = HSD_MemAlloc(sizeof(*data));
    HSD_ASSERTREPORT(__LINE__, data, "Can't get user_data.\n");
    memset(data, 0, sizeof(*data));
    GObj_InitUserData(gobj, 0, free_user_data, data);
    proc = HSD_GObj_SetupProc(gobj, display_proc, 0);
    proc->flags_3 = HSD_GObj_804D783C;

    proc = HSD_GObj_SetupProc(GObj_Create(0, 1, 0x80), think, 0);
    proc->flags_3 = HSD_GObj_804D783C;
}

#endif
