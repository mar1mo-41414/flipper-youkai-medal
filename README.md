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
- **書き込み (Write)**: 保存した `.nfc` を空の NTAG213 に書き込んで、実物として使えるメダル / アークにする (`.fap` のみ)

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
| Write .nfc to blank NTAG | 保存済みのダンプを空の NTAG213 に書き込む |
| About | 説明 |

読み取りに成功すると、UID・パスワード・復号したデータのチェックサムが表示され、
`SD:/nfc/yokai/YW4_<ID>_<UID>.nfc` (3DS は `YW3_medal_<UID>.nfc`) に保存される。

結果画面で右ボタン **Edit** を押すと:

- **Clone (new random UID)**: 中身はそのままで、UID だけ新しくする
- **Change ID + new UID** (妖怪ウォッチ4 のみ): 3 文字の ID (例 `M88` = ジバニャン) を入力して、その種類のアークを作る。
  ID の一覧は [switch-youkaiwatch4-medal の docs/YOKAI_IDS.md](https://github.com/mar1mo-41414/switch-youkaiwatch4-medal/blob/main/docs/YOKAI_IDS.md)
- **Write to blank NTAG**: 今表示している中身を、かざした空の NTAG213 に書き込む

妖怪ウォッチ4 は同じ UID のアークを 1 回しか読み込まない (「このアークは一度しか読み取れません」)。
使うたびに Clone で新しい UID のものを作る (または別のタグに書き込む) こと。

### 空の NTAG213 への書き込み

**Write .nfc to blank NTAG** でダンプを選ぶか、結果画面の **Edit → Write to blank NTAG** を選び、
空の NTAG213 をかざす。

タグの UID は書き換えられないので、**かざしたタグの UID で鍵とパスワードを計算し直してから**書き込む
(選んだ `.nfc` の UID は使われない)。つまり、1 枚の `.nfc` から何枚でも作れて、どれも別の UID になる
(= 妖怪ウォッチ4 で何度でも読み込める)。

書き込む内容と順番:

| ページ | 内容 |
|---|---|
| 3〜39 | NDEF の CC・URL と、そのタグの UID で暗号化し直した保護データ |
| 43 / 44 | PWD (UID から算出) / PACK (`BE EF`) |
| 42 | ACCESS (読み出しも保護する `PROT`。設定を永久ロックする `CFGLCK` は立てない) |
| 41 | MIRROR / AUTH0 (保護の開始ページ。**最後に書く**) |

AUTH0 を先に書くと以降のページが保護されて PWD も書けなくなるので、設定は最後。
書き終わったら、書いた PWD で認証し直して PACK (`BE EF`) を確認し、全ページを読み戻して照合する。
成功すると、書き込んだ内容と同じダンプが `SD:/nfc/yokai/` にも保存される。

- 必要なのは**空の NTAG213** (ロックもパスワードもかかっていないもの)。NTAG215/216 や
  マジックタグではページ構成が違うため受け付けない。
- ページ 0〜2 (UID / BCC / 静的ロックバイト) とページ 40 (動的ロックバイト) は書き換えない。
  ロックをかけないので、同じタグに何度でも書き直せる。
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
