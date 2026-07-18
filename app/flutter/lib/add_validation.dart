// add_validation.dart -- the pure decision behind the Add sheet's Save button,
// kept engine- and widget-free so it is unit-testable. The bug this guards: the
// Save handler used to `return` silently on an empty value, during a sync, or on
// a missing passphrase, leaving the dialog open with no feedback ("nothing
// happens, very confusing"). Every non-proceed path now yields a message.

// Engine limits (see c/common.h). One encoded key (a path component) is bounded
// by AIS_KEY_MAX; one whole store line (id|ts|keys|value) by AIS_LINE_MAX. We
// bound the value by AIS_LINE_MAX as a coarse guard -- the engine enforces the
// exact line budget -- so an over-long paste gets a length-specific message
// instead of being silently truncated behind the generic "Could not save".
const int kAisKeyMax = 512;
const int kAisLineMax = 65536;

/// The message to show when Save is tapped, or null if the save may proceed.
String? addSaveError({
  required String value,
  required bool engineReady,
  required bool syncing,
  required bool encrypt,
  required String passphrase,
  String keys = '',
}) {
  if (!engineReady) return null; // the Add button is disabled; nothing to report
  if (value.trim().isEmpty) return 'Type something to remember first.';
  if (syncing) return 'A sync is running. Try again in a moment.';
  if (encrypt && passphrase.isEmpty) return 'Enter a passphrase to encrypt.';
  final content = contentError(value: value, keys: keys);
  if (content != null) return content;
  return null;
}

/// A length/character problem the engine would silently swallow, or null when
/// [value] and [keys] are within the engine's limits. A NUL byte truncates the
/// record at the C-string boundary; an over-long key or value is dropped or
/// truncated. Surface WHY rather than let a save look like it worked and lose
/// text. [keys] is the normalized, space-separated tag string.
String? contentError({required String value, required String keys}) {
  if (value.contains('\u0000') || keys.contains('\u0000')) {
    return 'Remove the special (null) character before saving.';
  }
  for (final k in keys.split(RegExp(r'\s+')).where((k) => k.isNotEmpty)) {
    if (k.length > kAisKeyMax) {
      return 'One of your tags is too long (max $kAisKeyMax characters).';
    }
  }
  if (value.length > kAisLineMax) {
    return 'That note is too long to save (max $kAisLineMax characters).';
  }
  return null;
}

/// True when the engine actually persisted the record: store() and
/// storeEncryptedAsync() return the new id, or -1 when nothing was written.
bool saveSucceeded(int id) => id >= 0;

/// The message after an Add save. The bug this guards: the handler popped the
/// sheet and showed "Saved" even when the engine returned -1 (bad args, blob
/// write failure, crypto not built), so a failed write looked successful.
String saveOutcomeMessage(int id, String keys) => !saveSucceeded(id)
    ? 'Could not save. Check storage and try again.'
    : (keys.isEmpty ? 'Saved (no tags)' : 'Saved under: $keys');

/// The message after an in-place tag edit; the engine's update() bool decides.
/// The bug this guards: the handler showed "Tags updated" unconditionally, even
/// when update() returned false (unknown/deleted record), telling the user an
/// edit succeeded that changed nothing.
String tagsUpdateMessage(bool ok) => ok ? 'Tags updated' : "Couldn't update tags";
