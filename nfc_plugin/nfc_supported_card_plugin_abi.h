#pragma once

/* NFC アプリ (Unleashed ファーム同梱、GPL-3.0) が .fal プラグインを読み込むときに期待する
 * ABI を、このリポジトリ (MIT) 用に独自に書き起こしたもの。
 * Unleashed 本体のヘッダ (applications/main/nfc/plugins/supported_cards/nfc_supported_card_plugin.h)
 * は複製していない — ここにあるのは型・関数シグネチャだけの、相互運用のための最小限の再定義。
 *
 * verify()/read()/parse() の意味、APPID 命名規則 ("*_parser" で終える) などの仕様は
 * NFC アプリ本体のドキュメントを参照。実装時に確認したファームのバージョン: unlshd-093。 */

#include <furi/core/string.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>

#define NFC_SUPPORTED_CARD_PLUGIN_APP_ID      "NfcSupportedCardPlugin"
#define NFC_SUPPORTED_CARD_PLUGIN_API_VERSION 1

typedef bool (*NfcSupportedCardPluginVerify)(Nfc* nfc);
typedef bool (*NfcSupportedCardPluginRead)(Nfc* nfc, NfcDevice* device);
typedef bool (*NfcSupportedCardPluginParse)(const NfcDevice* device, FuriString* parsed_data);

typedef struct {
    NfcProtocol protocol;
    NfcSupportedCardPluginVerify verify;
    NfcSupportedCardPluginRead read;
    NfcSupportedCardPluginParse parse;
} NfcSupportedCardsPlugin;
