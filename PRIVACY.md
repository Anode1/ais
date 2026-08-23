# Privacy Policy for AIS

Last updated: 1 July 2026

AIS ("the app") is a personal, offline associative index for links, notes,
documents, and passwords. This policy explains how the app handles your
information. The short version: the app does not collect, transmit, or share any
personal data.

## Data the app stores

Everything you enter (keys, entries, links, notes, and secrets) is stored
**locally on your device**. Secrets are **encrypted** on the device with a
passphrase you choose. Your index is your own plain-text files: you can open,
read and copy them yourself. That is deliberate, and it means anyone who can read
those files can read everything in them that you did not encrypt. The app has no
user accounts and no server, and nothing you store is sent to the developer.

## Deleting an entry

Deleting an entry removes it from your index, and syncing carries the deletion to
your other devices. To make it travel, AIS keeps a small permanent marker: the
date you deleted, and a short fingerprint of the deleted value. The value itself
is not kept. The marker stays for the life of the index, so a device that was
switched off when you deleted something does not push it back later.

Be aware of what that marker means. The fingerprint is a fast checksum, not a
cryptographic one. Someone who can read your index files, and who already guesses
what a deleted entry might have been, can test that guess against the fingerprint
in seconds. It will not tell them what you deleted, but it can confirm a guess,
and it tells them the date. Until you run `ais --compact`, your index also still
records which tags a deleted entry carried.

Entries you saved **encrypted** are not affected: their fingerprint is taken over
the encrypted form, which cannot be guessed this way, and the encrypted file is
shredded the moment you delete. If something may need to be truly gone later,
save it encrypted.

If you want a deletion to leave no trace at all, run:

    ais --compact --forget-deleted

That erases the deletion markers on this device: the entries stay deleted, but
nothing is left for anyone to test a guess against them. Sync your other devices
first: a device that has not yet seen the deletion can send the entry back.

Editing an entry is not the same as deleting it. The index keeps one record per
in-place edit, so your other devices can apply the same edit, and that record
holds a fingerprint of the text you replaced. As with a deletion, the fingerprint
will not tell anyone what the old text was, but it can confirm a guess.
`--forget-deleted` forgets the intermediate versions and keeps the fingerprints,
since they are what makes an edit travel. If text may need to be truly gone,
delete the entry rather than editing it out, or save it encrypted.

## Data we collect

**None.** The app does not collect, log, or transmit personal data, usage
analytics, advertising identifiers, location, contacts, or any other information
to the developer or to third parties. There is no telemetry and no crash
reporting.

## On-demand LAN sync

The app offers optional **on-demand LAN sync**: a one-time transfer of your index
**directly between your own devices** over your local network, only when you start
it. That traffic is **end-to-end encrypted** and travels only between devices you
control. It does not pass through the developer or any third-party server.

You can also sync through a **shared folder** — an SD card, a USB stick, or a
cloud folder you chose. Those files are plain text, like your index. Nothing is
sent to the developer either way, but anyone who can read that folder can read
what is in it, so pick a folder only you can reach.

## Third parties, ads, and tracking

The app contains **no advertising, no third-party analytics, and no tracking
components**. No data is shared with or sold to anyone.

## Permissions

The app uses device storage to keep your index on the device, and local-network
access only for the optional device-to-device sync described above. These
permissions are not used to send data off your device for any other purpose.

**Microphone.** The app asks for the microphone only when you tap the mic button
to dictate a search, and only for as long as you are dictating. Speech is
recognised **on the device**: the app requests on-device recognition explicitly
and does not fall back to a cloud recogniser, so your audio and the words it
becomes are not sent to us or to anyone else. If your phone has no offline
language pack installed for your language, dictation simply does not work and the
app says so, rather than sending the audio away. Nothing is recorded or stored:
the recognised words go into the search box, and that is all.

## Children's privacy

The app is a general-purpose utility, is not directed at children, and does not
knowingly collect data from anyone, including children.

## Changes to this policy

This policy lives at:

https://github.com/Anode1/ais/blob/main/PRIVACY.md

If it changes, the updated version is posted there with a new "Last updated" date.
That is the address given to the app stores, so it is the one to check.

## Contact

For questions about this policy or the app, open an issue at the project
repository: https://github.com/Anode1/ais

AIS is free and open-source software (GPLv2). Its full source, including every
line of data handling described above, is publicly auditable at the repository.
