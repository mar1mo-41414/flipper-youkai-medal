#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 妖怪メダル (3DS 妖怪ウォッチ3) / 妖怪アーク (Switch 妖怪ウォッチ4) の鍵導出。
 * どちらも UID から HMAC-SHA256 を 2 回 (間にニブル置換) かけるだけで、鍵と置換表が違う。 */

typedef enum {
    YmGameYw3, /* 3DS 妖怪ウォッチ3 の妖怪メダル */
    YmGameYw4, /* Switch 妖怪ウォッチ4 の妖怪アーク */
} YmGame;

#define YM_UID_LEN   7
#define YM_DATA_LEN  48
#define YM_PACK_0    0xBE
#define YM_PACK_1    0xEF

typedef struct {
    uint8_t pwd[4];
    uint8_t key[16];
    uint8_t iv[16];
} YmKeys;

void ym_derive(YmGame game, const uint8_t uid[YM_UID_LEN], YmKeys* out);

/* AES-128-CTR (暗号化と復号は同じ処理) */
void ym_aes128_ctr(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, uint8_t* out, size_t len);

/* 48 バイトを復号し、16 バイト x 3 ブロックのチェックサム (先頭 15 バイトの和) を確認する */
bool ym_decrypt_data(YmGame game, const uint8_t uid[YM_UID_LEN], const uint8_t in[YM_DATA_LEN], uint8_t out[YM_DATA_LEN]);

/* 保護データの先頭ページ番号 */
uint8_t ym_data_first_page(YmGame game);
