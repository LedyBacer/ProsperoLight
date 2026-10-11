# Starting a stream from a home-screen forwarder

A forwarder is a separate small app with its own home-screen tile: its own title ID, icon
and name. When it is opened, it starts ProsperoLight (`PPSA99002`) with launch arguments,
and ProsperoLight starts that stream by itself instead of waiting on its launcher. One tile
per PC game, each with the game's own picture.

ProsperoLight does not make the tiles. Tools that follow the
[PS5 Forwarder Format](https://github.com/heydemoura/ps5-forwarder-format) do, such as
[Forwarder Manager](https://github.com/heydemoura/ps5-forwarder-manager): give them
`PPSA99002` as the target and the arguments below.

## Arguments

| Argument | Meaning |
| --- | --- |
| `--app <name or number>` | The app to start, as the Games page names it (`Steam Big Picture`), or its number in Sunshine. Required: without it the launcher opens as usual. |
| `--pc <name or address>` | The saved PC to stream from: its name as the PCs page shows it, or its address, with the port when Sunshine does not use the default one (`192.168.1.50:48989`). Without it, the PC that was selected last. |
| `--exit-after-stream` | When the stream ends normally, close ProsperoLight so the console returns to the home screen. Without it, the launcher opens. |

Each value may also follow an equals sign (`--app=Desktop`). Names are compared whole and
without regard to capitals. Unknown arguments are ignored.

An app's number never changes when the app is renamed on the PC; its name is easier to
read. Either works.

## What happens

1. ProsperoLight shows its connecting screen at once, with the app's name: no opening
   animation, no sound and none of the launcher's pages. Behind it, it selects the PC and
   waits for it to answer, for up to fifteen seconds.
2. It selects the app and starts the stream exactly as a press on Start does, with the
   settings saved in ProsperoLight (resolution, frame rate, codec, bitrate, and the
   settings kept for that PC).
3. Leaving the stream (Touchpad + L1) opens the Games page, or, with
   `--exit-after-stream`, closes ProsperoLight.

The launcher appears with a notice, and nothing is streamed, when:

- the PC is not one of the saved ones;
- the PC does not answer in time;
- the PC is not paired with this PS5 (pair it once from the PCs page);
- the PC has no app of that name or number;
- the PC cannot encode what the settings ask for (the launcher says which setting).

A stream that fails or is cut off opens the launcher with the reason, with or without
`--exit-after-stream`.

## What a forwarder tool can read

So that a tool can offer a choice instead of asking for names, ProsperoLight keeps the saved
PCs, their apps and box art under `/data/prosperolight/forwarder`:

| Path | Contents |
| --- | --- |
| `pcs/<key>.json` | One paired PC and the apps it lists. |
| `posters/<key>/<id>.png` | One app's box art, as Sunshine sent it (usually 600x800). |

`<key>` is the PC's address and port, as in `192.168.1.50_47989`. A PC's file looks like this:

```json
{
  "format": "prosperolight-forwarder-catalog/1",
  "key": "192.168.1.50_47989",
  "name": "Gaming-PC",
  "address": "192.168.1.50",
  "port": 47989,
  "apps": [
    {"id": 1298491830, "name": "Desktop"},
    {"id": 7, "name": "Steam Big Picture"}
  ]
}
```

- A PC's file is written when its app list arrives or changes while ProsperoLight is open,
  and deleted when the PC is removed. A PC that was never paired has no file.
- Box art is saved as it is fetched: for the apps on screen first, then for the rest of the
  selected PC's list. An app may have no file yet.
- Files are replaced whole, so a reader never sees half of one.
- `name` and `address` are what `--pc` takes; an app's `name` or `id` is what `--app` takes.
- The list exists only when ProsperoLight has its `/data/prosperolight` folder.

The code is in [`include/forwarder_catalog.hpp`](../include/forwarder_catalog.hpp).

## Limits

- Arguments reach a new ProsperoLight only. If ProsperoLight is already running, the
  console brings it to the front as it is; a forwarder should close it first.
- The forwarded stream is started once. After it, ProsperoLight behaves as if it had been
  opened from its own tile.
- The PC has to be saved and paired beforehand, from ProsperoLight itself.

Parsing and matching are in [`include/forwarded_launch.hpp`](../include/forwarded_launch.hpp);
the steps are in [`src/launcher/forwarded_start.hpp`](../src/launcher/forwarded_start.hpp).
