# MeshXpress

An AmiExpress-flavoured BBS ("/X" for the mesh) that runs as a MeshCore **room server**. Users log in to the
room as usual from the MeshCore app; the room's chat view becomes the BBS terminal.

```
-= MeshXpress /X =- [GENERAL]
R)ead L)ist E)nter J)oin
W)ho S)tats B)ulletins C)omment
D)oors H)andle G)oodbye
```

* Single letters (optionally with a number: `J 2`, `R 12`, `D 1`) and anything starting
  with `/` are commands. Everything else is posted as a message in the current area, so
  people who never learn the commands can still chat like in a normal room.
* Every reply is a private message to the caller, max 150 chars per page, at most 3 pages
  per command (`(1/3)` footers). No ANSI, no colours: LoRa is slow and the app is plain text.
* New callers pick a handle on first login. Handles, messages, calls and read pointers
  persist on the node's flash (SPIFFS, `/bbs/*.dat`).
* Areas: GENERAL, TECH, MARKET, plus SYSOP (comments to sysop, visible to admins only)
  and BULLETIN (read with `B`, written by the sysop). Posts are also pushed to everyone
  logged in as normal room posts, prefixed `[TECH] handle: ...` outside GENERAL.
* Door: Number Guess with a persistent top-5.

## Layout

| dir | what |
|---|---|
| `core/` | portable C++17 engine, no Arduino dependencies (`BbsEngine`, `BbsStorage`, `Pager`, `Doors`) |
| `host/` | Mac/Linux REPL + scenario tests over plain files |
| `firmware/` | PlatformIO project for Heltec LoRa32 V3 and V4, builds against `../MeshCore-upstream` as a library |

## Host: try it without hardware

```bash
cd host
make test        # scenario tests, checks every page <= 150 bytes and UTF-8 safe
make repl && ./repl
!login fredrik admin     # simulated login; "admin" = sysop
Zombie                   # pick a handle
?                        # menu
```

## Firmware

### Just flash it

Every push builds Heltec V3 and V4 images in GitHub Actions (artifacts on the Actions tab;
tagged `v*` builds become draft releases). Grab `MeshXpress_Heltec_v3-<version>-merged.bin`
(or `_v4`) and:

```bash
pip install esptool
esptool.py --chip esp32s3 --port /dev/cu.usbserial-XXXX write_flash 0x0 MeshXpress_Heltec_v3-<version>-merged.bin
```

The merged image contains bootloader, partition table and app, so it goes to address 0.
Any browser flasher that takes a single image at 0x0 works too. Your node identity lives
in the SPIFFS partition and survives the flash; flash a companion image back later to
turn the node into a companion again.

### Build it yourself

Expects a MeshCore checkout next to this repo: `../MeshCore-upstream` (clone of
https://github.com/meshcore-dev/MeshCore). `firmware/lib/ed25519` is a symlink into it.

```bash
cd firmware
pio run -e Heltec_v3_bbs                    # build (or -e Heltec_v4_bbs)
pio run -e Heltec_v3_bbs -t mergebin        # + single flashable image (firmware-merged.bin)
pio run -e Heltec_v3_bbs -t upload          # flash (Heltec on USB)
pio device monitor                          # serial console = sysop console
```

Build flags of note (in `platformio.ini`): `ADVERT_NAME`, `ROOM_PASSWORD` (what callers
type to log in), `ADMIN_PASSWORD` (sysop), `BBS_ANNOUNCE_LOGINS=0` to silence
"* X logged in" room posts.

The project consumes `MeshCore-upstream` via its `library.json` (`build_as_lib.py`,
`-D MC_VARIANT=heltec_v3`). Two quirks: `lib/ed25519` is a symlink into the upstream tree
and is compiled as project source (`build_src_filter`), because the library dependency
finder does not pick it up through a `.cpp` include; and the OTA web server libraries are
listed explicitly since `ESP32Board.cpp` needs them when `ADMIN_PASSWORD` is defined.

### Sysop commands (USB serial, or admin CLI over the air)

```
bbs.stats                 users / calls / online / msgs per area / uptime
bbs.users [page]          list handles and call counts (* = sysop)
bbs.post <text>           bulletin: stored + pushed to the room as "* BULLETIN: ..."
bbs.say <text>            system post to GENERAL as SYSOP
bbs.kick <handle>         drop a session
bbs.wipe <area>           delete an area's messages (general|tech|market|sysop|bulletin)
bbs.announce on|off       login announcements
```
The OLED shows "N online" and the last BBS event on the two bottom lines.

## Firmware integration points (for hacking)

* `MyMesh::onAnonDataRecv` — after a successful room login, `BbsEngine::onLogin` produces
  the banner (or the handle prompt), queued 2.3 s later so the login RESPONSE lands first.
* `MyMesh::onPeerDataRecv`, `TXT_TYPE_PLAIN` branch — every line from a logged-in user
  goes to `BbsEngine::onInput`. Reply pages are queued and sent after the ACK.
* `MyMesh::loop` — drains the page queue (one packet per pass, 1.5 s between pages to the
  same caller) and refreshes the OLED status line.
* `BbsAction::broadcast` → `storePost()` into the room's normal post ring, so the
  existing push/ACK machinery delivers it to everyone.

## License

MIT, see `LICENSE`. The firmware sources under `firmware/src/` derive from MeshCore's
`simple_room_server` example (MIT); see `firmware/src/NOTICE.md`.
