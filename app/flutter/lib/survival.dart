// survival.dart -- keeping the index alive across an uninstall on mobile.
// The store lives in the app's private directory and dies with the app; the
// pieces here decide when to say so and remember that it was said. Pure Dart
// (no engine, no platform channels) so `flutter test` drives them directly.
import 'dart:io';

import 'ais_ffi.dart' show TlRow;

/// Whether the one-time "keep a copy" hint is due: the save that just landed
/// is the store's first record, on a phone, and the hint was never shown here.
bool keepHintDue(
        {required bool mobile, required int liveBefore, required bool shown}) =>
    mobile && liveBefore == 0 && !shown;

/// The once-per-store hint flag: a plain file next to the store, like
/// `syncfolder`. An unreadable dir counts as shown, so a broken store is
/// never nagged about keeping itself.
class KeepHintFlag {
  final String dir;
  const KeepHintFlag(this.dir);
  File get _file => File('$dir/keephint');
  bool get shown {
    try {
      return _file.existsSync();
    } catch (_) {
      return true;
    }
  }

  void markShown() {
    try {
      _file.writeAsStringSync('shown\n');
    } catch (_) {}
  }
}

/// Test seam standing in for the mobile store: `flutter test` runs on the host
/// with no libais.so, so the survival flow (first-save hint, empty-state
/// restore) is driven through this fake instead of the engine. Null = the real
/// engine and platform.
class SurvivalHarness {
  final String dir; // flag files land here, beside a store that isn't there
  final List<TlRow> Function() timeline;
  final int Function() countLive;
  final int Function(String keys, String value) store;
  // The restore picker+merge: null = cancelled, true = a copy merged in,
  // false = the folder held no usable copy.
  final Future<bool?> Function() pickAndRestore;
  const SurvivalHarness(
      {required this.dir,
      required this.timeline,
      required this.countLive,
      required this.store,
      required this.pickAndRestore});
}
