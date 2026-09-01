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
    mts         real data: when each record was last edited on THIS device
                (so a delete made elsewhere cannot undo a later edit)
    sts         real data: which records have already survived a delete made
                elsewhere, so the other devices are told once and stop resending
    katt        real data: when a tag was put on a record that already existed,
                so re-adding a tag another device removed actually sticks
    ktomb       real data: which tags were taken off, so a removal propagates
    blobs/      real data: documents saved by `doc`
    version     on-disk format version (see below)
    next_id     rebuildable from store
    idx/, off   rebuildable from store (the search index)
    lock        per-device, ephemeral: never sync this one
    foldsync    per-device: which shared folders THIS device syncs with (its own
                mount points, meaningless on another machine): never sync it
    syncfolder  per-device: the folder the app syncs with, written by the app
                (a second device would start syncing to the first one's path)
    syncid      per-device identity; it is copied by a whole-folder sync, which
                is why a cloned identity heals itself on the next folder pass

**Update every device before sharing one index folder between them.** `mts`, `sts`
and `katt` decide what a delete means, and an AIS older than 0.3.15 does not know
they exist: it would undo an edit you made on another device after that device's
delete, silently. So an index this version has opened is marked format v4, and an
older AIS refuses to open it and says to update instead. Nothing is damaged and
nothing is lost by that refusal: update the lagging device and it opens normally.

Simplest reliable rule: **sync the whole folder and ignore the per-device files.**
Everything else stays internally consistent because Syncthing keeps it identical on
both ends. In the folder's Ignore Patterns:

    lock
    foldsync
    syncfolder

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

### Two-way in one round: `ais --sync`

To converge BOTH devices in a single connection (no running it twice), use
`--sync` instead of `--export`/`--import`:

    # 1. On the host device:
    ais --sync --serve
    #    prints, e.g.:  ais --sync http://192.168.1.5:8766 --token <token>

    # 2. On the other device, run the printed command:
    ais --sync http://192.168.1.5:8766 --token <token>

Both devices merge each other's records in one exchange, so neither is fixed as
"sender" or "receiver" (the merge is order-independent, so any device can sync
with any other). The one-way `--export`/`--import` above still work for a
deliberate one-directional copy. The mobile app's "Sync" button runs this same
`--sync` exchange (Host / Join).

### Pairing by scan (phone): no token typing

Typing a 32-character token is tedious. Instead, the hosting device can show the address
and token as a QR that encodes a link:

    ais://sync?host=192.168.1.5:8766&token=<token>

On the phone, scan that with the ordinary camera app. The phone recognizes the link, opens
AIS, and AIS asks you to confirm the sync (a link can come from anywhere, and syncing shares
this device's records) before it joins. The app bundles no QR scanner: your phone's own
camera does the reading, and AIS just registers the `ais://` link. If you would rather not
scan, Join still accepts the address and token typed by hand, and the address
may be a NAME as well as a number, so `http://mylaptop.local:8766` works wherever
that name resolves (mDNS, your router's DHCP names, `/etc/hosts`).

This carries both the records (values, keys, deletions) and the `doc` blob FILES, so a
synced document opens on the peer. If two devices independently saved different documents
under the same name, both are kept (the incoming one lands beside the local one under a new
name, and its record is repointed). Limits today: it is LAN-only by design; for devices on
different networks, or for continuous background syncing, use Syncthing below.

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

    # 4. Never sync the per-device files
    #    In that folder's settings, under Ignore Patterns, add these three lines
    #    (the same three listed above: each one means something only on the device
    #    that wrote it, and syncfolder would send a second device chasing the
    #    first one's path):
    lock
    foldsync
    syncfolder

Done. An edit on one device now appears on the other within seconds on the same network.

## Sync through a shared folder (`--sync-folder`)

The alternative to sharing the whole index directory: each device drops a small
bundle of its own into one shared folder, and picks up everybody else's. The folder
can be a Syncthing folder, a mounted drive, a USB stick, or a cloud drive. Nothing
has to be reachable at the same moment, and devices never overwrite each other's
files.

    # on each device, whenever you like (a timer, a login script, or by hand)
    ais --sync-folder /path/to/shared/folder

The folder has to exist first. AIS will not create it, and that is deliberate: a
typo, or a drive that is not plugged in, would otherwise become a brand-new empty
directory that every run happily reports as synced while nothing ever arrives.
Sync is what stands in for a backup here, so a pass that silently does nothing is
the one failure you cannot notice until you need the data.

For the same reason, a folder you have synced with before is checked for device
bundles. If it holds none at all, the run stops and says so, because the usual
causes are a drive that is not mounted and a folder that was emptied or replaced.
Look at the folder; if it is genuinely the one you want in its current state, run
it again with `-y`, and it will be accepted from then on.

A folder is identified by its path, not by the drive behind it, so the very first
run into a mount point whose drive is not attached writes into the empty directory
underneath and reports success. Plug the drive in before the first sync to that
folder.

If you run this from a timer or a login script, send its output somewhere you will
see it. A refusal that only reaches a cron job's stderr is the same silence this
check exists to end.

Reading a message and acting on it beats a green tick that means nothing:

    no such folder                the path is wrong, or the drive is not plugged in
    not a folder                  the path is a file
    cannot read that folder       permissions, or a dead network/cloud mount
    no device bundles in          synced here before, empty now: not mounted/emptied
    cannot write into             read-only or full: others will not see this device

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
