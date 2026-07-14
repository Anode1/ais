// add_validation.dart -- the pure decision behind the Add sheet's Save button,
// kept engine- and widget-free so it is unit-testable. The bug this guards: the
// Save handler used to `return` silently on an empty value, during a sync, or on
// a missing passphrase, leaving the dialog open with no feedback ("nothing
// happens, very confusing"). Every non-proceed path now yields a message.

/// The message to show when Save is tapped, or null if the save may proceed.
String? addSaveError({
  required String value,
  required bool engineReady,
  required bool syncing,
  required bool encrypt,
  required String passphrase,
}) {
  if (!engineReady) return null; // the Add button is disabled; nothing to report
  if (value.isEmpty) return 'Type something to remember first.';
  if (syncing) return 'A sync is running. Try again in a moment.';
  if (encrypt && passphrase.isEmpty) return 'Enter a passphrase to encrypt.';
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
