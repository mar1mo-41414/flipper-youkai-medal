# Yo-kai Medal Reader (Flipper Zero)

[English README →](README-EN.md)

妖怪ウォッチの NFC メダル / アーク (NTAG213) のパスワード保護を解除して、全ページを `.nfc` に保存する
Flipper Zero アプリ (Unleashed ファーム向け FAP)。保存したファイルは標準の NFC アプリでそのままエミュレートできる。

| 対応 | ゲーム |
|---|---|
| 妖怪アーク | 妖怪ウォッチ4 (Switch) |
| 妖怪メダル | 妖怪ウォッチ3 (3DS) ※ 実物では未検証 |

2 つの形で配布している。

| 形式 | 中身 | 置き場所 |
|---|---|---|
| `yokai_medal.fap` | 読み取り・複製・種類の変更・空タグへの書き込みができる独立アプリ | `apps/NFC/` |
| `yokai_medal_parser.fal` | 読み取り専用。標準の NFC アプリの「Read」にそのまま乗る | `apps_data/nfc/plugins/` |

## できること

- **読み取り**: メダル / アークをかざすと、UID からパスワードを計算してロックを解除し、全ページを保存する (両方の形式で可能)
- **複製 (Clone)**: 読み取った (または保存済みの) ダンプを、新しい乱数 UID で作り直す (`.fap` のみ)
- **種類の変更 (Change ID)**: 妖怪ウォッチ4 のアークを、別の種類 (別の妖怪) のアークとして作り直す (`.fap` のみ)
- **書き込み (Write)**: 保存した `.nfc` を空の NTAG (213/215/216) に書き込んで、実物として使えるメダル / アークにする (`.fap` のみ)

## インストール

[Releases](https://github.com/mar1mo-41414/flipper-youkai-medal/releases) から必要なファイルをダウンロードする。

- `yokai_medal.fap` → SD カードの `apps/NFC/`
- `yokai_medal_parser.fal` → SD カードの `apps_data/nfc/plugins/` (無ければ作る)

どちらもファームの API バージョンに依存する。Releases のビルドに書いてある Unleashed のバージョンと、
Flipper のファームのバージョンを合わせること。違う場合は自分でビルドする (下記)。

## 使い方

### yokai_medal.fap (独立アプリ)

Apps → NFC → **Yo-kai Medal Reader**

| メニュー | 内容 |
|---|---|
| Read (auto detect) | メダル / アークを読む (種類は自動判別) |
| Read YW3 medal (3DS) / Read YW4 ark (Switch) | 種類を固定して読む |
| Load .nfc file | 保存済みのダンプを開く |
| Write .nfc to blank NTAG | 保存済みのダンプを空のタグに書き込む |
| About | 説明 |

読み取りに成功すると、UID・パスワード・復号したデータのチェックサムが表示され、
`SD:/nfc/yokai/YW4_<ID>_<UID>.nfc` (3DS は `YW3_medal_<UID>.nfc`) に保存される。

結果画面で右ボタン **Edit** を押すと:

- **Clone (new random UID)**: 中身はそのままで、UID だけ新しくする
- **Change ID + new UID** (妖怪ウォッチ4 のみ): 3 文字の ID (例 `M88` = ジバニャン) を入力して、その種類のアークを作る。
  ID の一覧は [switch-youkaiwatch4-medal の docs/YOKAI_IDS.md](https://github.com/mar1mo-41414/switch-youkaiwatch4-medal/blob/main/docs/YOKAI_IDS.md)
- **Write to blank NTAG**: 今表示している中身を、かざした空のタグに書き込む

妖怪ウォッチ4 は同じ UID のアークを 1 回しか読み込まない (「このアークは一度しか読み取れません」)。
使うたびに Clone で新しい UID のものを作る (または別のタグに書き込む) こと。

### 空の NTAG への書き込み

**Write .nfc to blank NTAG** でダンプを選ぶか、結果画面の **Edit → Write to blank NTAG** を選び、
空のタグをかざす。NTAG213 / NTAG215 / NTAG216 に対応している。

タグの UID は書き換えられないので、**かざしたタグの UID で鍵とパスワードを計算し直してから**書き込む
(選んだ `.nfc` の UID は使われない)。つまり、1 枚の `.nfc` から何枚でも作れて、どれも別の UID になる
(= 妖怪ウォッチ4 で何度でも読み込める)。

書き込む内容と順番:

| ページ | 内容 |
|---|---|
| 3〜39 | NDEF の CC・URL と、そのタグの UID で暗号化し直した保護データ |
| PWD / PACK | PWD (UID から算出) / PACK (`BE EF`) |
| ACCESS | 読み出しも保護する `PROT`。設定を永久ロックする `CFGLCK` は立てない |
| CFG0 | MIRROR / AUTH0 (保護の開始ページ。**最後に書く**) |

AUTH0 を先に書くと以降のページが保護されて PWD も書けなくなるので、設定は最後。
書き終わったら、書いた PWD で認証し直して PACK (`BE EF`) を確認し、全ページを読み戻して照合する。
成功すると、書き込んだ内容と同じダンプが `SD:/nfc/yokai/` にも保存される。

設定ページ・PWD・PACK の位置はタグの種類で違うので、種類ごとに正しいページへ書く
(保護データが入るページ 3〜39 は、どの種類でも同じ位置):

| 種類 | 総ページ | CFG0 | ACCESS | PWD | PACK | 動的ロック |
|---|---|---|---|---|---|---|
| NTAG213 | 45 | 41 | 42 | 43 | 44 | 40 |
| NTAG215 | 135 | 131 | 132 | 133 | 134 | 130 |
| NTAG216 | 231 | 227 | 228 | 229 | 230 | 226 |

- 必要なのは**空のタグ** (ロックもパスワードもかかっていないもの)。これ以外の種類
  (NTAG210/212、Ultralight、マジックタグなど) は `Wrong tag type` で止まる。
- **NTAG215 は実機で確認済み**。空の NTAG215 に書いたアークが、妖怪ウォッチ4 (Switch) で
  何も弾かれずそのままアークとして読み込まれた。実物のメダル / アークは NTAG213 だが、
  NTAG215 でも問題なく通る。
  NTAG216 はページ構成が NTAG215 と同じなので同様に動くはずだが未検証 (結果画面に注記が出る)。
- NTAG215/216 ではメダルが使わないページ 40 以降は空のまま残る (NTAG213 ではここが設定ページ)。
  ゲームはページ 0x00〜0x2A を読むのでこの領域も読んではいるが、参照していないことが
  実機で裏付けられた。
- ページ 0〜2 (UID / BCC / 静的ロックバイト) と動的ロックバイトのページは書き換えない。
  ロックをかけないので、同じタグに何度でも書き直せる。
- ページ 3 (NDEF の CC) は WRITE がビット OR になるタグがあるため、「今の値 + ダンプの値」を
  書き込む値 = 期待値として扱う。工場出荷の NTAG215 (CC = `E1 10 3E 00`) ではサイズのバイトが
  `0x3E` のまま残るが、種類の判別 (先頭が `E1`) もゲームの動作も NDEF を見ないので問題にならない。
- このアプリで書いたタグは、PWD が UID から算出できるので、そのまま上書きできる。
  別のパスワードがかかっているタグは `Tag already has another password` で止まる。
- NXP の署名 (READ_SIG) は空タグのものがそのまま残る。妖怪ウォッチ4 は署名を確認しない
  (READ_SIG を送っていないことを解析で確認済み)。

### yokai_medal_parser.fal (NFC アプリのプラグイン)

Apps → NFC → **Read**。メダル / アークをかざすと、標準の NFC アプリの画面のまま UID・パスワード・
チェックサムが表示され、通常の読み取りと同じ場所 (`SD:/nfc/`) に保存される。種類は自動判別。
複製や種類の変更はできない (`.fap` を使う)。

## ビルド

```bash
python3 -m venv .venv && .venv/bin/pip install ufbt
.venv/bin/ufbt update --index-url=https://up.unleashedflip.com/directory.json   # Unleashed の SDK

.venv/bin/ufbt            # -> dist/yokai_medal.fap
.venv/bin/ufbt launch     # USB 接続した Flipper にインストールして起動

scripts/build_plugin.sh   # -> dist_plugin/yokai_medal_parser.fal (下記参照)
```

`yokai_medal_parser.fal` は `.fap` と違い、ufbt (SDK だけ) ではビルドできない。NFC アプリの
supported_cards プラグインは、親アプリ (`nfc`) の `application.fam` があるソースツリー内でしか
ビルドできないため、`scripts/build_plugin.sh` が Unleashed ファームウェア全体を取得して、
公式のビルドツール (`fbt`) でビルドする (数分かかる)。

仕組み (パスワードの算出、判別方法、テスト) は [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 注意

- 自分が持っているメダル / アークのバックアップや動作確認を目的としたものです。
- 株式会社レベルファイブ・任天堂とは関係ありません。
- ゲームのデータや ROM は含んでいません。

## ライセンス

[MIT](LICENSE)
