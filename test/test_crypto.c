/* PC 上で ym_crypto を検証する: cc -o test/test_crypto test/test_crypto.c ym_crypto.c */
#include <stdio.h>
#include <string.h>
#include "../ym_crypto.h"

/* ゲーム実機で読み込めたジバニャン (M88) のアークのページ 28-39 */
#define JIBANYAN_PAGES "73192D2AFB4E71F7519A1C2187A78158F8828FB71AEA8ED69613270838B30BE83F2B2B759438F78885D9568686768A46"

static void hex(const char* s, uint8_t* out, size_t n) {
    for(size_t i = 0; i < n; i++) {
        while(*s == ' ') s++;
        sscanf(s, "%2hhx", &out[i]);
        s += 2;
    }
}

static int check(YmGame g, const char* uid_s, const char* want_pwd, const char* data_s, const char* want_plain) {
    uint8_t uid[7], pwd[4], data[48], plain[48];
    hex(uid_s, uid, 7);
    hex(want_pwd, pwd, 4);
    YmKeys k;
    ym_derive(g, uid, &k);
    int ok = memcmp(k.pwd, pwd, 4) == 0;
    printf("%s uid %s pwd %02X%02X%02X%02X %s", g == YmGameYw3 ? "YW3" : "YW4", uid_s, k.pwd[0], k.pwd[1], k.pwd[2],
           k.pwd[3], ok ? "OK" : "NG");
    if(data_s) {
        hex(data_s, data, 48);
        bool cs = ym_decrypt_data(g, uid, data, plain);
        int pm = memcmp(plain, want_plain, 7) == 0;
        printf("  checksum %s plain \"%.7s\" %s", cs ? "OK" : "NG", plain, pm ? "OK" : "NG");
        ok = ok && cs && pm;
    }
    printf("\n");
    return ok;
}

int main(void) {
    int ok = 1;
    ok &= check(YmGameYw4, "0470BCC2DB6481", "CC0F5D9D",
                "D12ACB2F1856495715FA09F09D45DDEE0123321B1680ACB012A5AAADE8503D548F55272127CD15D072B8A0BD685C6BC8",
                "0315MCN");
    ok &= check(YmGameYw4, "04506F22D46480", "4FF59D34",
                "8232CEB033D712282676D1BD7F1D7F697A752AEA9730C73073A4116B0B7DB0F457B6951F2B8C16CCDE6B273936AE000F",
                "0330MD2");
    /* 3DS 版は実物のダンプが無いので Python の別実装の値と比較 */
    ok &= check(YmGameYw3, "0470BCC2DB6481", "2673B85D", NULL, NULL);
    /* アプリの複製処理 (rebuild_with_new_uid) と同じ手順: 平文 "0156M88" + チェックサム → 新 UID の鍵で暗号化。
       ゲームで読み込めた PC ツール (make_nfc.py) で作ったジバニャン (UID 04 4A F5 EA EB 04 28) の pages 28-39 / PWD と比較 */
    {
        uint8_t uid[7], plain[48] = {0}, enc[48], want[48], wpwd[4];
        hex("044AF5EAEB0428", uid, 7);
        memcpy(plain, "0156M88", 7);
        for(int b = 0; b < 48; b += 16) {
            uint8_t sum = 0;
            for(int i = 0; i < 15; i++) sum += plain[b + i];
            plain[b + 15] = sum;
        }
        YmKeys k;
        ym_derive(YmGameYw4, uid, &k);
        ym_aes128_ctr(k.key, k.iv, plain, enc, 48);
        hex(JIBANYAN_PAGES, want, 48);
        hex("550B09D8", wpwd, 4);
        int m = memcmp(enc, want, 48) == 0 && memcmp(k.pwd, wpwd, 4) == 0;
        printf("clone M88 (jibanyan) %s\n", m ? "OK" : "NG");
        ok &= m;
    }
    printf(ok ? "ALL OK\n" : "FAILED\n");
    return !ok;
}
