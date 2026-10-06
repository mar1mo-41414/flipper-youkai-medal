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
# 環境変数:
#   UNLEASHED_REF   取得する Unleashed のタグ/ブランチ (既定: unlshd-093。.fap と同じ SDK バージョンに合わせる)
#   UNLEASHED_SRC   既にローカルにある Unleashed のソースツリーを使う場合、そのパス (省略時は shallow clone)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-$REPO_ROOT/dist_plugin}"
REF="${UNLEASHED_REF:-unlshd-093}"
PLUGIN_SUBDIR="applications/main/nfc/plugins/supported_cards"

workdir=""
cleanup() { [ -n "$workdir" ] && rm -rf "$workdir"; }
trap cleanup EXIT

if [ -n "${UNLEASHED_SRC:-}" ]; then
    fw="$UNLEASHED_SRC"
else
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

mkdir -p "$OUT_DIR"
cp "$fw/build/f7-firmware-D/.extapps/yokai_medal_parser.fal" "$OUT_DIR/"
echo "-> $OUT_DIR/yokai_medal_parser.fal" >&2
