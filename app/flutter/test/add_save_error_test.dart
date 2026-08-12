// The Add sheet's Save must never fail silently: each non-proceed path yields a
// message, a valid save yields null.
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/add_validation.dart';

void main() {
  group('addSaveError', () {
    test('empty value asks for content', () {
      expect(
        addSaveError(value: '', engineReady: true, syncing: false, encrypt: false, passphrase: ''),
        'Type something to remember first.');
    });
    test('sync in progress tells the user to wait (the bug you hit)', () {
      expect(
        addSaveError(value: 'x', engineReady: true, syncing: true, encrypt: false, passphrase: ''),
        'A sync is running. Try again in a moment.');
    });
    test('encrypt without passphrase asks for one', () {
      expect(
        addSaveError(value: 'x', engineReady: true, syncing: false, encrypt: true, passphrase: ''),
        'Enter a passphrase to encrypt.');
    });
    test('valid plain save proceeds (null)', () {
      expect(
        addSaveError(value: 'x', engineReady: true, syncing: false, encrypt: false, passphrase: ''),
        isNull);
    });
    test('valid encrypted save proceeds (null)', () {
      expect(
        addSaveError(value: 'x', engineReady: true, syncing: false, encrypt: true, passphrase: 'pw'),
        isNull);
    });
    test('whitespace-only value is treated as empty', () {
      expect(
        addSaveError(value: '   ', engineReady: true, syncing: false, encrypt: false, passphrase: ''),
        'Type something to remember first.');
    });
  });

  // An over-long or NUL-bearing key/value, which the engine would silently
  // truncate, gets a specific message up front.
  group('contentError / engine limits', () {
    test('a value within limits is fine (null)', () {
      expect(contentError(value: 'a note', keys: 'home wifi'), isNull);
    });
    test('a NUL in the value is rejected with a specific message', () {
      expect(contentError(value: 'a\u0000b', keys: ''),
          'Remove the special (null) character before saving.');
    });
    test('an over-long tag is rejected by length', () {
      final longKey = 'k' * (kAisKeyMax + 1);
      expect(contentError(value: 'x', keys: longKey),
          'One of your tags is too long (max $kAisKeyMax characters).');
    });
    test('a tag at the limit is allowed', () {
      expect(contentError(value: 'x', keys: 'k' * kAisKeyMax), isNull);
    });
    test('an over-long value is rejected by length', () {
      expect(contentError(value: 'v' * (kAisLineMax + 1), keys: ''),
          'That note is too long to save (max $kAisLineMax characters).');
    });
    test('addSaveError surfaces a content problem too', () {
      final longKey = 'k' * (kAisKeyMax + 1);
      expect(
        addSaveError(value: 'x', engineReady: true, syncing: false, encrypt: false, passphrase: '', keys: longKey),
        'One of your tags is too long (max $kAisKeyMax characters).');
    });
  });

  // store()/storeEncryptedAsync() returning -1 persisted nothing.
  group('saveOutcomeMessage / saveSucceeded', () {
    test('failed store (id < 0) is reported, NOT "Saved"', () {
      expect(saveSucceeded(-1), isFalse);
      expect(saveOutcomeMessage(-1, 'note'),
          'Could not save. Check storage and try again.');
    });
    test('successful store under tags announces the tags', () {
      expect(saveSucceeded(1), isTrue);
      expect(saveOutcomeMessage(1, 'note bank'), 'Saved under: note bank');
    });
    test('successful keyless store says (no tags)', () {
      expect(saveOutcomeMessage(7, ''), 'Saved (no tags)');
    });
    test('id 0 is a valid stored record (boundary)', () {
      expect(saveSucceeded(0), isTrue);
    });
  });

  // update() is false for an unknown or deleted record.
  group('tagsUpdateMessage', () {
    test('update failure is reported, NOT "Tags updated"', () {
      expect(tagsUpdateMessage(false), "Couldn't update tags");
    });
    test('update success announces it', () {
      expect(tagsUpdateMessage(true), 'Tags updated');
    });
  });
}
