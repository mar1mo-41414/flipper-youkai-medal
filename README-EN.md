# Yo-kai Medal Reader (Flipper Zero)

[日本語 README →](README.md)

A Flipper Zero app (FAP for the Unleashed firmware) that unlocks the password-protected NFC medals / arks
(NTAG213) of the Yo-kai Watch games and saves every page to a `.nfc` file. Saved files can be emulated with the stock NFC app.

| Supports | Game |
|---|---|
| Yo-kai Arks | Yo-kai Watch 4 (Switch) |
| Yo-kai Medals | Yo-kai Watch 3 (3DS) — not tested with a real medal |

## Features

- **Read**: derives the password from the UID, unlocks the tag and saves a full dump
- **Clone**: rebuilds a read (or saved) dump with a new random UID
- **Change ID**: turns a Yo-kai Watch 4 ark into another ark type (another Yo-kai)

## Install

Download `yokai_medal.fap` from [Releases](https://github.com/mar1mo-41414/flipper-youkai-medal/releases) and put it in
`apps/NFC/` on the SD card. The FAP depends on the firmware API version: use a build that matches your Unleashed version,
or build it yourself.

## Usage

Apps → NFC → **Yo-kai Medal Reader**, then *Read (auto detect)*, a fixed game, or *Load .nfc file*.
Dumps are saved to `SD:/nfc/yokai/`. On the result screen press **Edit** (right) for *Clone* or *Change ID*
(3-char base-36 ID, e.g. `M88` = Jibanyan; see
[YOKAI_IDS.md](https://github.com/mar1mo-41414/switch-youkaiwatch4-medal/blob/main/docs/YOKAI_IDS.md)).
Yo-kai Watch 4 accepts each UID only once, so clone a new UID every time.

## Build

```bash
python3 -m venv .venv && .venv/bin/pip install ufbt
.venv/bin/ufbt update --index-url=https://up.unleashedflip.com/directory.json
.venv/bin/ufbt
```

How it works: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (Japanese).

## Notes

Intended for backing up and testing medals / arks you own. Not affiliated with LEVEL-5 or Nintendo.
No game data is included.

## License

[MIT](LICENSE)
