# Syncing your AIS index across devices (no cloud)

AIS keeps everything as plain files in one index folder, so syncing needs no cloud account.
There are two no-cloud paths, and you can use both:

- **Built-in one-shot LAN sync** (below): `ais --export --serve` on one device, `ais --import`
  on the other. Encrypted, nothing to install, good for an occasional copy or merge.
- **Syncthing** for continuous, automatic background syncing: peer-to-peer over your own
  network (or an encrypted relay when the devices are apart), open source, on Android and
  Linux, Windows, macOS.

## What an index holds (so you know what matters)

    store       real data, the SOURCE OF TRUTH (append-only records)
    tomb        real data: which records were deleted
    blobs/      real data: documents saved by `doc`
    version     on-disk format version
    next_id     rebuildable from store
    idx/, off   rebuildable from store (the search index)
    lock        per-device, ephemeral: never sync this one

Simplest reliable rule: **sync the whole folder and ignore only `lock`.** Everything
else stays internally consistent because Syncthing keeps it identical on both ends.

## Built-in one-shot LAN sync (no setup)

For a quick copy or merge between two devices on the same Wi-Fi, AIS has this built in,
end-to-end encrypted, with nothing to install:

# 1. On device A (the source), serve the index to one peer:
ais --export --serve
#    It prints a one-time token and the exact command to run on the other device, e.g.:
#        ais --import http://192.168.1.5:8766 --token ad61d80ed83fbfe381eeac93768aa676

# 2. On device B (the destination), run that printed command:
ais --import http://192.168.1.5:8766 --token ad61d80ed83fbfe381eeac93768aa676

Device B pulls A's records and merges them: new values arrive, and deletions made on A
propagate to B (last writer wins, by timestamp). The transfer is encrypted (XChaCha20-Poly1305)
under a key derived from the one-time token, and the token itself never crosses the wire (the
client proves it knows it by answering a challenge), so a snoop or tamperer on the LAN gets only
ciphertext, and a wrong token is rejected before anything is merged. The server is single-shot: it serves one
pull, then exits. Run it the other way to also merge B's changes back into A.

The default port is 8766; pass one to `ais --export --serve PORT` to change it.

Limits today: this carries the records (values, keys, deletions), not the `doc` blob FILES,
so a synced document reference can dangle on the peer until blob transfer lands. It is
LAN-only by design; for devices on different networks, or for continuous background syncing,
use Syncthing below.

## Syncthing setup

# 1. Install on both devices
#    Linux computer:   apt install syncthing   (then run: syncthing)
#    Windows / macOS:  https://syncthing.net  (SyncTrayzor is a nice Windows wrapper)
#    Android phone:    "Syncthing-Fork" from F-Droid or Play (the maintained app)

# 2. Pair the two devices (one time)
#    Open each device's Syncthing web UI (computer: http://127.0.0.1:8384).
#    Phone: Actions -> Show ID (a QR code). Computer: Add Remote Device, then scan or
#    paste the ID. Accept the prompt on the other device. They can now see each other.

# 3. Share the index folder
#    Computer: Add Folder, point it at your index (for example ~/.ais), give it a
#    Folder ID like "ais-index", and on the Sharing tab tick the phone. On the phone,
#    accept the offered folder and choose where it lands (the AIS app's index dir).

# 4. Never sync the lock file
#    In that folder's settings, under Ignore Patterns, add this one line:
lock

Done. An edit on one device now appears on the other within seconds on the same network.

## Conflicts

As a single user you are almost always on one device at a time, so conflicts are rare.
If you do edit the same index on two devices while offline, Syncthing keeps both copies
and writes a file named like `store.sync-conflict-...`; reconcile by merging the store
(it is append-only and built to merge). Two-writer chaos is a multi-user problem you do
not have here.

## Other no-cloud options

    LocalSend / KDE Connect   one-shot manual transfer over Wi-Fi, no background daemon
    git over LAN or SSH       keeps history; push the index to your computer yourself
    USB cable (adb or MTP)    air-gapped, bulletproof, good for the first big copy

Avoid Bluetooth: it is slow and fiddly, and every option above uses Wi-Fi instead.
