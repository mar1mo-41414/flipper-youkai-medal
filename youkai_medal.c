/* Yo-kai Medal Reader: 妖怪ウォッチ3 (3DS) の妖怪メダル / 妖怪ウォッチ4 (Switch) の妖怪アークを
 * UID から算出したパスワードでアンロックし、全ページを .nfc に保存する。
 * 読んだ (または読み込んだ) ダンプから、UID や種別 ID を変えた複製も作れる。 */

#include <furi.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_device.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller.h>

#include "ym_crypto.h"

#define TAG      "YokaiMedal"
#define SAVE_DIR EXT_PATH("nfc/yokai")

/* YW4: シールの番号 = int(ID, 36) - 28652 (A315 = MCN, A330 = MD2 で確認) */
#define YW4_ID_NUM_BASE 28652

typedef enum {
    ViewMenu,
    ViewPopup,
    ViewResult,
    ViewEdit,
    ViewTextInput,
} ViewId;

typedef enum {
    MenuReadAuto,
    MenuReadYw3,
    MenuReadYw4,
    MenuLoad,
    MenuAbout,
} MenuItem;

typedef enum {
    EditClone,
    EditChangeId,
} EditItem;

typedef enum {
    EventReadDone = 100,
    EventEdit,
    EventIdEntered,
} CustomEvent;

typedef enum {
    SourceRead,
    SourceLoaded,
    SourceCreated,
} Source;

typedef struct {
    Gui* gui;
    ViewDispatcher* vd;
    Submenu* menu;
    Submenu* edit;
    Popup* popup;
    Widget* result;
    TextInput* text_input;
    NotificationApp* notif;
    DialogsApp* dialogs;

    Nfc* nfc;
    NfcPoller* poller;
    NfcDevice* device;
    MfUltralightData* data; /* 現在扱っているダンプ */

    MenuItem mode;
    ViewId current;
    Source source;
    char id_buf[8];

    /* poller コールバック (NFC スレッド) で埋める */
    YmGame game;
    bool detected; /* ページ 3 から種類を判別できたか */
    bool auth_tried;
    bool auth_ok;
    uint8_t pwd[4];
    uint8_t pack[2];

    /* 解析結果 */
    bool valid; /* 復号してチェックサムが合った (= 複製の元にできる) */
    uint8_t plain[YM_DATA_LEN];
    FuriString* text;
    FuriString* path;
} App;

static const char* game_name(YmGame g) {
    return g == YmGameYw3 ? "YW3 medal (3DS)" : "YW4 ark (Switch)";
}

/* ページ 3: 3DS メダル = F5 10 12 00 (直後のページ 4 が "BABO")、4 のアーク = NDEF CC (E1 ..) */
static bool game_from_page3(const uint8_t* p3, YmGame* out) {
    if(p3[0] == 0xF5 && p3[1] == 0x10) {
        *out = YmGameYw3;
        return true;
    }
    if(p3[0] == 0xE1) {
        *out = YmGameYw4;
        return true;
    }
    return false;
}

/* ---------- NFC ---------- */

static NfcCommand poller_callback(NfcGenericEvent event, void* context) {
    App* app = context;
    furi_assert(event.protocol == NfcProtocolMfUltralight);
    const MfUltralightPollerEvent* ev = event.event_data;

    switch(ev->type) {
    case MfUltralightPollerEventTypeRequestMode:
        ev->data->poller_mode = MfUltralightPollerModeRead;
        break;
    case MfUltralightPollerEventTypeAuthRequest: {
        const MfUltralightData* d = (const MfUltralightData*)nfc_poller_get_data(app->poller);
        size_t uid_len = 0;
        const uint8_t* uid = mf_ultralight_get_uid(d, &uid_len);
        if(app->mode == MenuReadYw3) {
            app->game = YmGameYw3;
        } else if(app->mode == MenuReadYw4) {
            app->game = YmGameYw4;
        } else {
            MfUltralightPageReadCommandData rd;
            app->detected = mf_ultralight_poller_read_page(event.instance, 0, &rd) == MfUltralightErrorNone &&
                            game_from_page3(rd.page[3].data, &app->game);
            if(!app->detected) app->game = YmGameYw4;
        }
        if(uid_len != YM_UID_LEN) {
            ev->data->auth_context.skip_auth = true;
            break;
        }
        YmKeys k;
        ym_derive(app->game, uid, &k);
        memcpy(app->pwd, k.pwd, 4);
        memcpy(ev->data->auth_context.password.data, k.pwd, 4);
        ev->data->auth_context.skip_auth = false;
        app->auth_tried = true;
        break;
    }
    case MfUltralightPollerEventTypeAuthSuccess:
        app->auth_ok = true;
        memcpy(app->pack, ev->data->auth_context.pack.data, 2);
        break;
    case MfUltralightPollerEventTypeAuthFailed:
        app->auth_ok = false;
        break;
    case MfUltralightPollerEventTypeReadFailed:
        /* 認証まで進んでいない = タグが無い (または途中で離れた)。結果を出さずに検出からやり直す */
        if(!app->auth_tried) {
            app->detected = false;
            app->auth_ok = false;
            return NfcCommandReset;
        }
        /* fall through */
    case MfUltralightPollerEventTypeReadSuccess:
        mf_ultralight_copy(app->data, (const MfUltralightData*)nfc_poller_get_data(app->poller));
        view_dispatcher_send_custom_event(app->vd, EventReadDone);
        return NfcCommandStop;
    default:
        break;
    }
    return NfcCommandContinue;
}

static void switch_view(App* app, ViewId id) {
    app->current = id;
    view_dispatcher_switch_to_view(app->vd, id);
}

static void start_read(App* app) {
    app->detected = false;
    app->auth_tried = false;
    app->auth_ok = false;
    memset(app->pack, 0, sizeof(app->pack));

    popup_reset(app->popup);
    popup_set_header(app->popup, "Hold medal/ark", 64, 14, AlignCenter, AlignTop);
    popup_set_text(
        app->popup,
        app->mode == MenuReadAuto ? "to Flipper's back\n(auto detect)" : "to Flipper's back",
        64,
        32,
        AlignCenter,
        AlignTop);
    switch_view(app, ViewPopup);
    notification_message(app->notif, &sequence_blink_start_cyan);

    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfUltralight);
    nfc_poller_start(app->poller, poller_callback, app);
}

static void stop_read(App* app) {
    if(app->poller) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
        notification_message(app->notif, &sequence_blink_stop);
    }
}

/* ---------- ダンプの解析・保存・複製 ---------- */

static void append_hex(FuriString* s, const uint8_t* p, size_t n) {
    for(size_t i = 0; i < n; i++) furi_string_cat_printf(s, "%s%02X", i ? " " : "", p[i]);
}

/* 保護データを復号してチェックサムを確認し、app->plain / app->valid を更新 */
static void analyze(App* app) {
    size_t uid_len = 0;
    const uint8_t* uid = mf_ultralight_get_uid(app->data, &uid_len);
    uint8_t first = ym_data_first_page(app->game);
    app->valid = false;
    if(uid_len != YM_UID_LEN || app->data->pages_read < first + YM_DATA_LEN / 4) return;
    uint8_t enc[YM_DATA_LEN];
    for(int i = 0; i < YM_DATA_LEN / 4; i++) memcpy(&enc[i * 4], app->data->page[first + i].data, 4);
    app->valid = ym_decrypt_data(app->game, uid, enc, app->plain);
}

/* YW4 の平文先頭 = "NNNN" (シールの番号) + 3 文字の 36 進 ID */
static void yw4_id(const App* app, char id[4]) {
    memcpy(id, app->plain + 4, 3);
    id[3] = '\0';
}

/* PWD/PACK をページに書き込み、/ext/nfc/yokai/ に保存する */
static bool save_dump(App* app) {
    size_t uid_len = 0;
    const uint8_t* uid = mf_ultralight_get_uid(app->data, &uid_len);
    uint8_t pwd_page = mf_ultralight_get_pwd_page_num(app->data->type);
    if(pwd_page && pwd_page + 1 < MF_ULTRALIGHT_MAX_PAGE_NUM) {
        memcpy(app->data->page[pwd_page].data, app->pwd, 4);
        app->data->page[pwd_page + 1].data[0] = app->pack[0];
        app->data->page[pwd_page + 1].data[1] = app->pack[1];
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("nfc"));
    storage_simply_mkdir(storage, SAVE_DIR);
    furi_record_close(RECORD_STORAGE);

    char label[8] = "medal";
    if(app->game == YmGameYw4) yw4_id(app, label);
    furi_string_printf(app->path, "%s/%s_%s_", SAVE_DIR, app->game == YmGameYw3 ? "YW3" : "YW4", label);
    for(size_t i = 0; i < uid_len; i++) furi_string_cat_printf(app->path, "%02X", uid[i]);
    furi_string_cat_str(app->path, ".nfc");

    nfc_device_set_data(app->device, NfcProtocolMfUltralight, app->data);
    return nfc_device_save(app->device, furi_string_get_cstr(app->path));
}

/* 新しい乱数 UID で app->plain を暗号化し直して app->data を作り替える */
static void rebuild_with_new_uid(App* app) {
    uint8_t uid[YM_UID_LEN];
    uid[0] = 0x04; /* NXP */
    furi_hal_random_fill_buf(&uid[1], YM_UID_LEN - 1);
    mf_ultralight_set_uid(app->data, uid, YM_UID_LEN);

    /* ページ 0-2: UID0-2, BCC0 / UID3-6 / BCC1, 内部, ロック */
    uint8_t* p = app->data->page[0].data;
    p[0] = uid[0];
    p[1] = uid[1];
    p[2] = uid[2];
    p[3] = 0x88 ^ uid[0] ^ uid[1] ^ uid[2];
    memcpy(app->data->page[1].data, &uid[3], 4);
    app->data->page[2].data[0] = uid[3] ^ uid[4] ^ uid[5] ^ uid[6];

    /* チェックサムを付け直してから暗号化 */
    for(int b = 0; b < YM_DATA_LEN; b += 16) {
        uint8_t sum = 0;
        for(int i = 0; i < 15; i++) sum += app->plain[b + i];
        app->plain[b + 15] = sum;
    }
    YmKeys k;
    ym_derive(app->game, uid, &k);
    uint8_t enc[YM_DATA_LEN];
    ym_aes128_ctr(k.key, k.iv, app->plain, enc, YM_DATA_LEN);
    uint8_t first = ym_data_first_page(app->game);
    for(int i = 0; i < YM_DATA_LEN / 4; i++) memcpy(app->data->page[first + i].data, &enc[i * 4], 4);

    memcpy(app->pwd, k.pwd, 4);
    app->pack[0] = YM_PACK_0;
    app->pack[1] = YM_PACK_1;
}

static void button_callback(GuiButtonType result, InputType type, void* context) {
    App* app = context;
    if(result == GuiButtonTypeRight && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->vd, EventEdit);
    }
}

static void show_result(App* app) {
    FuriString* s = app->text;
    furi_string_reset(s);
    size_t uid_len = 0;
    const uint8_t* uid = mf_ultralight_get_uid(app->data, &uid_len);
    analyze(app);

    static const char* src_name[] = {"", "Loaded: ", "Created: "};
    furi_string_cat_printf(
        s,
        "%s%s%s\n",
        src_name[app->source],
        game_name(app->game),
        (app->source != SourceCreated && !app->detected) ? " ?" : "");
    furi_string_cat_str(s, "UID: ");
    append_hex(s, uid, uid_len);
    furi_string_cat_str(s, "\nPWD: ");
    append_hex(s, app->pwd, 4);

    bool ok = app->valid;
    if(app->source == SourceRead) {
        bool pack_ok = app->auth_ok && app->pack[0] == YM_PACK_0 && app->pack[1] == YM_PACK_1;
        if(!app->auth_tried) {
            furi_string_cat_str(s, "\nAuth: not tried");
        } else if(app->auth_ok) {
            furi_string_cat_printf(s, "\nPACK: %02X %02X %s", app->pack[0], app->pack[1], pack_ok ? "OK" : "??");
        } else {
            furi_string_cat_str(s, "\nAuth FAILED");
        }
        ok = ok && app->auth_ok;
    }
    furi_string_cat_printf(s, "\nPages: %u/%u", app->data->pages_read, app->data->pages_total);
    if(app->data->pages_read >= ym_data_first_page(app->game) + YM_DATA_LEN / 4) {
        furi_string_cat_printf(s, "\nChecksum: %s", app->valid ? "OK" : "NG");
    }
    if(app->valid) {
        if(app->game == YmGameYw4) {
            char id[4];
            yw4_id(app, id);
            furi_string_cat_printf(s, "\nNo.%.4s  ID %s", (const char*)app->plain, id);
        }
        furi_string_cat_str(s, "\nData: ");
        append_hex(s, app->plain, 16);
    }

    if(app->source == SourceLoaded) {
        notification_message(app->notif, ok ? &sequence_success : &sequence_error);
    } else if(ok) {
        bool saved = save_dump(app);
        furi_string_cat_printf(
            s,
            "\n%s:\n%s",
            saved ? "Saved" : "Save FAILED",
            furi_string_get_cstr(app->path) + strlen(EXT_PATH("")));
        notification_message(app->notif, saved ? &sequence_success : &sequence_error);
    } else {
        furi_string_cat_str(s, "\nNot saved");
        notification_message(app->notif, &sequence_error);
    }

    widget_reset(app->result);
    widget_add_text_scroll_element(app->result, 0, 0, 128, ok ? 52 : 64, furi_string_get_cstr(s));
    if(ok) widget_add_button_element(app->result, GuiButtonTypeRight, "Edit", button_callback, app);
    switch_view(app, ViewResult);
}

static void load_file(App* app) {
    DialogsFileBrowserOptions opt;
    dialog_file_browser_set_basic_options(&opt, ".nfc", NULL);
    opt.base_path = EXT_PATH("nfc");
    furi_string_set(app->path, SAVE_DIR);
    if(!dialog_file_browser_show(app->dialogs, app->path, app->path, &opt)) return;

    if(!nfc_device_load(app->device, furi_string_get_cstr(app->path)) ||
       nfc_device_get_protocol(app->device) != NfcProtocolMfUltralight) {
        widget_reset(app->result);
        widget_add_text_scroll_element(app->result, 0, 0, 128, 64, "Not an NTAG/Ultralight\n.nfc file");
        switch_view(app, ViewResult);
        return;
    }
    mf_ultralight_copy(
        app->data, (const MfUltralightData*)nfc_device_get_data(app->device, NfcProtocolMfUltralight));
    app->detected = game_from_page3(app->data->page[3].data, &app->game);
    if(!app->detected) app->game = YmGameYw4;
    size_t uid_len = 0;
    const uint8_t* uid = mf_ultralight_get_uid(app->data, &uid_len);
    memset(app->pwd, 0, sizeof(app->pwd));
    if(uid_len == YM_UID_LEN) {
        YmKeys k;
        ym_derive(app->game, uid, &k);
        memcpy(app->pwd, k.pwd, 4);
    }
    app->pack[0] = YM_PACK_0;
    app->pack[1] = YM_PACK_1;
    app->source = SourceLoaded;
    show_result(app);
}

static void show_about(App* app) {
    widget_reset(app->result);
    widget_add_text_scroll_element(
        app->result,
        0,
        0,
        128,
        64,
        "Yo-kai Medal Reader v1.2\n"
        "Unlocks NTAG213 Yo-kai medals\n"
        "(Yo-kai Watch 3, 3DS) and\n"
        "Yo-kai arks (Yo-kai Watch 4,\n"
        "Switch) with a password\n"
        "derived from the UID, then\n"
        "saves a full dump to\n"
        "SD:/nfc/yokai/\n"
        "Auto: page 3 decides\n"
        "F5 10.. = YW3, E1 .. = YW4\n"
        "\n"
        "Edit (after read/load):\n"
        "- Clone: same data,\n"
        "  new random UID\n"
        "- Change ID (YW4): new ark\n"
        "  type (3-char base36 ID,\n"
        "  e.g. M88), new random UID\n"
        "YW4 accepts each UID once.");
    switch_view(app, ViewResult);
}

/* ---------- UI ---------- */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    app->mode = (MenuItem)index;
    switch(app->mode) {
    case MenuAbout:
        show_about(app);
        break;
    case MenuLoad:
        load_file(app);
        break;
    default:
        app->source = SourceRead;
        start_read(app);
        break;
    }
}

static void create_and_show(App* app) {
    rebuild_with_new_uid(app);
    app->source = SourceCreated;
    show_result(app);
}

static void text_input_done(void* context) {
    App* app = context;
    view_dispatcher_send_custom_event(app->vd, EventIdEntered);
}

static void edit_callback(void* context, uint32_t index) {
    App* app = context;
    if(index == EditClone) {
        create_and_show(app);
    } else if(index == EditChangeId) {
        yw4_id(app, app->id_buf);
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Ark ID (3 chars, e.g. M88)");
        text_input_set_minimum_length(app->text_input, 3);
        text_input_set_result_callback(app->text_input, text_input_done, app, app->id_buf, 4, false);
        switch_view(app, ViewTextInput);
    }
}

/* 入力された 36 進 3 文字の ID で YW4 の平文を作り直す */
static bool apply_id(App* app) {
    char id[4];
    uint32_t v = 0;
    for(int i = 0; i < 3; i++) {
        char c = app->id_buf[i];
        if(c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if(c >= '0' && c <= '9') {
            v = v * 36 + (c - '0');
        } else if(c >= 'A' && c <= 'Z') {
            v = v * 36 + (c - 'A' + 10);
        } else {
            return false;
        }
        id[i] = c;
    }
    if(app->id_buf[3] != '\0') return false;
    int32_t num = (int32_t)v - YW4_ID_NUM_BASE;
    if(num < 0 || num > 9999) num = 0;
    memset(app->plain, 0, sizeof(app->plain));
    snprintf((char*)app->plain, 5, "%04ld", (long)num);
    memcpy(app->plain + 4, id, 3);
    return true;
}

static void show_edit_menu(App* app) {
    submenu_reset(app->edit);
    submenu_set_header(app->edit, "Create a copy");
    submenu_add_item(app->edit, "Clone (new random UID)", EditClone, edit_callback, app);
    if(app->game == YmGameYw4) {
        submenu_add_item(app->edit, "Change ID + new UID", EditChangeId, edit_callback, app);
    }
    switch_view(app, ViewEdit);
}

static bool custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    switch(event) {
    case EventReadDone:
        stop_read(app);
        show_result(app);
        return true;
    case EventEdit:
        show_edit_menu(app);
        return true;
    case EventIdEntered:
        if(apply_id(app)) {
            create_and_show(app);
        } else {
            widget_reset(app->result);
            widget_add_text_scroll_element(
                app->result, 0, 0, 128, 64, "Invalid ID.\nUse 3 chars of 0-9 / A-Z\n(e.g. M88, MCN, MD2)");
            switch_view(app, ViewResult);
            app->valid = false;
        }
        return true;
    default:
        return false;
    }
}

static bool back_callback(void* context) {
    App* app = context;
    switch(app->current) {
    case ViewMenu:
        view_dispatcher_stop(app->vd);
        break;
    case ViewTextInput:
        switch_view(app, ViewEdit);
        break;
    case ViewEdit:
        switch_view(app, ViewResult);
        break;
    case ViewPopup:
        stop_read(app);
        switch_view(app, ViewMenu);
        break;
    default:
        switch_view(app, ViewMenu);
        break;
    }
    return true;
}

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->text = furi_string_alloc();
    app->path = furi_string_alloc();
    app->nfc = nfc_alloc();
    app->device = nfc_device_alloc();
    app->data = mf_ultralight_alloc();
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->gui = furi_record_open(RECORD_GUI);
    app->vd = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->vd, app);
    view_dispatcher_set_custom_event_callback(app->vd, custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->vd, back_callback);
    view_dispatcher_attach_to_gui(app->vd, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Read (auto detect)", MenuReadAuto, menu_callback, app);
    submenu_add_item(app->menu, "Read YW3 medal (3DS)", MenuReadYw3, menu_callback, app);
    submenu_add_item(app->menu, "Read YW4 ark (Switch)", MenuReadYw4, menu_callback, app);
    submenu_add_item(app->menu, "Load .nfc file", MenuLoad, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    view_dispatcher_add_view(app->vd, ViewMenu, submenu_get_view(app->menu));

    app->edit = submenu_alloc();
    view_dispatcher_add_view(app->vd, ViewEdit, submenu_get_view(app->edit));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->vd, ViewPopup, popup_get_view(app->popup));
    app->result = widget_alloc();
    view_dispatcher_add_view(app->vd, ViewResult, widget_get_view(app->result));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->vd, ViewTextInput, text_input_get_view(app->text_input));
    return app;
}

static void app_free(App* app) {
    stop_read(app);
    view_dispatcher_remove_view(app->vd, ViewMenu);
    view_dispatcher_remove_view(app->vd, ViewEdit);
    view_dispatcher_remove_view(app->vd, ViewPopup);
    view_dispatcher_remove_view(app->vd, ViewResult);
    view_dispatcher_remove_view(app->vd, ViewTextInput);
    submenu_free(app->menu);
    submenu_free(app->edit);
    popup_free(app->popup);
    widget_free(app->result);
    text_input_free(app->text_input);
    view_dispatcher_free(app->vd);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    mf_ultralight_free(app->data);
    nfc_device_free(app->device);
    nfc_free(app->nfc);
    furi_string_free(app->path);
    furi_string_free(app->text);
    free(app);
}

int32_t youkai_medal_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    switch_view(app, ViewMenu);
    view_dispatcher_run(app->vd);
    app_free(app);
    return 0;
}
