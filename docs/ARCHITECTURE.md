# 仕組み

## パスワードと暗号の算出

どちらのゲームも、タグの UID (7 バイト) だけから次のように算出している。

```
h1   = HMAC-SHA256(KEY1, UID 7 バイト)
h1'  = h1 の各バイトの上位/下位ニブルを SBOX で置換
h2   = HMAC-SHA256(KEY2, h1')
PWD  = h2[28:32]      … PWD_AUTH (0x1B) で送る 4 バイト
PACK = BE EF          … ゲームが期待する応答
保護データ: AES-128-CTR
  key = h2[0:16]
  IV  = (h2[24:32] || UID || 00) XOR (h2[0:15] || 00)
  48 バイト = 16 バイト x 3 ブロック。各ブロックの最終バイトは先頭 15 バイトの和 (mod 256)
```

| | KEY1 | KEY2 | SBOX | 保護データ |
|---|---|---|---|---|
| 妖怪ウォッチ3 (3DS) | `SjqjE90z8w` | `bYYw75Ks9K` | 14,2,11,7,5,0,12,8,10,13,15,4,6,3,1,9 | ページ 6〜17 |
| 妖怪ウォッチ4 (Switch) | `DKtjn3JAZc` | `5g9D63n8Mt` | 4,9,15,0,5,10,14,1,6,11,13,2,7,12,3,8 | ページ 28〜39 |

妖怪ウォッチ4 の算出式と、ゲーム内部の処理の解析は
[switch-youkaiwatch4-medal](https://github.com/mar1mo-41414/switch-youkaiwatch4-medal) を参照。

### 妖怪ウォッチ4 の平文

先頭 7 バイトが ASCII で `NNNNXXX`。

- `NNNN`: シールに印刷されている番号 (例 `A315` → `0315`)。`int(XXX, 36) − 28652` と一致する。ゲームは使っていない
- `XXX`: アークの種別 ID (36 進 3 文字)。ゲームはこれでアークの種類を決める

残りは 0 (とチェックサム)。

## 種類の自動判別

認証の前にページ 0〜3 を読み、ページ 3 で判別する。

- `F5 10 ..` → 妖怪ウォッチ3 のメダル (ページ 4 は `BABO`)
- `E1 ..` (NDEF の Capability Container) → 妖怪ウォッチ4 のアーク

## 読み取りの流れ

`nfc_poller` (MfUltralight) を使う。

1. `AuthRequest` イベントで UID を取得し、(Auto のとき) `mf_ultralight_poller_read_page(0)` で種類を判別
2. パスワードを計算して `auth_context.password` にセット
3. poller が全ページを読む → `ReadSuccess` で結果をコピー
4. 保護データを復号してチェックサムを確認
5. PWD / PACK をページ 43 / 44 に書き込んでから `nfc_device_save` で保存
   (標準の NFC アプリでパスワードを入れて解除・保存したファイルとバイト単位で同じになる)

## 複製・種類の変更

1. 復号した平文を用意する (Change ID のときは `NNNNXXX` + 0 で作り直す)
2. 新しい UID = `04` + 乱数 6 バイト
3. ページ 0〜2 の UID / BCC0 (`88 ^ UID0 ^ UID1 ^ UID2`) / BCC1 (`UID3 ^ .. ^ UID6`) を書き換え
4. チェックサムを付けて、新しい UID の鍵で暗号化し直す
5. PWD / PACK を書き込んで保存

NDEF の URL と NXP の署名 (READ_SIG の値) は元のまま残す。妖怪ウォッチ4 はどちらも確認していない
(実機で確認済み)。

## 暗号の実装

FAP からファームの mbedtls は使えない (SDK の api_symbols.csv で非公開) ので、
SHA-256 / HMAC-SHA256 / AES-128 (暗号化方向のみ。CTR なので復号も同じ) を `ym_crypto.c` に実装している。

PC 上でテストできる:

```bash
cc -o test/test_crypto test/test_crypto.c ym_crypto.c && ./test/test_crypto
```

テストの内容: 妖怪ウォッチ4 の実物のアーク 2 枚のダンプで PWD・復号・チェックサムを確認、
妖怪ウォッチ3 の PWD を別実装 (Python) と比較、複製処理の出力をゲームで読み込めたファイルと比較。
