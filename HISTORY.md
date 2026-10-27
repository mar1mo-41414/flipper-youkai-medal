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

## 2026-09-26 v1.2 タグの検出待ち

- 不具合: Read を選ぶと、タグをかざしていなくても即座に結果画面 (`YW4 ark (Switch) ?` / `Not saved`) になっていた。
  タグが無いと poller がすぐ `ReadFailed` を返し、それを読み取り完了として扱っていたため。
- 対策: `ReadFailed` のうち AuthRequest まで進んでいないもの (タグが無い、または途中で離れた) は
  `NfcCommandReset` を返して検出からやり直す。認証を試した後の `ReadFailed` (パスワード違いなど) は従来どおり結果を表示。

## 2026-10-06 NFC アプリの supported_cards プラグイン (yokai_medal_parser.fal) を追加

- `.fap` (独立アプリ) とは別に、標準の NFC アプリの「Read」にそのまま乗る読み取り専用プラグインを追加。
  読み取り・保存だけを行い、複製・種類変更などの編集機能は無い (引き続き `.fap` 側だけに実装)。
  `.fap` の `application.fam` やソースは変更していない。
- `.fal` は ufbt (SDK のみ) ではビルドできないことが判明。supported_cards プラグインは
  `apptype=FlipperAppType.PLUGIN`, `requires=["nfc"]` で、親アプリ `nfc` の `application.fam` が
  存在するソースツリー内でないとビルドできず、ufbt の環境には `nfc` アプリのマニフェストが無いため
  `Missing application manifest for 'nfc'` で失敗する。
  → `scripts/build_plugin.sh` で Unleashed ファームウェア全体 (shallow clone、約 300MB) を取得し、
  `applications/main/nfc/plugins/supported_cards/` にこのリポジトリのプラグインを仮配置、
  公式の `fbt` でビルドする方式にした (クリーンな clone から約 2 分)。
- プラグイン本体は `nfc_plugin/yokai_medal_parser.c`。鍵導出は既存の `ym_crypto.c`/`.h` をそのまま使う
  (`scripts/build_plugin.sh` がビルド時にファーム側へコピーする)。
- NFC アプリのプラグイン ABI (`NfcSupportedCardsPlugin` 構造体、verify/read/parse のシグネチャ) は
  Unleashed 本体 (GPL-3.0) のヘッダをそのまま複製せず、型定義だけを `nfc_plugin/nfc_supported_card_plugin_abi.h`
  として自前で書き起こした (このリポジトリは MIT なため)。
- 判別方法・鍵導出は `.fap` と同じ (ページ 3 で YW3/YW4 を判別、UID からパスワードを計算)。
  `verify()` はページ 3 だけを読んで判定、`read()` は `mf_ultralight_poller_sync_read_card` に
  パスワードを渡して全ページ読み取り。
- CI (`build.yml`) に `build-fal` ジョブを追加。`build-fap` (既存、ufbt action) とは独立したジョブにし、
  タグ push 時はどちらも Release に添付する。

## 2026-10-06 .fal の実機確認

- `yokai_medal_parser.fal` を SD の `apps_data/nfc/plugins/` に置き、標準の NFC アプリの Read で
  UID `04 70 BC C2 DB 64 81` のアーク (はむはむ、MCN) を読み取り。
  保存されたダンプは、以前 `.fap` で手動解除・保存したダンプ (`switch-youkaiwatch4-medal/Data/00_Unlock.nfc`)
  と `diff` でバイト単位で完全一致 (UID・PWD・PACK・保護データの復号結果まで含む)。

## 2026-10-27 v1.4 空の NTAG213 への書き込み

- メニューに **Write .nfc to blank NTAG**、結果画面の Edit に **Write to blank NTAG** を追加。
  保存済みの (または今読んだ・今作った) ダンプを、空の NTAG213 に書き込んで実物にできるようにした。
- タグの UID は書き換えられないので、**かざしたタグの UID で鍵・PWD を算出し直してから**書き込む
  (選んだ `.nfc` の UID は使わない)。1 枚の `.nfc` から UID の違うタグを何枚でも作れる
  = 妖怪ウォッチ4 の「一度しか読み取れません」を気にせず使える。
- 実装: `nfc_poller` の `AuthRequest` イベントの中で同期的に全部やる
  (`mf_ultralight_poller_write_page` / `_read_page` / `_auth_pwd` はコールバックの中でしか呼べない)。
  `skip_auth = true` にして poller 本体の読み取りには進ませず、終わったら `NfcCommandStop`。
- 書き込む順番が重要。ページ 3〜39 → PWD (43) → PACK (44) → ACCESS (42) → **AUTH0 (41) を最後**。
  AUTH0 を先に書くと、それ以降のページが即座に保護されて PWD も設定も書けなくなる
  (ACCESS は AUTH0 が既定の 0xFF のままなら効かないので先に書いてよい)。
- 設定は元のダンプの AUTH0 (`0x1C`) / ACCESS (`0x80`) を使うが、**CFGLCK は落とす**。
  実物のアークは ACCESS = `0xC0` で CFGLCK (設定ページの永久ロック) が立っているが、
  立てると二度と書き換えられないタグになるため。ゲームは設定ページを見ないので影響なし。
- 書かないページ: 0〜2 (UID / BCC / 静的ロックバイト) と 40 (動的ロックバイト)。
  ロックをかけないので同じタグに何度でも書き直せる。
  NXP の署名は空タグのものが残るが、妖怪ウォッチ4 は READ_SIG を送らないので確認されない。
- 書き込み前のチェック: タグの種類がダンプと同じか (`MfUltralightType`、設定ページの位置が変わるため
  NTAG215/216 は受け付けない)、UID が 7 バイトか、静的・動的ロックバイトが立っていないか。
  既に保護範囲に入っているタグには、その UID から算出した PWD で PWD_AUTH を試す
  (このアプリで書いたタグなら通る。別のパスワードなら `Tag already has another password` で止める)。
- 書き込み後の検証: 書いた PWD で PWD_AUTH して PACK (`BE EF`) を確認 → ページ 3〜39 と設定ページを
  読み戻して照合。成功したら同じ内容のダンプを `SD:/nfc/yokai/` にも保存する。
- `rebuild_with_new_uid` を `rebuild_with_uid(uid)` に分け、複製 (乱数 UID) と書き込み (実タグの UID) で
  同じ再暗号化処理を共有するようにした。
- 未確認: 実機での書き込み (空の NTAG213 が手元に来てから確認する)。
  特に ページ 3 (NDEF の CC) は WRITE がビット OR になるタグがあるため、
  工場出荷状態 (`00 00 00 00` または `E1 10 12 00`) から `E1 11 12 0F` になるかを読み戻しで確認している。
- ビルド確認: Unleashed SDK `unlshd-094` (API 88.19) で `.fap` を警告ゼロでビルド、APPCHK も通った。

## 2026-10-27 Unleashed unlshd-094 対応

- `.fal` のビルドで固定していた Unleashed のバージョンを `unlshd-093` → `unlshd-094` に上げた
  (`.github/workflows/build.yml` の `UNLEASHED_REF`、`scripts/build_plugin.sh` の既定値)。
  `.fap` 側 (ufbt action の `sdk-channel: release`) が最新リリースを引くため、093 のままだと
  Release に別バージョン向けの `.fap` と `.fal` が並んでしまう。
- 094 では fbt の出力先ディレクトリが `build/f7-firmware-D/` → `build/f7-firmware-DC/` に変わって
  いて、`scripts/build_plugin.sh` が `.fal` のコピーに失敗していた (ビルド自体は成功していた)。
  決め打ちをやめて `build/f7-firmware-*/.extapps/` から探すようにした。
