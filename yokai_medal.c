/* Yo-kai Medal Reader: 妖怪ウォッチ3 (3DS) の妖怪メダル / 妖怪ウォッチ4 (Switch) の妖怪アークを
 * UID から算出したパスワードでアンロックし、全ページを .nfc に保存する。
 * 読んだ (または読み込んだ) ダンプから、UID や種別 ID を変えた複製も作れる。
 * 作った複製は、空の NTAG (213/215/216) に書き込んで実物のメダル / アークにできる
 * (タグの UID は変えられないので、かざしたタグの UID で鍵・パスワードを作り直してから書く)。 */

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

/* 書き込むページの範囲。ページ 0〜2 (UID/BCC/ロックバイト) はタグ側で固定なので触らない。
 * 末尾 39 は NTAG213 のユーザーメモリ末尾 = メダル / アークが使う範囲の末尾
 * (NTAG215/216 の 40 ページ目以降は使わないので空のまま残す) */
#define WRITE_FIRST_PAGE 3
#define WRITE_LAST_PAGE  39

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
    MenuCreate,
    MenuWrite,
    MenuErase,
    MenuAbout,
} MenuItem;

typedef enum {
    EditClone,
    EditChangeId,
    EditWrite,
} EditItem;

typedef enum {
    EventReadDone = 100,
    EventTagDone, /* 書き込み / 消去が終わった */
    EventEdit,
    EventEraseConfirmed,
    EventIdEntered,
} CustomEvent;

typedef enum {
    SourceRead,
    SourceLoaded,
    SourceCreated,
} Source;

/* ID 入力画面を何のために出しているか */
typedef enum {
    IdInputChangeId, /* 既存のダンプの種別 ID を変える */
    IdInputCreate, /* 0 からアークを作る */
} IdInputPurpose;

/* NFC スレッドで行う処理 */
typedef enum {
    OpRead,
    OpWrite,
    OpErase,
} Operation;

/* 書き込み / 消去の結果 (どちらも同じ手順を踏むので共用) */
typedef enum {
    WriteNone, /* まだ試していない */
    WriteOk,
    WriteNoTag, /* 途中でタグが離れた */
    WriteBadType, /* NTAG213/215/216 以外のタグ */
    WriteBadUid, /* UID が 7 バイトでない */
    WriteLocked, /* ロックビットが立っていて書けない */
    WriteProtected, /* 既に別のパスワードがかかっている */
    WriteFailed, /* WRITE が通らなかった */
    WriteAuthFailed, /* 書き込んだ PWD で認証できなかった */
    WritePackBad, /* PACK が BE EF にならなかった */
    WriteVerifyFailed, /* 読み戻した内容が合わない */
    WriteRetryAuth, /* 内部用: 認証の順番を変えてやり直す (画面には出ない) */
} WriteStatus;

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
    MfUltralightData* out; /* 書き込み用に組み立てた、実タグ向けの内容 */

    MenuItem mode;
    ViewId current;
    Source source;
    Operation op;
    IdInputPurpose id_purpose;
    char id_buf[8];

    /* poller コールバック (NFC スレッド) で埋める */
    YmGame game;
    bool detected; /* ページ 3 から種類を判別できたか */
    bool auth_tried;
    bool auth_ok;
    uint8_t pwd[4];
    uint8_t pack[2];

    /* 書き込みの結果 (NFC スレッドで埋める) */
    WriteStatus wr_status;
    uint8_t wr_page; /* 失敗したページ */
    uint8_t wr_uid[YM_UID_LEN]; /* 書き込んだタグの UID */
    uint8_t wr_pack[2];
    const char* wr_type; /* かざされたタグの種類の名前 (静的な文字列) */
    MfUltralightType wr_tag_type;
    bool wr_retry; /* 認証の順番を変えて 2 回目を試している */

    /* 解析結果 */
    bool valid; /* 復号してチェックサムが合った (= 複製の元にできる) */
    uint8_t plain[YM_DATA_LEN];
    FuriString* text;
    FuriString* path;
} App;

static const char* game_name(YmGame g) {
    return g == YmGameYw3 ? "YW3 medal (3DS)" : "YW4 ark (Switch)";
}

/* ページ 3 で種類を判別する。
 * 3DS メダル = F5 10 12 00 (直後のページ 4 が "BABO")。
 * 妖怪ウォッチ4 のアーク = NDEF の CC が E1 11 12 0F。工場出荷の NTAG は
 * E1 10 12 00 (213) / E1 10 3E 00 (215) なので、バージョンのニブル (1.1) と
 * アクセス条件 (0F = NDEF 書き込み不可) まで見れば普通の NDEF タグと区別できる。 */
static bool game_from_page3(const uint8_t* p3, YmGame* out) {
    if(p3[0] == 0xF5 && p3[1] == 0x10) {
        *out = YmGameYw3;
        return true;
    }
    if(p3[0] == 0xE1 && (p3[1] & 0x01) && (p3[3] & 0x0F) == 0x0F) {
        *out = YmGameYw4;
        return true;
    }
    return false;
}

/* ページ 27 (保護データの直前) にある識別子 `BA B0 ..` の確認。
 * ページ 3 の CC は WRITE がビット OR で元に戻せないため、消去してもアークの CC が残り、
 * ページ 3 だけでは本物と区別できない。消去すると ページ 4〜39 が 0 になるのでそこも見ていたが、
 * 「非ゼロ」では消去後に別のデータを書いたタグを弾けない (CC が残っているので素通りする)。
 * そこで、消去で消えて他の用途ではまず現れない この識別子で判定する。
 * 後ろ 2 バイト (`01 01`) は版かもしれないので見ない。
 *
 * ページ 24〜27 は AUTH0 = 0x1C の範囲外なので認証なしで読める。READ は 4 ページ単位なので、
 * ページ 27 からではなく 24 から読んで 4 ページ目を見る (27 からだと保護領域にかかる)。
 * YW3 は AUTH0 = 6 でこの位置が保護されるため、この確認は YW4 だけに使う。
 * YW3 の CC (`F5 10 ..`) は NDEF の形ではないので、普通のタグと取り違える心配は無い。 */
static bool is_ark_data_page(const uint8_t* p27) {
    return p27[0] == 0xBA && p27[1] == 0xB0;
}

/* ---------- NFC ---------- */

/* 「ダンプの解析・保存・複製」の後ろで定義 (複製と同じ再暗号化を使う) */
static void write_to_tag(App* app, MfUltralightPoller* poller);
static void erase_tag(App* app, MfUltralightPoller* poller);

static NfcCommand poller_callback(NfcGenericEvent event, void* context) {
    App* app = context;
    furi_assert(event.protocol == NfcProtocolMfUltralight);
    const MfUltralightPollerEvent* ev = event.event_data;

    switch(ev->type) {
    case MfUltralightPollerEventTypeRequestMode:
        ev->data->poller_mode = MfUltralightPollerModeRead;
        break;
    case MfUltralightPollerEventTypeAuthRequest: {
        /* 書き込み / 消去はここで全部やる (poller が読み取りを始める前の、タグが起きている状態) */
        if(app->op != OpRead) {
            ev->data->auth_context.skip_auth = true;
            if(app->op == OpWrite) {
                write_to_tag(app, event.instance);
            } else {
                erase_tag(app, event.instance);
            }
            view_dispatcher_send_custom_event(app->vd, EventTagDone);
            return NfcCommandStop;
        }
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
            /* YW4 は ページ 27 の識別子も見て、消去済みのタグを弾く */
            if(app->detected && app->game == YmGameYw4) {
                MfUltralightPageReadCommandData nd;
                app->detected =
                    mf_ultralight_poller_read_page(event.instance, 24, &nd) == MfUltralightErrorNone &&
                    is_ark_data_page(nd.page[3].data);
            }
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
        if(app->op != OpRead || !app->auth_tried) {
            app->detected = false;
            app->auth_ok = false;
            return NfcCommandReset;
        }
        /* fall through */
    case MfUltralightPollerEventTypeReadSuccess:
        /* 書き込み / 消去のつもりで AuthRequest が来ないまま読み終わった
         * = パスワード認証に対応しないタグ */
        if(app->op != OpRead) {
            const MfUltralightData* d = (const MfUltralightData*)nfc_poller_get_data(app->poller);
            app->wr_type = mf_ultralight_get_device_name(d, NfcDeviceNameTypeShort);
            app->wr_status = WriteBadType;
            view_dispatcher_send_custom_event(app->vd, EventTagDone);
            return NfcCommandStop;
        }
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

static void start_poller(App* app, const char* header, const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 14, AlignCenter, AlignTop);
    popup_set_text(app->popup, text, 64, 32, AlignCenter, AlignTop);
    switch_view(app, ViewPopup);
    notification_message(app->notif, &sequence_blink_start_cyan);

    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolMfUltralight);
    nfc_poller_start(app->poller, poller_callback, app);
}

static void start_read(App* app) {
    app->op = OpRead;
    app->detected = false;
    app->auth_tried = false;
    app->auth_ok = false;
    memset(app->pack, 0, sizeof(app->pack));

    start_poller(
        app,
        "Hold medal/ark",
        app->mode == MenuReadAuto ? "to Flipper's back\n(auto detect)" : "to Flipper's back");
}

/* wr_retry は保ったまま poller を開始する (認証の順番を変えたやり直し用) */
static void start_tag_op(App* app) {
    app->wr_status = WriteNone;
    app->wr_page = 0;
    app->wr_type = NULL;
    memset(app->wr_uid, 0, sizeof(app->wr_uid));
    memset(app->wr_pack, 0, sizeof(app->wr_pack));

    if(app->op == OpErase) {
        start_poller(app, "Hold tag to erase", "to Flipper's back\n(data is cleared)");
    } else {
        start_poller(app, "Hold blank NTAG", "to Flipper's back\nand keep it still");
    }
}

static void start_write(App* app) {
    app->op = OpWrite;
    app->wr_retry = false;
    start_tag_op(app);
}

static void start_erase(App* app) {
    app->op = OpErase;
    app->wr_retry = false;
    start_tag_op(app);
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

/* 指定した UID で app->plain を暗号化し直して app->data を作り替える */
static void rebuild_with_uid(App* app, const uint8_t uid[YM_UID_LEN]) {
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

/* 新しい乱数 UID (04 + 乱数 6 バイト) で作り替える */
static void rebuild_with_new_uid(App* app) {
    uint8_t uid[YM_UID_LEN];
    uid[0] = 0x04; /* NXP */
    furi_hal_random_fill_buf(&uid[1], YM_UID_LEN - 1);
    rebuild_with_uid(app, uid);
}

/* ---------- 0 からアークを作る ---------- */

/* NTAG213 の GET_VERSION 応答。ファームの mf_ultralight_get_type_by_version() は
 * storage_size 0x0F + prod_type の下位ニブル 4 (NTAG) をこれと同じく NTAG213 と判定する */
static const uint8_t NTAG213_VERSION[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03};

/* 実物のアークと同じ固定ページ */
static const uint8_t ARK_CC[4] = {0xE1, 0x11, 0x12, 0x0F}; /* ページ 3: NDEF の CC */
static const uint8_t ARK_MAGIC[4] = {0xBA, 0xB0, 0x01, 0x01}; /* ページ 27 */
static const uint8_t ARK_DYN_LOCK[4] = {0x00, 0x00, 0x00, 0xBD}; /* ページ 40: 動的ロック (BD は RFUI) */
static const uint8_t ARK_CFG0[4] = {0x04, 0x00, 0x00, 0x1C}; /* MIRROR / AUTH0 = 0x1C */
static const uint8_t ARK_CFG1[4] = {0xC0, 0x00, 0x00, 0x00}; /* ACCESS: PROT + CFGLCK */

#define ARK_NDEF_FIRST_PAGE 4
#define ARK_NDEF_PAGES      (27 - ARK_NDEF_FIRST_PAGE) /* ページ 4〜26 */

/* NDEF に https://www.ywg.jp/<ID> を置く。実物のトークンは <ID> + 規則不明の文字列なので、
 * ゲームが種類を決めるのに使う先頭 3 文字 (種別 ID) だけ合わせる。
 * ゲームは NDEF を読まない (実機で確認済み) が、
 *  - このアプリ / プラグインの種別判定が「NDEF が残っていること」も見る
 *  - スマホからも妥当な URL として見える
 * ので埋めておく。 */
static void build_ark_ndef(uint8_t* buf, size_t len, const char* id) {
    memset(buf, 0, len);
    char url[24];
    int n = snprintf(url, sizeof(url), "www.ywg.jp/%.3s", id);
    if(n < 0) return;
    size_t url_len = (size_t)n;
    if(7 + url_len + 1 > len) return; /* 入らない (ID が 3 文字なら起きない) */

    size_t i = 0;
    buf[i++] = 0x03; /* NDEF メッセージ TLV */
    buf[i++] = (uint8_t)(4 + 1 + url_len); /* レコードの長さ */
    buf[i++] = 0xD1; /* MB | ME | SR, TNF = NFC Forum well-known */
    buf[i++] = 0x01; /* タイプ長 */
    buf[i++] = (uint8_t)(1 + url_len); /* ペイロード長 */
    buf[i++] = 'U'; /* URI レコード */
    buf[i++] = 0x04; /* 前置コード: https:// */
    memcpy(&buf[i], url, url_len);
    i += url_len;
    buf[i++] = 0xFE; /* 終端 TLV */
}

/* 元ダンプなしで、妖怪ウォッチ4 のアークのダンプ (NTAG213) を組み立てる。
 * app->plain (= 種別 ID を入れた平文) は呼び出し側で用意しておく。
 *
 * NXP の署名 (READ_SIG の値) は UID に対する NXP の秘密鍵の署名なので作れず、0 のまま。
 * ゲームは READ_SIG を送らないので影響しない (解析で確認済み)。
 * 物理タグに書く場合は、そのタグ自身の本物の署名が残る。 */
static void create_ark_from_scratch(App* app) {
    /* mf_ultralight_reset() は iso14443_3a 側しか消さないので、残りは自分で消す */
    mf_ultralight_reset(app->data);
    memset(app->data->page, 0, sizeof(app->data->page));
    memset(&app->data->version, 0, sizeof(app->data->version));
    memset(&app->data->signature, 0, sizeof(app->data->signature));
    memset(app->data->counter, 0, sizeof(app->data->counter));
    memset(app->data->tearing_flag, 0, sizeof(app->data->tearing_flag));
    app->data->auth_attempts = 0;

    app->game = YmGameYw4;
    app->detected = true;
    app->data->type = MfUltralightTypeNTAG213;
    app->data->pages_total = mf_ultralight_get_pages_total(MfUltralightTypeNTAG213);
    app->data->pages_read = app->data->pages_total;
    memcpy(&app->data->version, NTAG213_VERSION, sizeof(NTAG213_VERSION));

    /* エミュレートに必要な ISO14443-3A の値。NTAG は SAK = 00、ATQA の下位バイトは
     * UID の長さ (7 バイトなら 0x44) */
    Iso14443_3aData* iso = mf_ultralight_get_base_data(app->data);
    const uint8_t atqa[2] = {0x44, 0x00};
    iso14443_3a_set_atqa(iso, atqa);
    iso14443_3a_set_sak(iso, 0x00);

    char id[4];
    yw4_id(app, id);
    memcpy(app->data->page[3].data, ARK_CC, MF_ULTRALIGHT_PAGE_SIZE);
    uint8_t ndef[ARK_NDEF_PAGES * MF_ULTRALIGHT_PAGE_SIZE];
    build_ark_ndef(ndef, sizeof(ndef), id);
    for(uint8_t i = 0; i < ARK_NDEF_PAGES; i++) {
        memcpy(
            app->data->page[ARK_NDEF_FIRST_PAGE + i].data,
            &ndef[i * MF_ULTRALIGHT_PAGE_SIZE],
            MF_ULTRALIGHT_PAGE_SIZE);
    }
    memcpy(app->data->page[27].data, ARK_MAGIC, MF_ULTRALIGHT_PAGE_SIZE);
    memcpy(app->data->page[40].data, ARK_DYN_LOCK, MF_ULTRALIGHT_PAGE_SIZE);
    memcpy(app->data->page[41].data, ARK_CFG0, MF_ULTRALIGHT_PAGE_SIZE);
    memcpy(app->data->page[42].data, ARK_CFG1, MF_ULTRALIGHT_PAGE_SIZE);

    /* 乱数 UID で ページ 0〜2 (UID / BCC)・保護データ・PWD / PACK を埋める。
     * ページ 2 の internal バイトは NTAG21x の固定値 */
    rebuild_with_new_uid(app);
    app->data->page[2].data[1] = 0x48;
}

/* ---------- 空の NTAG への書き込み ---------- */

/* 書き込み先にできるタグ。7 バイト UID・パスワード認証・設定ページを持ち、
 * メダル / アークが使うページ (3〜39) が収まるもの。
 * 設定ページ・PWD・PACK の位置は種類ごとに違う (NTAG213 は 41〜44、215 は 131〜134、
 * 216 は 227〜230) ので、ページ番号はファームの helper から取る。 */
static bool writable_tag_type(MfUltralightType type) {
    return type == MfUltralightTypeNTAG213 || type == MfUltralightTypeNTAG215 ||
           type == MfUltralightTypeNTAG216;
}

/* 元ダンプから書き込むページの末尾 (ダンプが途中までしか読めていない場合はそこまで) */
static uint8_t write_last_page(const App* app) {
    uint8_t last = WRITE_LAST_PAGE;
    if(app->data->pages_read && app->data->pages_read - 1 < last) {
        last = (uint8_t)(app->data->pages_read - 1);
    }
    return last;
}

/* 書き込む内容を app->out に組み立てる。
 * 土台はかざされた実タグ (種類・バージョン・署名・ページ数・UID) で、そこに元ダンプの
 * ページ 3〜39 を載せ、保護データは実タグの UID の鍵で暗号化し直す。
 * PWD / PACK / 設定は、そのタグの種類の位置に置く。
 * head = 実タグのページ 0〜3 (読み出し済み)。 */
static void build_write_data(App* app, const MfUltralightData* tag, const MfUltralightPage* head) {
    size_t uid_len = 0;
    const uint8_t* uid = mf_ultralight_get_uid(tag, &uid_len);

    mf_ultralight_copy(app->out, tag);
    app->out->pages_read = app->out->pages_total;

    /* ページ 0〜3 は実タグの今の値 (UID / BCC / 内部 / ロックバイト / CC)。書き換えられない */
    for(uint8_t i = 0; i < 4; i++) app->out->page[i] = head[i];

    uint8_t last = write_last_page(app);
    for(uint8_t p = WRITE_FIRST_PAGE; p <= last; p++) app->out->page[p] = app->data->page[p];

    /* ページ 3 (NDEF の CC) は WRITE がビット OR になるタグがあるので、
     * 「今の値 + 元ダンプのビット」を書き込む値 = 期待値にする。
     * こうすると OR のタグでも普通に上書きされるタグでも結果が同じになる
     * (工場出荷の NTAG215 は CC = E1 10 3E 00 で、サイズのバイトが 0x3E のまま残る) */
    for(uint8_t i = 0; i < 4; i++) {
        app->out->page[WRITE_FIRST_PAGE].data[i] |= head[WRITE_FIRST_PAGE].data[i];
    }

    /* 保護データ: チェックサムを付け直して、実タグの UID の鍵で暗号化 */
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
    for(int i = 0; i < YM_DATA_LEN / 4; i++) memcpy(app->out->page[first + i].data, &enc[i * 4], 4);

    memcpy(app->pwd, k.pwd, 4);
    app->pack[0] = YM_PACK_0;
    app->pack[1] = YM_PACK_1;

    /* PWD / PACK */
    uint8_t pwd_page = mf_ultralight_get_pwd_page_num(app->out->type);
    memcpy(app->out->page[pwd_page].data, app->pwd, 4);
    app->out->page[pwd_page + 1].data[0] = app->pack[0];
    app->out->page[pwd_page + 1].data[1] = app->pack[1];
    app->out->page[pwd_page + 1].data[2] = 0; /* RFUI */
    app->out->page[pwd_page + 1].data[3] = 0;

    /* 設定: 元ダンプの AUTH0 / ACCESS を引き継ぐ (実物のアークは AUTH0 = 0x1C、
     * ACCESS = 0x80 で読み出しも保護)。元ダンプに設定が無ければ保護データの先頭から保護する。
     * CFGLCK (設定ページの永久ロック) は必ず落とす。立てると二度と書き換えられないタグになる */
    uint16_t cfg_page = mf_ultralight_get_config_page_num(app->out->type);
    memset(&app->out->page[cfg_page], 0, sizeof(MfUltralightPage));
    memset(&app->out->page[cfg_page + 1], 0, sizeof(MfUltralightPage));
    MfUltralightConfigPages* cfg = (MfUltralightConfigPages*)&app->out->page[cfg_page];
    cfg->auth0 = ym_data_first_page(app->game);
    cfg->access.prot = true;

    MfUltralightConfigPages* src = NULL;
    uint16_t src_cfg_page = mf_ultralight_get_config_page_num(app->data->type);
    if(src_cfg_page && app->data->pages_read > src_cfg_page + 1 &&
       mf_ultralight_get_config_page(app->data, &src) && src->auth0 &&
       src->auth0 < app->out->pages_total) {
        cfg->mirror = src->mirror;
        cfg->mirror_page = src->mirror_page;
        cfg->auth0 = src->auth0;
        cfg->access = src->access;
    }
    cfg->access.cfglck = false;
}

/* タグの種類ごとのページ番号 */
typedef struct {
    uint16_t cfg; /* 設定ページ 1 (MIRROR / AUTH0)。+1 が ACCESS */
    uint8_t pwd; /* PWD。+1 が PACK */
    uint8_t lock; /* 動的ロックバイト */
} TagPages;

static bool write_raw_page(
    App* app,
    MfUltralightPoller* poller,
    uint16_t page,
    const uint8_t data[MF_ULTRALIGHT_PAGE_SIZE]) {
    MfUltralightPage pg;
    memcpy(pg.data, data, MF_ULTRALIGHT_PAGE_SIZE);
    if(mf_ultralight_poller_write_page(poller, (uint8_t)page, &pg) == MfUltralightErrorNone) {
        return true;
    }
    app->wr_status = WriteFailed;
    app->wr_page = (uint8_t)page;
    return false;
}

static bool write_one_page(App* app, MfUltralightPoller* poller, uint16_t page) {
    return write_raw_page(app, poller, page, app->out->page[page].data);
}

/* 書き込みと消去の共通の下準備。種類と UID を確認し、ロックビットを見て、
 * 保護されていれば UID から計算した PWD で解除する。
 * head にはページ 0〜3 が入る (消去では種類の判別にも使う)。
 * detect_game = true なら、ページ 3 から YW3/YW4 を判別してから PWD を計算する。 */
static bool prepare_tag_op(
    App* app,
    MfUltralightPoller* poller,
    const MfUltralightData* tag,
    bool detect_game,
    MfUltralightPageReadCommandData* head,
    TagPages* pages,
    bool* authed) {
    app->wr_type = mf_ultralight_get_device_name(tag, NfcDeviceNameTypeShort);
    app->wr_tag_type = tag->type;
    *authed = false;

    if(!writable_tag_type(tag->type)) {
        app->wr_status = WriteBadType;
        return false;
    }
    size_t uid_len = 0;
    const uint8_t* uid = mf_ultralight_get_uid(tag, &uid_len);
    if(uid_len != YM_UID_LEN) {
        app->wr_status = WriteBadUid;
        return false;
    }
    memcpy(app->wr_uid, uid, YM_UID_LEN);

    pages->cfg = mf_ultralight_get_config_page_num(tag->type);
    pages->pwd = mf_ultralight_get_pwd_page_num(tag->type);
    pages->lock = (uint8_t)(pages->cfg - 1);

    /* ページ 0〜3 を読む。静的ロックバイト (ページ 2 の 3・4 バイト目) を確認する */
    if(mf_ultralight_poller_read_page(poller, 0, head) != MfUltralightErrorNone) {
        app->wr_status = WriteNoTag;
        return false;
    }
    if(head->page[2].data[2] || head->page[2].data[3]) {
        app->wr_status = WriteLocked;
        return false;
    }
    if(detect_game) {
        app->detected = game_from_page3(head->page[3].data, &app->game);
        if(!app->detected) app->game = YmGameYw4;
    }

    /* PWD_AUTH は活性化してから最初に送らないと通らない。保護されたページを READ すると
     * タグが NAK を返して IDLE 状態に戻り、以降のコマンドに応答しなくなるため
     * (読み取りが動くのは、poller が活性化直後に PWD_AUTH を送っているから)。
     * ページ 0〜3 は AUTH0 (YW3 は 6、YW4 は 0x1C) の範囲外なので、保護されていても読める。
     *
     * 保護されているかどうかは設定ページを読まないと分からず、その設定ページ自体が保護対象。
     * なので「認証してから読む」か「読んでから (必要なら) 認証する」かを、その処理で
     * ありふれた方に賭けて決める。外れたら 1 回だけ逆の順でやり直す (wr_retry)。
     *   消去   … 対象はこのアプリで書いた (= 保護された) タグなので、先に認証
     *   書き込み … 対象は空のタグなので、認証せずに読む */
    bool auth_first = (app->op == OpErase) != app->wr_retry;
    if(auth_first) {
        YmKeys k;
        ym_derive(app->game, uid, &k);
        MfUltralightPollerAuthContext auth = {0};
        memcpy(auth.password.data, k.pwd, 4);
        if(mf_ultralight_poller_auth_pwd(poller, &auth) != MfUltralightErrorNone) {
            /* 保護されていないタグだった (または別のパスワード)。認証なしでやり直す */
            app->wr_status = app->wr_retry ? WriteProtected : WriteRetryAuth;
            return false;
        }
        memcpy(app->wr_pack, auth.pack.data, 2);
        *authed = true;
    }

    /* 設定ページ (動的ロックバイト / CFG0 / ACCESS) を読む */
    MfUltralightPageReadCommandData rd;
    if(mf_ultralight_poller_read_page(poller, pages->lock, &rd) != MfUltralightErrorNone) {
        /* 読み出しも保護されている。認証ありでやり直す */
        app->wr_status = (auth_first || app->wr_retry) ? WriteProtected : WriteRetryAuth;
        return false;
    }
    /* 保護範囲が PWD / PACK のページまで来ているなら、書くには認証が必要 */
    /* rd.page[1] = CFG0 (MIRROR, RFUI, MIRROR_PAGE, AUTH0) */
    if(!*authed && rd.page[1].data[3] <= pages->pwd + 1) {
        app->wr_status = app->wr_retry ? WriteProtected : WriteRetryAuth;
        return false;
    }
    /* 動的ロックバイトが立っていると、ページ 16 以降が書けない */
    if(rd.page[0].data[0] || rd.page[0].data[1]) {
        app->wr_status = WriteLocked;
        return false;
    }
    return true;
}

/* かざされたタグに app->plain の内容を書き込む。NFC スレッド (AuthRequest) から呼ばれる。
 * 結果は app->wr_* に入れる。 */
static void write_to_tag(App* app, MfUltralightPoller* poller) {
    const MfUltralightData* tag = (const MfUltralightData*)nfc_poller_get_data(app->poller);

    /* 1. 種類・UID・ロックビットの確認と、必要なら保護の解除 */
    MfUltralightPageReadCommandData head;
    MfUltralightPageReadCommandData rd;
    TagPages pg;
    bool authed = false;
    /* 種類は元ダンプから分かっているので、ここでは判別しない */
    if(!prepare_tag_op(app, poller, tag, false, &head, &pg, &authed)) return;
    uint16_t cfg_page = pg.cfg;
    uint8_t pwd_page = pg.pwd;
    uint8_t lock_page = pg.lock;
    uint8_t last_page = write_last_page(app);

    /* 2. このタグ向けの中身を作る (UID で再暗号化、設定はこのタグの位置に) */
    build_write_data(app, tag, head.page);

    /* 3. 書き込む。AUTH0 (設定ページ 1) を最後にするのが肝心で、先に書くと
     *    それ以降のページが保護されて PWD も設定も書けなくなる */
    for(uint8_t p = WRITE_FIRST_PAGE; p <= last_page; p++) {
        if(!write_one_page(app, poller, p)) return;
    }
    if(!write_one_page(app, poller, pwd_page)) return; /* PWD */
    if(!write_one_page(app, poller, pwd_page + 1)) return; /* PACK */
    if(!write_one_page(app, poller, cfg_page + 1)) return; /* ACCESS (PROT) */
    if(!write_one_page(app, poller, cfg_page)) return; /* AUTH0 */

    /* 4. 書いた PWD で認証して PACK を確認する (1. で解除済みなら同じパスワードなので省略) */
    if(!authed) {
        MfUltralightPollerAuthContext auth = {0};
        memcpy(auth.password.data, app->pwd, 4);
        if(mf_ultralight_poller_auth_pwd(poller, &auth) != MfUltralightErrorNone) {
            app->wr_status = WriteAuthFailed;
            return;
        }
        memcpy(app->wr_pack, auth.pack.data, 2);
    }
    if(app->wr_pack[0] != YM_PACK_0 || app->wr_pack[1] != YM_PACK_1) {
        app->wr_status = WritePackBad;
        return;
    }

    /* 5. 読み戻して照合する (READ は 4 ページずつ返る) */
    for(uint8_t p = WRITE_FIRST_PAGE; p <= last_page; p += 4) {
        if(mf_ultralight_poller_read_page(poller, p, &rd) != MfUltralightErrorNone) {
            app->wr_status = WriteVerifyFailed;
            app->wr_page = p;
            return;
        }
        for(uint8_t i = 0; i < 4 && p + i <= last_page; i++) {
            if(memcmp(rd.page[i].data, app->out->page[p + i].data, 4) != 0) {
                app->wr_status = WriteVerifyFailed;
                app->wr_page = p + i;
                return;
            }
        }
    }
    /* 設定ページも確認する (PWD / PACK は読んでも 0 が返るので見ない) */
    if(mf_ultralight_poller_read_page(poller, lock_page, &rd) != MfUltralightErrorNone ||
       memcmp(rd.page[1].data, app->out->page[cfg_page].data, 4) != 0 ||
       rd.page[2].data[0] != app->out->page[cfg_page + 1].data[0]) {
        app->wr_status = WriteVerifyFailed;
        app->wr_page = (uint8_t)cfg_page;
        return;
    }

    /* 書いたタグの内容を、表示・保存用のダンプとして引き継ぐ */
    mf_ultralight_copy(app->data, app->out);
    app->wr_status = WriteOk;
}

/* ---------- 書き込んだタグの消去 ---------- */

/* NTAG21x の工場出荷値 (データシートの初期値)。
 * CFG0 = 04 00 00 FF
 *   04 = STRG_MOD_EN (強変調モード。データシートの初期値は 1)。ミラー機能は無効、
 *        AUTH0 = FF で保護なし。実物のアークの CFG0 が 04 00 00 1C で、
 *        メーカーが AUTH0 だけ変えていたことからも 04 が初期値と分かる
 * CFG1 = 00 05 00 00
 *   ACCESS = 0 (PROT・CFGLCK なし、AUTHLIM 0)、VCTID = 05。
 *   VCTID は VCSL コマンドが返すだけの値で、動作には影響しない */
static const uint8_t NTAG_BLANK_CFG0[4] = {0x04, 0x00, 0x00, 0xFF};
static const uint8_t NTAG_BLANK_CFG1[4] = {0x00, 0x05, 0x00, 0x00};
static const uint8_t NTAG_BLANK_PWD[4] = {0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t NTAG_BLANK_PAGE[4] = {0x00, 0x00, 0x00, 0x00};

/* 書き込んだタグを、普通の NTAG として使える状態に戻す。
 * NFC スレッド (AuthRequest) から呼ばれる。結果は app->wr_* に入れる。
 *
 * ページ 3 (NDEF の CC) だけは戻せない。NTAG21x の CC は WRITE がビット OR で、
 * 立てたビットを落とせないため (アークの CC は E1 11 12 0F で、下位ニブルの 0F が
 * 「NDEF 書き込み不可」を意味する)。メモリ自体は生の WRITE で書けるので、
 * Flipper や生コマンドを使うツールからは普通に書き換えられる。 */
static void erase_tag(App* app, MfUltralightPoller* poller) {
    const MfUltralightData* tag = (const MfUltralightData*)nfc_poller_get_data(app->poller);

    /* 1. 種類・UID・ロックビットの確認と、保護の解除。
     *    PWD は UID から計算するので、ページ 3 で YW3/YW4 を判別する必要がある */
    MfUltralightPageReadCommandData head;
    MfUltralightPageReadCommandData rd;
    TagPages pg;
    bool authed = false;
    if(!prepare_tag_op(app, poller, tag, true, &head, &pg, &authed)) return;
    uint8_t last_page = WRITE_LAST_PAGE; /* 書き込んだ範囲 (3〜39) だけを消す */

    /* 2. 先に保護を外す。こうすると以降は認証なしで書けるので、途中でタグが離れても
     *    保護がかかったままにならない (書き込みとは逆の順番) */
    if(!write_raw_page(app, poller, pg.cfg, NTAG_BLANK_CFG0)) return; /* AUTH0 = FF */
    if(!write_raw_page(app, poller, pg.cfg + 1, NTAG_BLANK_CFG1)) return; /* ACCESS = 0 */

    /* 3. PWD / PACK を工場出荷値に戻す */
    if(!write_raw_page(app, poller, pg.pwd, NTAG_BLANK_PWD)) return;
    if(!write_raw_page(app, poller, pg.pwd + 1, NTAG_BLANK_PAGE)) return;

    /* 4. ユーザーメモリを 0 で埋める (ページ 3 の CC は戻せないので触らない) */
    for(uint8_t p = WRITE_FIRST_PAGE + 1; p <= last_page; p++) {
        if(!write_raw_page(app, poller, p, NTAG_BLANK_PAGE)) return;
    }

    /* 5. 読み戻して照合する */
    for(uint8_t p = WRITE_FIRST_PAGE + 1; p <= last_page; p += 4) {
        if(mf_ultralight_poller_read_page(poller, p, &rd) != MfUltralightErrorNone) {
            app->wr_status = WriteVerifyFailed;
            app->wr_page = p;
            return;
        }
        for(uint8_t i = 0; i < 4 && p + i <= last_page; i++) {
            if(memcmp(rd.page[i].data, NTAG_BLANK_PAGE, MF_ULTRALIGHT_PAGE_SIZE) != 0) {
                app->wr_status = WriteVerifyFailed;
                app->wr_page = (uint8_t)(p + i);
                return;
            }
        }
    }
    if(mf_ultralight_poller_read_page(poller, pg.lock, &rd) != MfUltralightErrorNone ||
       memcmp(rd.page[1].data, NTAG_BLANK_CFG0, MF_ULTRALIGHT_PAGE_SIZE) != 0 ||
       rd.page[2].data[0] != NTAG_BLANK_CFG1[0]) {
        app->wr_status = WriteVerifyFailed;
        app->wr_page = (uint8_t)pg.cfg;
        return;
    }

    /* 6. PWD は読み出せない (常に 0 が返る) ので、工場出荷の FF FF FF FF で
     *    認証できることで確認する。保護は既に外してあるので、ここで失敗しても
     *    タグは使える状態 */
    MfUltralightPollerAuthContext auth = {0};
    memcpy(auth.password.data, NTAG_BLANK_PWD, 4);
    if(mf_ultralight_poller_auth_pwd(poller, &auth) != MfUltralightErrorNone) {
        app->wr_status = WriteAuthFailed;
        return;
    }
    memcpy(app->wr_pack, auth.pack.data, 2);

    app->wr_status = WriteOk;
}

static void button_callback(GuiButtonType result, InputType type, void* context) {
    App* app = context;
    if(result == GuiButtonTypeRight && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->vd, EventEdit);
    }
}

static void erase_button_callback(GuiButtonType result, InputType type, void* context) {
    App* app = context;
    if(result == GuiButtonTypeRight && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->vd, EventEraseConfirmed);
    }
}

/* 消去は実物のメダル / アークを壊しうる (NDEF の URL は固有で、消すと戻せない) ので確認する */
static void confirm_erase(App* app) {
    widget_reset(app->result);
    widget_add_text_scroll_element(
        app->result,
        0,
        0,
        128,
        52,
        "Erase NTAG\n"
        "Clears pages 4-39, removes\n"
        "the password and puts the\n"
        "config back to factory.\n"
        "A real medal/ark loses its\n"
        "NDEF URL for good - read it\n"
        "first if you want it back.\n"
        "Back = cancel");
    widget_add_button_element(
        app->result, GuiButtonTypeRight, "Erase", erase_button_callback, app);
    switch_view(app, ViewResult);
}

static void show_message(App* app, const char* text) {
    widget_reset(app->result);
    widget_add_text_scroll_element(app->result, 0, 0, 128, 64, text);
    switch_view(app, ViewResult);
}

static void show_result(App* app) {
    FuriString* s = app->text;
    furi_string_reset(s);
    size_t uid_len = 0;
    const uint8_t* uid = mf_ultralight_get_uid(app->data, &uid_len);
    analyze(app);

    if(app->source == SourceCreated) {
        furi_string_cat_printf(s, "Created: %s\n", game_name(app->game));
    } else if(app->detected) {
        furi_string_cat_printf(
            s, "%s%s\n", app->source == SourceLoaded ? "Loaded: " : "", game_name(app->game));
    } else {
        /* ページ 3 の CC か NDEF がメダル / アークの形をしていない (消去済みのタグなど) */
        furi_string_cat_printf(s, "Not a medal/ark?\n(tried as %s)\n", game_name(app->game));
    }
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

/* 書き込み / 消去の結果を表示する。書き込みが成功していれば、書いた内容を .nfc にも保存する */
static void show_tag_result(App* app) {
    bool erase = app->op == OpErase;
    FuriString* s = app->text;
    furi_string_reset(s);
    furi_string_cat_str(s, erase ? "Erase NTAG\n" : "Write to blank NTAG\n");

    bool ok = app->wr_status == WriteOk;
    switch(app->wr_status) {
    case WriteOk:
        furi_string_cat_str(s, "OK (verified)");
        break;
    case WriteBadType:
        furi_string_cat_printf(
            s,
            "Wrong tag type\nNeed: NTAG213/215/216\nGot: %s",
            app->wr_type ? app->wr_type : "?");
        break;
    case WriteBadUid:
        furi_string_cat_str(s, "Tag UID is not 7 bytes");
        break;
    case WriteLocked:
        furi_string_cat_str(s, "Tag is locked\n(lock bytes are set).\nUse a blank tag.");
        break;
    case WriteProtected:
        furi_string_cat_str(
            s,
            erase ? "Password does not match\nthis UID. Not a tag\nwritten by this app?" :
                    "Tag already has another\npassword. Unlock it first\nor use a blank tag.");
        break;
    case WriteFailed:
        furi_string_cat_printf(s, "WRITE rejected\nat page %u", app->wr_page);
        break;
    case WriteAuthFailed:
        furi_string_cat_str(
            s,
            erase ? "Cleared, but PWD_AUTH\nwith FF FF FF FF failed" :
                    "PWD_AUTH failed after\nwriting the password");
        break;
    case WritePackBad:
        furi_string_cat_printf(s, "PACK: %02X %02X (want BE EF)", app->wr_pack[0], app->wr_pack[1]);
        break;
    case WriteVerifyFailed:
        furi_string_cat_printf(s, "Verify failed\nat page %u", app->wr_page);
        break;
    default:
        furi_string_cat_str(s, "Tag lost");
        break;
    }

    if(app->wr_status != WriteNoTag && app->wr_status != WriteBadType) {
        furi_string_cat_printf(
            s, "\n%s %s\nUID: ", app->wr_type ? app->wr_type : "?", game_name(app->game));
        append_hex(s, app->wr_uid, YM_UID_LEN);
    }

    if(ok && erase) {
        furi_string_cat_printf(
            s,
            "\nPages %u-%u cleared\n"
            "Password removed\n"
            "PWD/PACK back to factory\n"
            "Page 3 (NDEF CC) cannot\nbe restored: its bits are\nwrite-once",
            WRITE_FIRST_PAGE + 1,
            WRITE_LAST_PAGE);
    } else if(ok) {
        furi_string_cat_str(s, "\nPWD: ");
        append_hex(s, app->pwd, 4);
        furi_string_cat_printf(s, "\nPACK: %02X %02X", app->wr_pack[0], app->wr_pack[1]);
        if(app->game == YmGameYw4) {
            char id[4];
            yw4_id(app, id);
            furi_string_cat_printf(s, "\nNo.%.4s  ID %s", (const char*)app->plain, id);
        }
        /* NTAG213 (実物と同じ) と NTAG215 (実機のゲームで読み込めるのを確認済み) は警告なし。
           NTAG216 はページ構成が NTAG215 と同じなので動くはずだが未検証 */
        if(app->wr_tag_type == MfUltralightTypeNTAG216) {
            furi_string_cat_str(s, "\n(NTAG216: untested\n in the game)");
        }
        /* 書いた内容と同じダンプを保存しておく (エミュレートや再書き込みに使える) */
        app->source = SourceCreated;
        bool saved = save_dump(app);
        furi_string_cat_printf(
            s,
            "\n%s:\n%s",
            saved ? "Saved" : "Save FAILED",
            furi_string_get_cstr(app->path) + strlen(EXT_PATH("")));
    }
    notification_message(app->notif, ok ? &sequence_success : &sequence_error);

    bool button = ok && !erase; /* 続けて次のタグに書く / 複製を作れるように Edit を残す */
    widget_reset(app->result);
    widget_add_text_scroll_element(
        app->result, 0, 0, 128, button ? 52 : 64, furi_string_get_cstr(s));
    if(button) {
        widget_add_button_element(app->result, GuiButtonTypeRight, "Edit", button_callback, app);
    }
    switch_view(app, ViewResult);
}

/* .nfc を選んで app->data に読み込む。成功したら true (失敗の表示はここで行う) */
static bool load_file(App* app) {
    DialogsFileBrowserOptions opt;
    dialog_file_browser_set_basic_options(&opt, ".nfc", NULL);
    opt.base_path = EXT_PATH("nfc");
    furi_string_set(app->path, SAVE_DIR);
    if(!dialog_file_browser_show(app->dialogs, app->path, app->path, &opt)) return false;

    if(!nfc_device_load(app->device, furi_string_get_cstr(app->path)) ||
       nfc_device_get_protocol(app->device) != NfcProtocolMfUltralight) {
        show_message(app, "Not an NTAG/Ultralight\n.nfc file");
        return false;
    }
    mf_ultralight_copy(
        app->data, (const MfUltralightData*)nfc_device_get_data(app->device, NfcProtocolMfUltralight));
    app->detected = game_from_page3(app->data->page[3].data, &app->game) &&
                    (app->game == YmGameYw3 ||
                     (app->data->pages_read > 27 && is_ark_data_page(app->data->page[27].data)));
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
    return true;
}

/* .nfc を選んで、その中身を空のタグに書き込む */
static void load_and_write(App* app) {
    if(!load_file(app)) return;
    analyze(app); /* app->plain / app->valid を用意する */
    if(!app->valid) {
        show_message(
            app,
            "Cannot decrypt this dump.\n"
            "Need a full medal/ark dump\n"
            "with a matching UID\n"
            "(read it with this app first).");
        notification_message(app->notif, &sequence_error);
        return;
    }
    start_write(app);
}

static void show_about(App* app) {
    show_message(
        app,
        "Yo-kai Medal Reader v1.6.1\n"
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
        "Create new ark (YW4):\n"
        "builds a full NTAG213 dump\n"
        "from just a 3-char ark ID,\n"
        "with no source dump. The\n"
        "NXP signature cannot be\n"
        "forged and stays zero; the\n"
        "game never reads it.\n"
        "\n"
        "Edit (after read/load):\n"
        "- Clone: same data,\n"
        "  new random UID\n"
        "- Change ID (YW4): new ark\n"
        "  type (3-char base36 ID,\n"
        "  e.g. M88), new random UID\n"
        "- Write to blank NTAG213/\n"
        "  215/216:\n"
        "  re-encrypts for the real\n"
        "  tag UID, then writes the\n"
        "  pages, PWD/PACK and\n"
        "  AUTH0/ACCESS (CFGLCK is\n"
        "  left off, so the tag can\n"
        "  be rewritten later)\n"
        "YW4 accepts each UID once.\n"
        "\n"
        "Erase NTAG: clears pages\n"
        "4-39, removes the password\n"
        "and puts PWD/PACK and the\n"
        "config back to factory, so\n"
        "the tag works as a plain\n"
        "NTAG again. Page 3 (NDEF\n"
        "CC) cannot be restored:\n"
        "its bits are write-once.");
}

/* ---------- UI ---------- */

/* ID 入力画面は text_input のコールバックを使うので、下の方で定義している */
static void ask_ark_id(App* app, IdInputPurpose purpose);

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    app->mode = (MenuItem)index;
    switch(app->mode) {
    case MenuAbout:
        show_about(app);
        break;
    case MenuLoad:
        if(load_file(app)) show_result(app);
        break;
    case MenuCreate:
        app->id_buf[0] = '\0';
        ask_ark_id(app, IdInputCreate);
        break;
    case MenuWrite:
        load_and_write(app);
        break;
    case MenuErase:
        confirm_erase(app);
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

/* 3 文字の種別 ID を入力させる */
static void ask_ark_id(App* app, IdInputPurpose purpose) {
    app->id_purpose = purpose;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Ark ID (3 chars, e.g. M88)");
    text_input_set_minimum_length(app->text_input, 3);
    text_input_set_result_callback(app->text_input, text_input_done, app, app->id_buf, 4, false);
    switch_view(app, ViewTextInput);
}

static void edit_callback(void* context, uint32_t index) {
    App* app = context;
    if(index == EditClone) {
        create_and_show(app);
    } else if(index == EditWrite) {
        start_write(app);
    } else if(index == EditChangeId) {
        yw4_id(app, app->id_buf);
        ask_ark_id(app, IdInputChangeId);
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
    submenu_add_item(app->edit, "Write to blank NTAG", EditWrite, edit_callback, app);
    switch_view(app, ViewEdit);
}

static bool custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    switch(event) {
    case EventReadDone:
        stop_read(app);
        show_result(app);
        return true;
    case EventTagDone:
        stop_read(app);
        /* 認証の順番の読みが外れた。poller を作り直して逆の順でもう一度 (1 回だけ) */
        if(app->wr_status == WriteRetryAuth) {
            app->wr_retry = true;
            start_tag_op(app);
            return true;
        }
        show_tag_result(app);
        return true;
    case EventEdit:
        show_edit_menu(app);
        return true;
    case EventEraseConfirmed:
        start_erase(app);
        return true;
    case EventIdEntered:
        if(apply_id(app)) {
            if(app->id_purpose == IdInputCreate) {
                create_ark_from_scratch(app);
                app->source = SourceCreated;
                show_result(app);
            } else {
                create_and_show(app);
            }
        } else {
            show_message(app, "Invalid ID.\nUse 3 chars of 0-9 / A-Z\n(e.g. M88, MCN, MD2)");
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
        switch_view(app, app->id_purpose == IdInputCreate ? ViewMenu : ViewEdit);
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
    app->out = mf_ultralight_alloc();
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
    submenu_add_item(app->menu, "Create new ark (YW4)", MenuCreate, menu_callback, app);
    submenu_add_item(app->menu, "Write .nfc to blank NTAG", MenuWrite, menu_callback, app);
    submenu_add_item(app->menu, "Erase NTAG (back to blank)", MenuErase, menu_callback, app);
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
    mf_ultralight_free(app->out);
    nfc_device_free(app->device);
    nfc_free(app->nfc);
    furi_string_free(app->path);
    furi_string_free(app->text);
    free(app);
}

int32_t yokai_medal_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    switch_view(app, ViewMenu);
    view_dispatcher_run(app->vd);
    app_free(app);
    return 0;
}
