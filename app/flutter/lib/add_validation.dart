// add_validation.dart -- the pure decision behind the Add sheet's Save button,
// kept engine- and widget-free so it is unit-testable. Every non-proceed path
// yields a message: Save never fails silently.

import 'dart:convert' show utf8;

// Engine limits, in BYTES of UTF-8. A key is a posting file name, so it is
// bounded by AIS_KEY_NAME_MAX (c/common.h); over it the engine refuses the
// save. A plain value has no cap: a long or multi-line one goes to a blob
// (c/doc.c). An encrypted value must seal into embed.c's 8192-byte buffer
// (57-byte header, MAC, base64), which 6000 bytes of plaintext fit.
const int kAisKeyMax = 255;
const int kAisEncryptedMax = 6000;

/// The message to show when Save is tapped, or null if the save may proceed.
/// [passphraseRepeat] is the confirmation field; null means no such field is
/// shown. Compared raw, no trimming: the engine gets the passphrase verbatim,
/// so a copy that differs by a space encrypts under a different key.
String? addSaveError({
  required String value,
  required bool engineReady,
  required bool syncing,
  required bool encrypt,
  required String passphrase,
  String? passphraseRepeat,
  String keys = '',
}) {
  if (!engineReady) return null; // the Add button is disabled; nothing to report
  if (value.trim().isEmpty) return 'Type something to remember first.';
  if (syncing) return 'A sync is running. Try again in a moment.';
  if (encrypt && passphrase.isEmpty) return 'Enter a passphrase to encrypt.';
  if (encrypt && passphraseRepeat != null && passphrase != passphraseRepeat) {
    return 'Passphrases do not match';
  }
  final content = contentError(value: value, keys: keys, encrypt: encrypt);
  if (content != null) return content;
  return null;
}

/// A length/character problem the engine would answer with a generic failure,
/// or null when [value] and [keys] are within its limits. A NUL byte truncates
/// the record at the C-string boundary; an over-long key is refused; an
/// over-long encrypted value does not seal. [keys] is the normalized,
/// space-separated tag string.
String? contentError(
    {required String value, required String keys, bool encrypt = false}) {
  if (value.contains('\u0000') || keys.contains('\u0000')) {
    return 'Remove the special (null) character before saving.';
  }
  for (final k in keys.split(RegExp(r'\s+')).where((k) => k.isNotEmpty)) {
    if (utf8.encode(k).length > kAisKeyMax) {
      return 'One of your tags is too long (max $kAisKeyMax characters).';
    }
  }
  if (encrypt && utf8.encode(value).length > kAisEncryptedMax) {
    return 'An encrypted note holds at most $kAisEncryptedMax characters. '
        'Shorten it, or save it unencrypted.';
  }
  return null;
}

/// True when the engine actually persisted the record: store() and
/// storeEncryptedAsync() return the new id, or -1 when nothing was written.
bool saveSucceeded(int id) => id >= 0;

/// The message after an Add save. id < 0 (bad args, blob write failure, crypto
/// not built) is reported as a failure, never as "Saved". [merged] means the
/// text was already in the index, so the save landed on that record (a value is
/// identity): restamped to today, keys attached, no new row. Saying "Saved"
/// there reads as a second note, and the missing row as data loss.
String saveOutcomeMessage(int id, String keys, {bool merged = false}) =>
    !saveSucceeded(id)
        ? 'Could not save. Check storage and try again.'
        : merged
            ? 'Already in your memory: kept as one entry, dated today'
            : (keys.isEmpty ? 'Saved (no tags)' : 'Saved under: $keys');

/// The message after an in-place tag edit; the engine's update() bool decides.
/// It is false for an unknown or deleted record, where nothing changed.
String tagsUpdateMessage(bool ok) => ok ? 'Tags updated' : "Couldn't update tags";
