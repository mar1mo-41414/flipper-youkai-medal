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
| `yokai_medal.fap` | Standalone app: read, clone, change ID, write to a blank tag, erase | `apps/NFC/` |
| `yokai_medal_parser.fal` | Read-only plugin for the stock NFC app's "Read" | `apps_data/nfc/plugins/` |

## Features

- **Read**: derives the password from the UID, unlocks the tag and saves a full dump (both forms)
- **Clone**: rebuilds a read (or saved) dump with a new random UID (`.fap` only)
- **Change ID**: turns a Yo-kai Watch 4 ark into another ark type (another Yo-kai) (`.fap` only)
- **Create**: builds a Yo-kai Watch 4 ark dump from just a 3-character ark ID, with no source dump (`.fap` only)
- **Write**: writes a saved `.nfc` onto a blank NTAG213/215/216, turning it into a real medal / ark (`.fap` only)
- **Erase**: puts a written tag back to blank so it can be used as a plain NTAG again (`.fap` only)

How it works — password derivation, tag memory layout, the write and erase procedures — is documented
in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (Japanese).

## Install

Download what you need from [Releases](https://github.com/mar1mo-41414/flipper-youkai-medal/releases):

- `yokai_medal.fap` → `apps/NFC/` on the SD card
- `yokai_medal_parser.fal` → `apps_data/nfc/plugins/` on the SD card (create it if missing)

Both depend on the firmware API version: use a build that matches your Unleashed version, or build it yourself.

## Usage

### yokai_medal.fap (standalone app)

Apps → NFC → **Yo-kai Medal Reader**, then *Read (auto detect)*, a fixed game, *Load .nfc file*,
*Create new ark (YW4)*, *Write .nfc to blank NTAG* or *Erase NTAG (back to blank)*. Dumps are saved
to `SD:/nfc/yokai/`.
On the result screen press **Edit** (right) for *Clone*, *Change ID* (3-char base-36 ID, e.g. `M88` =
Jibanyan; see [docs/YOKAI_IDS.md](docs/YOKAI_IDS.md)) or *Write to blank NTAG*.
Yo-kai Watch 4 accepts each UID only once, so clone a new UID (or write another tag) every time.

### Creating an ark without a source dump

*Create new ark (YW4)* asks for a 3-character ark ID (e.g. `M88` = Jibanyan) and builds a complete
Yo-kai Watch 4 ark dump **without any real ark or existing dump**. It is saved to `SD:/nfc/yokai/`,
ready to emulate with the stock NFC app or to write onto a blank tag via *Edit → Write to blank NTAG*.

The list of IDs to enter is in [docs/YOKAI_IDS.md](docs/YOKAI_IDS.md) (Japanese).

The only thing that decides an ark's type is the ID inside its encrypted data, and that is derivable
from the UID, so the whole dump can be assembled from scratch.

- **The NXP signature cannot be forged** (it is signed with NXP's private key over the tag's UID),
  so it stays zero. The game never reads it. When written onto a physical tag, that tag's own
  genuine signature remains.
- The NDEF URL is synthesized as `https://www.ywg.jp/<ID>`. A real token has an unknown string after
  the ID, but the game does not read the URL.
- **Verified on real hardware**: an ark built from scratch was accepted by Yo-kai Watch 4 (Switch)
  both when emulated from a Flipper Zero and when written onto a blank NTAG. The zero signature and
  the synthesized NDEF URL make no difference.

### Writing to a blank NTAG

> **⚠ A tag you write to cannot be fully restored**
>
> *Erase NTAG* puts the password, the protection and the data back to factory, but the NDEF
> management area cannot be reset — that is a property of the tag, not a limitation of this app.
> So even after erasing, **phone NFC apps will treat the tag as read-only and cannot write NDEF to
> it**. The Flipper and other tools that use low-level commands can still rewrite it freely.
>
> Don't write to a tag you plan to use as an ordinary NFC tag later. The reason is explained in
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#戻せないもの) (Japanese).

Pick a dump with *Write .nfc to blank NTAG* (or *Edit → Write to blank NTAG*) and hold the tag
against the Flipper. On success the same dump is also saved to `SD:/nfc/yokai/`.

A tag's UID cannot be changed, so the password is **recomputed from the UID of the tag you present**
— the UID inside the chosen `.nfc` is not used. One `.nfc` therefore makes any number of tags, each
with its own UID (so Yo-kai Watch 4 keeps accepting them).

- Needs a **blank NTAG213, NTAG215 or NTAG216** with no lock bits and no password. Any other type
  (NTAG210/212, Ultralight, magic tags) stops with `Wrong tag type`.
- **NTAG215 is verified on real hardware**: an ark written to a blank NTAG215 was accepted by
  Yo-kai Watch 4 (Switch) as an ordinary ark, with nothing rejected. Real medals / arks are NTAG213,
  but NTAG215 works just as well. NTAG216 is untested (the result screen says so).
- Nothing is locked, so **the same tag can be rewritten as often as you like**, including over a tag
  this app wrote before. A tag with some other password stops with `Tag already has another password`.
- After writing, the PACK is checked and every page is read back and compared. If something fails,
  the screen says which page it stopped at.

### Erasing a written tag

*Erase NTAG (back to blank)* asks for confirmation; press **Erase** (right) and hold the tag. It is
then usable as a plain NTAG again (verified on real hardware with an NTAG215).

| | After erasing |
|---|---|
| Data | the written range is all zeros |
| Password / protection | back to factory (unprotected) |
| NDEF management area | **not restored** (the tag does not allow it) |

The password is derived from the tag's UID, so **any tag written by this app erases itself**. A tag
with some other password stops with `Password does not match this UID`.

- An erased tag reads as `Not a medal/ark?` in the `.fap`; the `.fal` declines it so the stock NFC
  app reads it as a plain NTAG. The game no longer accepts it either.
- **On a real medal / ark this also erases its NDEF URL, which is unique and cannot be restored.**
  Read a dump first if you want it back.

### yokai_medal_parser.fal (NFC app plugin)

Apps → NFC → **Read**. The stock NFC app's own screen shows the UID, password and checksum, and the
dump is saved where regular reads go (`SD:/nfc/`). Auto-detected; no clone, change-ID, write or erase
here (use the `.fap`).

## Build

```bash
python3 -m venv .venv && .venv/bin/pip install ufbt
.venv/bin/ufbt update --index-url=https://up.unleashedflip.com/directory.json

.venv/bin/ufbt            # -> dist/yokai_medal.fap
scripts/build_plugin.sh   # -> dist_plugin/yokai_medal_parser.fal (takes a few minutes)
```

`yokai_medal_parser.fal` can't be built with ufbt alone, so `scripts/build_plugin.sh` fetches the
full Unleashed firmware source and builds it with the official `fbt` tool. It uses the latest
Unleashed release by default (override with `UNLEASHED_REF`).

## Notes

Intended for backing up and testing medals / arks you own. Not affiliated with LEVEL-5 or Nintendo.
No game data is included.

## License

[MIT](LICENSE)
