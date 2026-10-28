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
| `yokai_medal.fap` | Standalone app: read, clone, change ID, write to a blank tag | `apps/NFC/` |
| `yokai_medal_parser.fal` | Read-only plugin for the stock NFC app's "Read" | `apps_data/nfc/plugins/` |

## Features

- **Read**: derives the password from the UID, unlocks the tag and saves a full dump (both forms)
- **Clone**: rebuilds a read (or saved) dump with a new random UID (`.fap` only)
- **Change ID**: turns a Yo-kai Watch 4 ark into another ark type (another Yo-kai) (`.fap` only)
- **Write**: writes a saved `.nfc` onto a blank NTAG213/215/216, turning it into a real medal / ark (`.fap` only)
- **Erase**: puts a written tag back to blank so it can be used as a plain NTAG again (`.fap` only)

## Install

Download what you need from [Releases](https://github.com/mar1mo-41414/flipper-youkai-medal/releases):

- `yokai_medal.fap` → `apps/NFC/` on the SD card
- `yokai_medal_parser.fal` → `apps_data/nfc/plugins/` on the SD card (create it if missing)

Both depend on the firmware API version: use a build that matches your Unleashed version, or build it yourself.

## Usage

### yokai_medal.fap (standalone app)

Apps → NFC → **Yo-kai Medal Reader**, then *Read (auto detect)*, a fixed game, *Load .nfc file*,
*Write .nfc to blank NTAG* or *Erase NTAG (back to blank)*. Dumps are saved to `SD:/nfc/yokai/`. On the result screen press **Edit**
(right) for *Clone*, *Change ID* (3-char base-36 ID, e.g. `M88` = Jibanyan; see
[YOKAI_IDS.md](https://github.com/mar1mo-41414/switch-youkaiwatch4-medal/blob/main/docs/YOKAI_IDS.md))
or *Write to blank NTAG*.
Yo-kai Watch 4 accepts each UID only once, so clone a new UID (or write another tag) every time.

### Writing to a blank NTAG

Pick a dump with *Write .nfc to blank NTAG* (or *Edit → Write to blank NTAG*) and hold a blank
NTAG213, NTAG215 or NTAG216 against the Flipper.

A tag's UID cannot be changed, so the keys and the password are **recomputed from the UID of the tag
you present** — the UID inside the chosen `.nfc` is not used. One `.nfc` therefore makes any number
of tags, each with its own UID (so Yo-kai Watch 4 keeps accepting them).

Pages 3-39 (NDEF plus the protected data, re-encrypted for that tag), then PWD/PACK, then ACCESS and
finally CFG0 with AUTH0 — the config page goes last, because AUTH0 locks every page above it,
including the password. Afterwards the app re-authenticates with the password it wrote, checks PACK
is `BE EF`, reads every page back and compares, and saves the same dump to `SD:/nfc/yokai/`.

The config, PWD and PACK pages sit at different addresses per tag type, so each type gets its own
(the protected data at pages 3-39 is the same everywhere):

| Type | Pages | CFG0 | ACCESS | PWD | PACK | Dynamic lock |
|---|---|---|---|---|---|---|
| NTAG213 | 45 | 41 | 42 | 43 | 44 | 40 |
| NTAG215 | 135 | 131 | 132 | 133 | 134 | 130 |
| NTAG216 | 231 | 227 | 228 | 229 | 230 | 226 |

- Needs a **blank tag** with no lock bits and no password. Any other type (NTAG210/212, Ultralight,
  magic tags) is rejected with `Wrong tag type`.
- **NTAG215 is verified on real hardware**: an ark written to a blank NTAG215 was accepted by
  Yo-kai Watch 4 (Switch) as an ordinary ark, with nothing rejected. Real medals / arks are NTAG213,
  but NTAG215 works just as well. NTAG216 has the same layout as NTAG215 so it should behave the
  same, but it is untested (the result screen says so).
- Pages 40+ stay blank on NTAG215/216 (on NTAG213 that is where the config pages live). The game
  does read that range (it reads 0x00-0x2A), but the hardware test confirms it does not look at it.
- Pages 0-2 (UID / BCC / static lock bytes) and the dynamic lock page are left alone, and `CFGLCK`
  stays off, so the same tag can be rewritten as often as you like.
- Page 3 (the NDEF CC) is bitwise-OR on write for some tags, so the app writes (and expects)
  "current value OR dump value". A factory NTAG215 (CC `E1 10 3E 00`) keeps `0x3E` as its size byte;
  neither the type detection (first byte `E1`) nor the game reads NDEF, so it does not matter.
- The NXP signature of the blank tag is kept as is. Yo-kai Watch 4 never sends READ_SIG, so it is
  not checked.

### Erasing a written tag

*Erase NTAG (back to blank)* asks for confirmation; press **Erase** (right) and hold the tag.
Verified on real hardware with an NTAG215.

| Restored | Value |
|---|---|
| Pages 4-39 | all zeros |
| CFG0 | `04 00 00 FF` (AUTH0 = FF, no protection; `04` is the STRG_MOD_EN default) |
| ACCESS | `00` (no PROT, no CFGLCK) |
| PWD / PACK | `FF FF FF FF` / `00 00` (factory) |

The password is derived from the tag's UID, so **any tag written by this app unlocks itself** (the
game is detected from page 3). A tag with some other password stops with
`Password does not match this UID`.

Protection is removed first, so losing the tag mid-erase can never leave it wiped *and* locked
(the reverse of the write order). Afterwards pages 4-39 and the config are read back and compared,
and the factory password `FF FF FF FF` is used for one final PWD_AUTH check.

- **Page 3 (the NDEF CC) cannot be restored.** The CC is bitwise-OR on write, so bits that are set
  stay set. An ark's CC is `E1 11 12 0F`, and the trailing `0F` means "NDEF write access denied",
  so **phone NFC apps will treat the tag as read-only**. The memory itself is still writable with
  raw WRITE commands, so the Flipper and other low-level tools can rewrite it freely.
- Because the CC stays, an erased tag still *looks* like an ark if you only read page 3. Detection
  therefore also requires the NDEF area (page 4) to be present: an erased tag shows up as
  `Not a medal/ark?` in the `.fap`, and the `.fal` declines it so the stock NFC app reads it as a
  plain NTAG.
- The game will not accept it: the password is back to factory, so PWD_AUTH no longer answers
  `BE EF`, and the protected pages are zeros.
- Lock bytes are never touched, so nothing is locked.
- Only the written range (pages 4-39) is cleared; pages 40+ on NTAG215/216 are left as they were.
- **On a real medal / ark this also erases its NDEF URL, which is unique and cannot be restored.**
  Read a dump first if you want it back.

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
