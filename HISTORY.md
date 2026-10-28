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
- 同じスクリプトで、`UNLEASHED_SRC` を指定すると成功しても終了コードが 1 になっていたのを修正。
  EXIT トラップの `cleanup()` が `[ -n "$workdir" ] && rm -rf ...` で終わっており、
  `UNLEASHED_SRC` 指定時は `workdir` が空 = `[ -n "" ]` の 1 がそのまま
  スクリプトの終了コードになっていた (`return 0` を追加)。
  CI は `UNLEASHED_SRC` を使わないので影響は無かった。
- ビルド確認: `unlshd-094` のフルソースから `.fal` を生成できた (API 88.19、APPCHK 通過、7,256 bytes)。

## 2026-10-27 書き込み先に NTAG215 / NTAG216 を追加

- 実機確認で、手元のタグが全部 NTAG215 だったため `Need: NTAG213 / Got: NTAG215` で止まっていた。
  「ダンプと同じ種類のタグだけ」という制限を外し、NTAG213 / 215 / 216 に書けるようにした。
- 設定ページ・PWD・PACK・動的ロックの位置が種類ごとに違う。ページ番号はファームの
  `mf_ultralight_get_config_page_num()` / `_get_pwd_page_num()` から取るようにした。

  | 種類 | 総ページ | CFG0 | ACCESS | PWD | PACK | 動的ロック |
  |---|---|---|---|---|---|---|
  | NTAG213 | 45 | 41 | 42 | 43 | 44 | 40 |
  | NTAG215 | 135 | 131 | 132 | 133 | 134 | 130 |
  | NTAG216 | 231 | 227 | 228 | 229 | 230 | 226 |

- 書き込む内容の組み立てを `build_write_data()` に分離し、`app->out` (2 つ目の `MfUltralightData`)
  に作るようにした。土台はかざした実タグ (種類・バージョン・署名・ページ数・UID) で、
  そこに元ダンプのページ 3〜39 を載せ、保護データはそのタグの UID の鍵で暗号化し直す。
  成功したら `app->out` を `app->data` に引き継ぐので、保存される `.nfc` が
  実際に書いたタグ (NTAG215 なら 135 ページ、PWD はページ 133) と一致する。
  以前は元ダンプ (NTAG213) を書き換えて使っていたので、種類が違うと保存が壊れていた。
- 書き込むユーザーメモリは `WRITE_LAST_PAGE = 39` まで (= NTAG213 のユーザーメモリ末尾 =
  メダル / アークが使う範囲)。NTAG215/216 のページ 40 以降は空のまま残す。
- ページ 3 (NDEF の CC) の扱いを修正。NTAG21x は CC への WRITE がビット OR になるため、
  工場出荷の NTAG215 (CC = `E1 10 3E 00`) に `E1 11 12 0F` を書くと結果は `E1 11 3E 0F` になり、
  そのままだと読み戻しの照合が `Verify failed at page 3` で落ちる。
  「今の値 + 元ダンプの値」を書き込む値 = 期待値にすることで、OR のタグでも普通に上書きされる
  タグでも結果が一致するようにした。サイズのバイトが `0x3E` のまま残るが、
  種類の判別 (先頭が `E1`) もゲームの動作も NDEF を見ないので影響しない。
- 結果画面に、かざしたタグの種類を表示。NTAG213 以外のときは
  `! not NTAG213: untested with the game` を出す (実物のメダル / アークは NTAG213 で、
  ゲームが NTAG215/216 を受け付けるかは未検証。ゲームが読むページ 0x00〜0x2A の中身は同じになる)。
- ビルド確認: `unlshd-094` (API 88.19) で警告ゼロ、APPCHK 通過。

## 2026-10-27 CI のバージョン指定を「常に最新リリース」に

- `.fap` 側 (ufbt action の `sdk-channel: release`) が最新リリースを引くのに `.fal` 側だけ
  タグ固定だったため、新しいリリースが出るたびに Release に別バージョン向けの 2 つが並ぶ
  状態だった。手で bump する運用をやめ、`.fal` 側も最新リリースを自動で解決するようにした。
- `scripts/build_plugin.sh`: `UNLEASHED_REF` 省略時は `directory.json` (`.fap` 側と同じ
  `up.unleashedflip.com` の index) の release チャンネルからバージョンを解決する。
  release チャンネルのバージョン文字列はそのままファームの git タグ名になっている (例 `unlshd-094`)。
  解決に失敗したら古い値で代用せずエラーで止める。
- 使ったバージョンを `出力先/unleashed_ref.txt` に書き出し、CI が artifact 名に使う
  (バージョン解決の実装を CI 側に重複させないため)。`UNLEASHED_SRC` 指定時は
  そのツリーの `git describe` を書く (解決した最新版を名乗らないように)。
- `.github/workflows/build.yml` から `UNLEASHED_REF` のハードコードを削除。
- 通し確認: 自動解決 (`unlshd-094`) → clone → fbt → `.fal` + `unleashed_ref.txt` 生成まで exit 0。
  `UNLEASHED_SRC` 指定の経路も確認済み。

## 2026-10-27 空の NTAG215 への書き込みの実機確認

- 空の NTAG215 への書き込みに成功。書き込み後の検証 (書いた PWD で PWD_AUTH → PACK `BE EF` の確認
  → ページ 3〜39 と設定ページの読み戻し照合) も通った。
- **別実装での裏付け**: 更新前の `yokai_medal_parser.fal` (v1.3 のまま。NTAG215 対応のコード変更は
  入っていない) で、書いたタグを標準の NFC アプリから読み取り → 正しく復号できた。
  このアプリの書き込み処理とは独立した実装で読めているので、次が確認できたことになる。
  - PWD / PACK を NTAG215 の位置 (ページ 133 / 134) に正しく書けている → PWD_AUTH が通った
  - AUTH0 / ACCESS (ページ 131 / 132) も正しい → 保護がかかった状態で、パスワードで解除できた
  - ページ 28〜39 が実タグの UID の鍵で暗号化されている → 復号とチェックサムが一致
  - ページ 3 の CC がビット OR で `E1 11 3E 0F` (サイズのバイトが工場出荷の `0x3E` のまま) に
    なっていても、種別判定 (先頭が `E1`) は通る
- `.fal` 側は元から種類に依存しない作りだった (`read()` は `mf_ultralight_poller_sync_read_card()`、
  `parse()` は `ym_data_first_page()` とページ 28〜39 しか見ない) ため、NTAG215 を読むための
  変更は不要。
- 未確認: **ゲーム (妖怪ウォッチ4) が NTAG215 のアークを受け付けるか**。
  ゲームが読むページ 0x00〜0x2A の中身は NTAG213 と同じになるが、NTAG213 では設定ページだった
  ページ 40〜42 が NTAG215 では空 (0) のまま残る点だけが違う。
  解析ではゲームはこの領域を参照していない (ページ 28〜39 を復号するだけ) が、未検証。
  弾かれた場合は、ページ 40〜42 に NTAG213 の設定値 (`04 00 00 1C` / `C0 00 00 00` / PWD) を
  そのまま書き込んで試す。

## 2026-10-28 NTAG215 のアークをゲームで読み込み確認

- 空の NTAG215 に書いたアークを、妖怪ウォッチ4 (Switch) の実機で読み込み。
  **何も弾かれず、そのままアークとして読み込まれた**。
- これで、NTAG213 との唯一の違いだったページ 40〜42 (NTAG213 では設定ページ、NTAG215 では空) が
  ゲームの判定に影響しないことが裏付けられた。解析で「ゲームはページ 0x00〜0x2A を FAST_READ するが
  参照しているのはページ 28〜39 の復号だけ」としていた読みが、実機で確認できたことになる。
- 結果画面の警告を整理。NTAG213 (実物と同じ) と NTAG215 (確認済み) には何も出さず、
  NTAG216 (ページ構成は NTAG215 と同じだが未検証) にだけ注記を出すようにした。
- 書き込み待ちの画面の文言も `Hold blank NTAG213` → `Hold blank NTAG` に変更。
  README / README-EN / docs も「NTAG213 のみ」「未検証」の記述を更新した。
- 以上で v1.4 の書き込み機能は **NTAG215 で** 書き込み → 読み戻し照合 →
  別実装 (`.fal`) での読み取り・復号 → ゲームでの読み込み まで一通り確認できた。
  空の NTAG213 / NTAG216 は手元に無いため、そちらへの書き込みは実機未確認
  (処理は同じで、設定ページ・PWD・PACK の位置だけが違う)。

## 2026-10-28 v1.5 書き込んだタグの消去 (Erase)

- メニューに **Erase NTAG (back to blank)** を追加。書き込んだタグを、普通の NTAG として
  使える状態に戻せるようにした。

  | 順 | ページ | 書く値 |
  |---|---|---|
  | 1 | CFG0 | `04 00 00 FF` (AUTH0 = FF = 保護なし) |
  | 2 | ACCESS | `00 05 00 00` (PROT・CFGLCK なし、VCTID = 05) |
  | 3 | PWD / PACK | `FF FF FF FF` / `00 00 00 00` |
  | 4 | ページ 4〜39 | `00 00 00 00` |

- **書き込みとは逆に、保護を外すのを最初にやる**。こうすると途中でタグが離れても
  「中身が消えた上に保護がかかったまま」にならない。
- 工場出荷値はデータシートの初期値。CFG0 の `04` (STRG_MOD_EN) は、実物のアークの CFG0 が
  `04 00 00 1C` でメーカーが AUTH0 だけ変えていたことからも裏付けられる。
  VCTID = `05` は VCSL コマンドが返すだけの値で動作には影響しない。
- PWD はタグの UID から計算するので、このアプリで書いたタグはそのまま解除できる。
  そのためには種類が必要なので、**ページ 3 で YW3/YW4 を判別してから** PWD を計算する
  (書き込みでは元ダンプから種類が分かっている)。
- 検証: ページ 4〜39 と設定ページを読み戻して照合し、最後に工場出荷の PWD (`FF FF FF FF`) で
  PWD_AUTH が通ることを確認する (PWD は読み出せず常に 0 が返るため、認証でしか確認できない)。
- **ページ 3 (NDEF の CC) は戻せない**。NTAG21x の CC は WRITE がビット OR で、立てたビットを
  落とせないため。アークの CC は `E1 11 12 0F` で末尾の `0F` が NFC Forum 的に
  「NDEF 書き込み不可」を意味するので、スマホの NFC アプリからは読み取り専用のタグとして
  扱われる。メモリ自体は生の WRITE で書けるので Flipper 等からは普通に書き換えられる。
  (この OR の挙動は NTAG215 への書き込み時に実機で確認済み。工場出荷の CC `E1 10 3E 00` に
  `E1 11 12 0F` を書いた結果が `E1 11 3E 0F` になっていた)
- 実物のメダル / アークに使うと固有の NDEF URL も消えて戻せないため、**確認画面を挟んだ**
  (右ボタン Erase で実行、Back で取り消し)。
- リファクタ: 書き込みと消去で共通の下準備 (種類・UID・ロックビットの確認と保護の解除) を
  `prepare_tag_op()` に、ページ書き込みを `write_raw_page()` に切り出した。
  結果画面も `show_tag_result()` で共用 (`app->op` で文面を切り替え)。
- ビルド確認: `unlshd-094` (API 88.19) で警告ゼロ、APPCHK 通過。実機での消去は未確認。
