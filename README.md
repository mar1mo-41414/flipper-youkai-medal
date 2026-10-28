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
| `yokai_medal.fap` | 読み取り・複製・種類の変更・空タグへの書き込み・消去ができる独立アプリ | `apps/NFC/` |
| `yokai_medal_parser.fal` | 読み取り専用。標準の NFC アプリの「Read」にそのまま乗る | `apps_data/nfc/plugins/` |

## できること

- **読み取り**: メダル / アークをかざすと、UID からパスワードを計算してロックを解除し、全ページを保存する (両方の形式で可能)
- **複製 (Clone)**: 読み取った (または保存済みの) ダンプを、新しい乱数 UID で作り直す (`.fap` のみ)
- **種類の変更 (Change ID)**: 妖怪ウォッチ4 のアークを、別の種類 (別の妖怪) のアークとして作り直す (`.fap` のみ)
- **新規作成 (Create)**: 元ダンプなしで、3 文字の種別 ID だけから妖怪ウォッチ4 のアークのダンプを作る (`.fap` のみ)
- **書き込み (Write)**: 保存した `.nfc` を空の NTAG (213/215/216) に書き込んで、実物として使えるメダル / アークにする (`.fap` のみ)
- **消去 (Erase)**: 書き込んだタグを空の状態に戻して、普通の NTAG として使えるようにする (`.fap` のみ)

パスワードの算出方法、タグのページ構成、書き込み・消去の手順などの仕組みは
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) にまとめてある。

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
| Create new ark (YW4) | 種別 ID を入力して、元ダンプなしでアークのダンプを作る |
| Write .nfc to blank NTAG | 保存済みのダンプを空のタグに書き込む |
| Erase NTAG (back to blank) | 書き込んだタグを空の状態に戻す |
| About | 説明 |

読み取りに成功すると、UID・パスワード・復号したデータのチェックサムが表示され、
`SD:/nfc/yokai/YW4_<ID>_<UID>.nfc` (3DS は `YW3_medal_<UID>.nfc`) に保存される。

結果画面で右ボタン **Edit** を押すと:

- **Clone (new random UID)**: 中身はそのままで、UID だけ新しくする
- **Change ID + new UID** (妖怪ウォッチ4 のみ): 3 文字の ID (例 `M88` = ジバニャン) を入力して、その種類のアークを作る。
  ID の一覧は [docs/YOUKAI_IDs.md](docs/YOUKAI_IDs.md)
- **Write to blank NTAG**: 今表示している中身を、かざした空のタグに書き込む

妖怪ウォッチ4 は同じ UID のアークを 1 回しか読み込まない (「このアークは一度しか読み取れません」)。
使うたびに Clone で新しい UID のものを作る (または別のタグに書き込む) こと。

### 元ダンプなしでアークを作る (Create)

**Create new ark (YW4)** を選んで 3 文字の種別 ID (例 `M88` = ジバニャン) を入力すると、
**実物のアークもダンプも無しで**妖怪ウォッチ4 のアークのダンプ 1 枚を作る。
そのまま `SD:/nfc/yokai/` に保存され、標準の NFC アプリでエミュレートしたり、
Edit → Write to blank NTAG で空のタグに焼いたりできる。

入れる ID の一覧は [docs/YOUKAI_IDs.md](docs/YOUKAI_IDs.md)。

ゲームがアークの種類を決めているのは暗号化されたデータの中の種別 ID だけで、そこは UID から
算出できるので、1 から組み立てられる。

- **NXP の署名は作れない** (タグの UID に対する NXP の秘密鍵の署名なので偽造不可)。0 のままになる。
  ゲームは署名を読まないので影響しない。物理タグに焼く場合は、そのタグ自身の本物の署名が残る。
- NDEF の URL は `https://www.ywg.jp/<ID>` を合成して入れている。実物のトークンは種別 ID のあとに
  規則不明の文字列が続くが、ゲームは URL を読まないので影響しない。
- **実機で確認済み**。0 から作ったアークが、Flipper Zero でのエミュレートでも、空の NTAG に
  焼いた物理タグでも、妖怪ウォッチ4 (Switch) にそのまま読み込まれた。
  署名が 0 でも NDEF の URL が合成でも問題にならないことが裏付けられた。

### 空の NTAG への書き込み

> **⚠ 書き込んだタグは完全には元に戻せない**
>
> **Erase NTAG** でパスワード・保護・データは工場出荷の状態に戻せるが、NDEF の管理領域だけは
> タグの仕様上戻せない。そのため消去後も、**スマホの NFC アプリからは読み取り専用のタグとして
> 扱われ、NDEF を書き込めない**。Flipper や低レベルのコマンドを使うツールからは普通に書き換えられる。
>
> 普通の NFC タグとして使う予定があるタグには書き込まないこと。
> 理由は [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#戻せないもの) を参照。

**Write .nfc to blank NTAG** でダンプを選ぶか、結果画面の **Edit → Write to blank NTAG** を選び、
空のタグをかざす。成功すると、書き込んだ内容と同じダンプが `SD:/nfc/yokai/` にも保存される。

タグの UID は書き換えられないので、**かざしたタグの UID でパスワードを計算し直してから**書き込む
(選んだ `.nfc` の UID は使われない)。つまり、1 枚の `.nfc` から何枚でも作れて、どれも別の UID になる
(= 妖怪ウォッチ4 で何度でも読み込める)。

- 必要なのは **空の NTAG213 / NTAG215 / NTAG216** (ロックもパスワードもかかっていないもの)。
  これ以外の種類 (NTAG210/212、Ultralight、マジックタグなど) は `Wrong tag type` で止まる。
- **NTAG215 は実機で確認済み**。空の NTAG215 に書いたアークが、妖怪ウォッチ4 (Switch) で
  何も弾かれずそのままアークとして読み込まれた。実物のメダル / アークは NTAG213 だが、
  NTAG215 でも問題なく通る。NTAG216 は未検証 (書き込むと結果画面に注記が出る)。
- ロックはかけないので、**同じタグに何度でも書き直せる**。このアプリで書いたタグへの上書きもできる。
  別のパスワードがかかっているタグは `Tag already has another password` で止まる。
- 書き込んだあと、PACK の確認と全ページの読み戻し照合まで行う。
  途中で失敗したときは、どのページで止まったかが表示される。

### 書き込んだタグを空に戻す (Erase)

**Erase NTAG (back to blank)** を選ぶと確認画面が出る。右ボタン **Erase** を押してタグをかざすと、
そのタグを普通の NTAG として使える状態に戻す (NTAG215 で実機確認済み)。

| | 消去後 |
|---|---|
| データ | 書き込んだ範囲はすべて 0 |
| パスワード・保護 | 工場出荷の状態 (保護なし) |
| NDEF の管理領域 | **戻らない** (タグの仕様上、戻せない) |

パスワードはタグの UID から計算するので、**このアプリで書いたタグならそのまま消去できる**。
別のパスワードがかかっているタグは `Password does not match this UID` で止まる。

- 消去したタグは、`.fap` では `Not a medal/ark?`、`.fal` では対象外として標準の NTAG として読まれる。
  ゲームも読み込まなくなる。
- **実物のメダル / アークに使うと、アークごとに固有の NDEF の URL も消えて戻せない。**
  消す前に Read でダンプを取っておくこと。

### yokai_medal_parser.fal (NFC アプリのプラグイン)

Apps → NFC → **Read**。メダル / アークをかざすと、標準の NFC アプリの画面のまま UID・パスワード・
チェックサムが表示され、通常の読み取りと同じ場所 (`SD:/nfc/`) に保存される。種類は自動判別。
複製・種類の変更・書き込み・消去はできない (`.fap` を使う)。

## ビルド

```bash
python3 -m venv .venv && .venv/bin/pip install ufbt
.venv/bin/ufbt update --index-url=https://up.unleashedflip.com/directory.json   # Unleashed の SDK

.venv/bin/ufbt            # -> dist/yokai_medal.fap
.venv/bin/ufbt launch     # USB 接続した Flipper にインストールして起動

scripts/build_plugin.sh   # -> dist_plugin/yokai_medal_parser.fal (数分かかる)
```

`yokai_medal_parser.fal` は `.fap` と違い、ufbt (SDK だけ) ではビルドできないため、
`scripts/build_plugin.sh` が Unleashed ファームウェア全体を取得して公式のビルドツール (`fbt`) で
ビルドする。既定では Unleashed の最新リリースを使う (`UNLEASHED_REF` で変更できる)。
理由は [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) の「ビルドが `.fap` と別系統な理由」を参照。

## 注意

- 自分が持っているメダル / アークのバックアップや動作確認を目的としたものです。
- 株式会社レベルファイブ・任天堂とは関係ありません。
- ゲームのデータや ROM は含んでいません。

## ライセンス

[MIT](LICENSE)
