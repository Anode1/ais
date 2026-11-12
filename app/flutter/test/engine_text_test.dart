// engineText: an engine string with a byte that is not UTF-8 decodes with a
// U+FFFD in its place; the strict decode threw for the whole page of records.
import 'dart:ffi';

import 'package:ais/ais_ffi.dart';
import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('a Latin-1 byte inside an engine string does not throw', () {
    final bytes = [0x43, 0x61, 0x66, 0xE9, 0x20, 0xD0, 0xB6, 0x00]; // "Caf\xE9 ж"
    final p = calloc<Uint8>(bytes.length);
    try {
      p.asTypedList(bytes.length).setAll(0, bytes);
      expect(engineText(p.cast<Utf8>()), 'Caf\u{FFFD} ж');
      expect(() => p.cast<Utf8>().toDartString(), throwsFormatException);
    } finally {
      calloc.free(p);
    }
  });
}
