# Yo-kai Medal Reader (Flipper Zero)

[日本語 README →](README.md)

A Flipper Zero app (FAP for the Unleashed firmware) that unlocks the password-protected NFC medals / arks
(NTAG213) of the Yo-kai Watch games and saves every page to a `.nfc` file. Saved files can be emulated with the stock NFC app.

| Supports | Game |
|---|---|
| Yo-kai Arks | Yo-kai Watch 4 (Switch) |
| Yo-kai Medals | Yo-kai Watch 3 (3DS) — not tested with a real medal |

Distributed as two files.

| File | What it does | Goes in |
|---|---|---|
| `yokai_medal.fap` | Standalone app: read, clone, change ID | `apps/NFC/` |
| `yokai_medal_parser.fal` | Read-only plugin for the stock NFC app's "Read" | `apps_data/nfc/plugins/` |

## Features

- **Read**: derives the password from the UID, unlocks the tag and saves a full dump (both forms)
- **Clone**: rebuilds a read (or saved) dump with a new random UID (`.fap` only)
- **Change ID**: turns a Yo-kai Watch 4 ark into another ark type (another Yo-kai) (`.fap` only)

## Install

Download what you need from [Releases](https://github.com/mar1mo-41414/flipper-youkai-medal/releases):

- `yokai_medal.fap` → `apps/NFC/` on the SD card
- `yokai_medal_parser.fal` → `apps_data/nfc/plugins/` on the SD card (create it if missing)

Both depend on the firmware API version: use a build that matches your Unleashed version, or build it yourself.

## Usage

### yokai_medal.fap (standalone app)

Apps → NFC → **Yo-kai Medal Reader**, then *Read (auto detect)*, a fixed game, or *Load .nfc file*.
Dumps are saved to `SD:/nfc/yokai/`. On the result screen press **Edit** (right) for *Clone* or *Change ID*
(3-char base-36 ID, e.g. `M88` = Jibanyan; see
[YOKAI_IDS.md](https://github.com/mar1mo-41414/switch-youkaiwatch4-medal/blob/main/docs/YOKAI_IDS.md)).
Yo-kai Watch 4 accepts each UID only once, so clone a new UID every time.

### yokai_medal_parser.fal (NFC app plugin)

Apps → NFC → **Read**. The stock NFC app's own screen shows the UID, password and checksum, and the
dump is saved where regular reads go (`SD:/nfc/`). Auto-detected; no clone/change-ID here (use the `.fap`).

## Build

```bash
python3 -m venv .venv && .venv/bin/pip install ufbt
.venv/bin/ufbt update --index-url=https://up.unleashedflip.com/directory.json

.venv/bin/ufbt            # -> dist/yokai_medal.fap
scripts/build_plugin.sh   # -> dist_plugin/yokai_medal_parser.fal (see below)
```

`yokai_medal_parser.fal` can't be built with ufbt alone — NFC app plugins must be built inside a source
tree that has the parent app's (`nfc`) `application.fam`. `scripts/build_plugin.sh` fetches the full
Unleashed firmware source and builds it with the official `fbt` tool (a few minutes).

How it works: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (Japanese).

## Notes

Intended for backing up and testing medals / arks you own. Not affiliated with LEVEL-5 or Nintendo.
No game data is included.

## License

[MIT](LICENSE)
