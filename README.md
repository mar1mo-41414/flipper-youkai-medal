# Yo-kai Medal Reader (Flipper Zero)

[English README →](README-EN.md)

妖怪ウォッチの NFC メダル / アーク (NTAG213) のパスワード保護を解除して、全ページを `.nfc` に保存する
Flipper Zero アプリ (Unleashed ファーム向け FAP)。保存したファイルは標準の NFC アプリでそのままエミュレートできる。

| 対応 | ゲーム |
|---|---|
| 妖怪アーク | 妖怪ウォッチ4 (Switch) |
| 妖怪メダル | 妖怪ウォッチ3 (3DS) ※ 実物では未検証 |

## できること

- **読み取り**: メダル / アークをかざすと、UID からパスワードを計算してロックを解除し、全ページを保存する
- **複製 (Clone)**: 読み取った (または保存済みの) ダンプを、新しい乱数 UID で作り直す
- **種類の変更 (Change ID)**: 妖怪ウォッチ4 のアークを、別の種類 (別の妖怪) のアークとして作り直す

## インストール

[Releases](https://github.com/mar1mo-41414/flipper-youkai-medal/releases) から `yokai_medal.fap` をダウンロードして、
SD カードの `apps/NFC/` に置く。

FAP はファームの API バージョンに依存する。Releases のビルドに書いてある Unleashed のバージョンと、
Flipper のファームのバージョンを合わせること。違う場合は自分でビルドする (下記)。

## 使い方

Apps → NFC → **Yo-kai Medal Reader**

| メニュー | 内容 |
|---|---|
| Read (auto detect) | メダル / アークを読む (種類は自動判別) |
| Read YW3 medal (3DS) / Read YW4 ark (Switch) | 種類を固定して読む |
| Load .nfc file | 保存済みのダンプを開く |
| About | 説明 |

読み取りに成功すると、UID・パスワード・復号したデータのチェックサムが表示され、
`SD:/nfc/yokai/YW4_<ID>_<UID>.nfc` (3DS は `YW3_medal_<UID>.nfc`) に保存される。

結果画面で右ボタン **Edit** を押すと:

- **Clone (new random UID)**: 中身はそのままで、UID だけ新しくする
- **Change ID + new UID** (妖怪ウォッチ4 のみ): 3 文字の ID (例 `M88` = ジバニャン) を入力して、その種類のアークを作る。
  ID の一覧は [switch-youkaiwatch4-medal の docs/YOKAI_IDS.md](https://github.com/mar1mo-41414/switch-youkaiwatch4-medal/blob/main/docs/YOKAI_IDS.md)

妖怪ウォッチ4 は同じ UID のアークを 1 回しか読み込まない (「このアークは一度しか読み取れません」)。
使うたびに Clone で新しい UID のものを作ること。

## ビルド

```bash
python3 -m venv .venv && .venv/bin/pip install ufbt
.venv/bin/ufbt update --index-url=https://up.unleashedflip.com/directory.json   # Unleashed の SDK
.venv/bin/ufbt            # -> dist/yokai_medal.fap
.venv/bin/ufbt launch     # USB 接続した Flipper にインストールして起動
```

仕組み (パスワードの算出、判別方法、テスト) は [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 注意

- 自分が持っているメダル / アークのバックアップや動作確認を目的としたものです。
- 株式会社レベルファイブ・任天堂とは関係ありません。
- ゲームのデータや ROM は含んでいません。

## ライセンス

[MIT](LICENSE)
