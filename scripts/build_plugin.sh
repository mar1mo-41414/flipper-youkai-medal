#!/usr/bin/env bash
# NFC アプリの supported_cards プラグイン (yokai_medal_parser.fal) をビルドする。
#
# .fal は親アプリ (nfc) の application.fam があるソースツリー内でしかビルドできないため、
# ufbt (SDK だけ) では作れない。Unleashed ファームウェア全体を取得し、
# applications/main/nfc/plugins/supported_cards/ にこのリポジトリのプラグインを仮配置して、
# 公式のビルドツール (fbt) でビルドする。
#
# usage: scripts/build_plugin.sh [出力先ディレクトリ (既定: dist_plugin)]
#
# 既定では「Unleashed の最新リリース」を使う。バージョンは .fap 側 (ufbt の
# sdk-channel: release) と同じ directory.json から引くので、.fap と .fal が
# 別バージョン向けになることがない。
#
# 環境変数:
#   UNLEASHED_REF         取得する Unleashed のタグ/ブランチ (省略時は最新リリースを自動で解決)
#   UNLEASHED_SRC         既にローカルにある Unleashed のソースツリーを使う場合、そのパス (省略時は shallow clone)
#   UNLEASHED_INDEX_URL   バージョン解決に使う directory.json の URL
#
# 解決したバージョンは 出力先/unleashed_ref.txt にも書き出す (CI が artifact 名に使う)。
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-$REPO_ROOT/dist_plugin}"
INDEX_URL="${UNLEASHED_INDEX_URL:-https://up.unleashedflip.com/directory.json}"
PLUGIN_SUBDIR="applications/main/nfc/plugins/supported_cards"

REF="${UNLEASHED_REF:-}"

# 最新リリースのバージョンを directory.json から取る。release チャンネルのバージョン文字列は
# そのままファームの git タグ名になっている (例: unlshd-094)
resolve_latest_ref() {
    curl -fsSL --max-time 60 "$INDEX_URL" | python3 -c '
import json, sys
channels = json.load(sys.stdin)["channels"]
print(next(c for c in channels if c["id"] == "release")["versions"][0]["version"])
'
}

workdir=""
# UNLEASHED_SRC 指定時は workdir が空になる。[ -n "" ] の 1 が EXIT トラップの戻り値として
# スクリプトの終了コードになってしまうので、必ず 0 を返す
cleanup() {
    [ -n "$workdir" ] && rm -rf "$workdir"
    return 0
}
trap cleanup EXIT

if [ -n "${UNLEASHED_SRC:-}" ]; then
    # 手元のツリーを使う。バージョンはそのツリー自身から取る (指定された REF は使わない)
    fw="$UNLEASHED_SRC"
    [ -n "$REF" ] || REF="$(git -C "$fw" describe --tags --always 2>/dev/null || echo local)"
else
    if [ -z "$REF" ]; then
        # 取れなかったら古い値で代用せずに止める
        echo "最新リリースのバージョンを取得中 ($INDEX_URL)..." >&2
        REF="$(resolve_latest_ref)" || REF=""
        [ -n "$REF" ] || {
            echo "最新リリースのバージョンを解決できなかった。UNLEASHED_REF で指定して。" >&2
            exit 1
        }
        echo "最新リリース: $REF" >&2
    fi
    workdir="$(mktemp -d)"
    fw="$workdir/unleashed-firmware"
    echo "Unleashed ($REF) を取得中..." >&2
    git clone --depth 1 --branch "$REF" --recurse-submodules --shallow-submodules \
        https://github.com/DarkFlippers/unleashed-firmware.git "$fw"
fi

plugin_dir="$fw/$PLUGIN_SUBDIR"
[ -d "$plugin_dir" ] || { echo "見つかりません: $plugin_dir (UNLEASHED_SRC / UNLEASHED_REF を確認)" >&2; exit 1; }

cp "$REPO_ROOT/nfc_plugin/yokai_medal_parser.c" "$plugin_dir/"
cp "$REPO_ROOT/nfc_plugin/nfc_supported_card_plugin_abi.h" "$plugin_dir/"
cp "$REPO_ROOT/ym_crypto.h" "$plugin_dir/"
cp "$REPO_ROOT/ym_crypto.c" "$plugin_dir/yokai_medal_crypto.c"

fam="$fw/applications/main/nfc/application.fam"
if ! grep -q 'appid="yokai_medal_parser"' "$fam"; then
    cat >> "$fam" <<'EOF'

App(
    appid="yokai_medal_parser",
    apptype=FlipperAppType.PLUGIN,
    entry_point="yokai_medal_parser_ep",
    targets=["f7"],
    requires=["nfc"],
    sources=[
        "plugins/supported_cards/yokai_medal_parser.c",
        "plugins/supported_cards/yokai_medal_crypto.c",
    ],
    fap_exclude_libs=["gcc"],
)
EOF
fi

echo "fbt でビルド中..." >&2
( cd "$fw" && ./fbt fap_yokai_medal_parser )

# 出力先のディレクトリ名はファームのビルド設定で変わる (unlshd-093 は f7-firmware-D、
# unlshd-094 は f7-firmware-DC) ので、決め打ちせずに探す
fal="$(ls -t "$fw"/build/f7-firmware-*/.extapps/yokai_medal_parser.fal 2>/dev/null | head -1)"
[ -n "$fal" ] || {
    echo "ビルド結果が見つかりません: $fw/build/f7-firmware-*/.extapps/yokai_medal_parser.fal" >&2
    exit 1
}

mkdir -p "$OUT_DIR"
cp "$fal" "$OUT_DIR/"
printf '%s\n' "$REF" > "$OUT_DIR/unleashed_ref.txt"
echo "-> $OUT_DIR/yokai_medal_parser.fal ($REF, $(basename "$(dirname "$(dirname "$fal")")"))" >&2
