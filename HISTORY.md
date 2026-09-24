# HISTORY

## 2026-09-24 v1.0 初版

- 3DS (妖怪ウォッチ3) のメダルと Switch (妖怪ウォッチ4) のアークの両方に対応する Unleashed 用 FAP として作成。
- ufbt を venv に入れ、`--index-url=https://up.unleashedflip.com/directory.json` で `unlshd-093` の SDK を取得。
- FAP から mbedtls は使えない (api_symbols.csv で `-`)。SHA-256 / HMAC-SHA256 / AES-128 (暗号化方向のみ, CTR) を
  `ym_crypto.c` に自前で実装。
- 検証 (`test/test_crypto.c`, PC 上):
  - YW4: UID `04 70 BC C2 DB 64 81` → PWD `CC 0F 5D 9D`、実ダンプを復号して `0315MCN`・チェックサム一致
  - YW4: UID `04 50 6F 22 D4 64 80` → PWD `4F F5 9D 34`、`0330MD2`・チェックサム一致
  - YW3: PWD と AES の鍵ストリームが Python の別実装と一致
- 読み取りの流れ: `nfc_poller` (MfUltralight) の AuthRequest イベントで UID を取り、
  `mf_ultralight_poller_read_page(0)` でページ 0〜3 を読んで種類を判別 (auto の場合) → PWD をセット →
  poller が全ページを読む → ReadSuccess で結果をコピー。
- 保存: PWD / PACK をページ 43 / 44 に書き込んでから `nfc_device_save` (Unleashed の NFC アプリで
  パスワード解除して保存したときと同じ形になるようにした)。
- 未確認: 実機での動作全般。特に AuthRequest の中で READ を追加で送っても poller の状態が崩れないか、
  3DS メダルの AUTH0 がページ 3 より後ろか (ページ 0〜3 が認証なしで読めるか)。

## 2026-09-24 実機確認 (YW4 アーク)

- Flipper (Unleashed unlshd-093) で UID `04 70 BC C2 DB 64 81` のアークを読み取り。
  Auto / YW4 のどちらのメニューでも成功: PWD `CC 0F 5D 9D`、PACK `BE EF OK`、Pages 45/45、
  Checksum OK、`No.0315 ID MCN`、`nfc/yokai/YW4_MCN_0470BCC2DB6481.nfc` に保存。
- 保存された `.nfc` は、Unleashed の NFC アプリでパスワードを手入力して解除・保存したダンプ
  と `diff` で完全に一致。
- AuthRequest 中に READ を追加で送っても poller の流れは崩れなかった (Auto 判別が機能)。
- 3DS (YW3) メダルは手元に無いため未検証。

## 2026-09-24 v1.1 複製・種類変更機能

- 結果画面に Edit ボタン (右) を追加。サブメニュー:
  - Clone (new random UID): 復号した平文を、新しい乱数 UID (`04` + `furi_hal_random_fill_buf`) の鍵で暗号化し直す。
    ページ 0〜2 (UID / BCC0 / BCC1)、保護データ、PWD / PACK を書き換え、`mf_ultralight_set_uid` で UID も更新して保存。
  - Change ID + new UID (YW4 のみ): TextInput で 36 進 3 文字を入力 → 平文 = `%04d` (ID − 28652、範囲外は 0000) + ID + 0 埋め。
- メニューに Load .nfc file を追加 (`dialog_file_browser_show` + `nfc_device_load`)。種類はページ 3 で判別。
- `test/test_crypto.c` に複製処理と同じ手順のテストを追加し、ゲーム実機で読み込めた
  `make_nfc.py --id M88` 製のジバニャン (UID `04 4A F5 EA EB 04 28`) のページ 28〜39 / PWD と一致することを確認。
- NDEF の URL は元のまま (ゲームは見ていないことを実機で確認済み)。
- 実機確認: Edit → Clone / Change ID で作ったアークが、どちらも妖怪ウォッチ4 で読み込めた。
