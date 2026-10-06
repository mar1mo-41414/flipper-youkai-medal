/* Yo-kai Medal Reader の読み取り専用プラグイン版 (NFC アプリの supported_cards プラグイン)。
 *
 * NFC アプリの標準フロー (Apps → NFC → Read) にそのまま乗る。タグをかざすと:
 *   1. verify(): ページ 3 を見て妖怪ウォッチ3 (3DS) / 4 (Switch) のタグか判別する
 *   2. read(): UID からパスワードを計算して PWD_AUTH → 全ページ読み取り
 *   3. parse(): 結果 (UID/PWD/PACK/チェックサム) を画面用テキストにする
 * 読み取りに成功すると、NFC アプリが自動で /ext/nfc/ 配下にダンプを保存する (PWD/PACK 入り)。
 *
 * UID や種類を変える複製・編集機能はこちらには無い。そちらは Apps から直接起動する
 * Yo-kai Medal Reader (youkai_medal.c, yokai_medal.fap) を使う。
 * 鍵導出 (HMAC-SHA256 x2 + ニブル置換 + AES-128-CTR) は ym_crypto.c と共有している。 */

#include "nfc_supported_card_plugin_abi.h"

#include <string.h>

#include <flipper_application/flipper_application.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight_poller_sync.h>

#include "ym_crypto.h"

#define TAG "YokaiMedalParser"

/* ページ 3: 3DS メダル = F5 10 12 00 (直後のページ 4 が "BABO")、4 のアーク = NDEF CC (E1 ..) */
static bool yokai_medal_game_from_page3(const uint8_t* p3, YmGame* out) {
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

static bool yokai_medal_verify(Nfc* nfc) {
    furi_assert(nfc);

    MfUltralightPage page3;
    MfUltralightError err = mf_ultralight_poller_sync_read_page(nfc, 3, &page3);
    if(err != MfUltralightErrorNone) {
        FURI_LOG_D(TAG, "Failed to read page 3: %d", err);
        return false;
    }
    YmGame game;
    bool ok = yokai_medal_game_from_page3(page3.data, &game);
    if(!ok) FURI_LOG_D(TAG, "Page 3 does not match YW3/YW4");
    return ok;
}

static bool yokai_medal_read(Nfc* nfc, NfcDevice* device) {
    furi_assert(nfc);
    furi_assert(device);

    bool read_ok = false;
    MfUltralightData* data = mf_ultralight_alloc();

    do {
        /* ページ 0-3 だけ素で読んで UID と種別を確定する (verify() の結果は使い回せない) */
        MfUltralightPage page0, page3;
        if(mf_ultralight_poller_sync_read_page(nfc, 0, &page0) != MfUltralightErrorNone) break;
        if(mf_ultralight_poller_sync_read_page(nfc, 3, &page3) != MfUltralightErrorNone) break;

        YmGame game;
        if(!yokai_medal_game_from_page3(page3.data, &game)) break;

        /* UID = ページ 0 の先頭 3 バイト + ページ 1 の 4 バイト (NXP の 7 バイト UID) */
        uint8_t uid[YM_UID_LEN];
        MfUltralightPage page1;
        if(mf_ultralight_poller_sync_read_page(nfc, 1, &page1) != MfUltralightErrorNone) break;
        uid[0] = page0.data[0];
        uid[1] = page0.data[1];
        uid[2] = page0.data[2];
        memcpy(&uid[3], page1.data, 4);

        YmKeys keys;
        ym_derive(game, uid, &keys);

        MfUltralightPollerAuthContext auth = {0};
        memcpy(auth.password.data, keys.pwd, 4);

        MfUltralightError err = mf_ultralight_poller_sync_read_card(nfc, data, &auth);
        /* 認証失敗 (パスワード違い = 別タグ) 以外は、途中まで読めた分も活かす */
        if(err != MfUltralightErrorNone && err != MfUltralightErrorProtocol) {
            FURI_LOG_D(TAG, "Read failed: %d", err);
        }
        if(data->pages_read == 0) break;

        nfc_device_set_data(device, NfcProtocolMfUltralight, data);
        read_ok = true;
    } while(false);

    mf_ultralight_free(data);
    return read_ok;
}

static bool yokai_medal_parse(const NfcDevice* device, FuriString* parsed_data) {
    furi_assert(device);
    furi_assert(parsed_data);

    const MfUltralightData* data = nfc_device_get_data(device, NfcProtocolMfUltralight);
    if(data == NULL) return false;

    bool parsed = false;
    do {
        if(data->pages_read < 4) break;
        YmGame game;
        if(!yokai_medal_game_from_page3(data->page[3].data, &game)) break;

        uint8_t uid[YM_UID_LEN];
        uid[0] = data->page[0].data[0];
        uid[1] = data->page[0].data[1];
        uid[2] = data->page[0].data[2];
        memcpy(&uid[3], data->page[1].data, 4);

        YmKeys keys;
        ym_derive(game, uid, &keys);

        uint8_t first = ym_data_first_page(game);
        bool have_data = data->pages_read >= (uint16_t)(first + YM_DATA_LEN / 4);
        bool checksum_ok = false;
        uint8_t plain[YM_DATA_LEN];
        if(have_data) {
            uint8_t enc[YM_DATA_LEN];
            for(int i = 0; i < YM_DATA_LEN / 4; i++) {
                memcpy(&enc[i * 4], data->page[first + i].data, 4);
            }
            checksum_ok = ym_decrypt_data(game, uid, enc, plain);
        }

        furi_string_printf(
            parsed_data,
            "\e#%s\nUID:",
            game == YmGameYw3 ? "Yo-kai Watch 3 medal" : "Yo-kai Watch 4 ark");
        for(int i = 0; i < YM_UID_LEN; i++) {
            furi_string_cat_printf(parsed_data, " %02X", uid[i]);
        }
        furi_string_cat_printf(
            parsed_data,
            "\nPWD: %02X %02X %02X %02X\nPages: %u/%u",
            keys.pwd[0],
            keys.pwd[1],
            keys.pwd[2],
            keys.pwd[3],
            data->pages_read,
            data->pages_total);
        if(have_data) {
            furi_string_cat_printf(parsed_data, "\nChecksum: %s", checksum_ok ? "OK" : "NG");
            if(checksum_ok && game == YmGameYw4) {
                furi_string_cat_printf(parsed_data, "\nNo.%.4s  ID %.3s", (const char*)plain, (const char*)plain + 4);
            }
        }

        parsed = true;
    } while(false);

    return parsed;
}

/* Actual implementation of app<>plugin interface */
static const NfcSupportedCardsPlugin yokai_medal_plugin = {
    .protocol = NfcProtocolMfUltralight,
    .verify = yokai_medal_verify,
    .read = yokai_medal_read,
    .parse = yokai_medal_parse,
};

/* Plugin descriptor to comply with basic plugin specification */
static const FlipperAppPluginDescriptor yokai_medal_plugin_descriptor = {
    .appid = NFC_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = NFC_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &yokai_medal_plugin,
};

/* Plugin entry point - must return a pointer to const descriptor  */
const FlipperAppPluginDescriptor* yokai_medal_parser_ep(void) {
    return &yokai_medal_plugin_descriptor;
}
