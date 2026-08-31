// Uninstall survival on mobile: the one-time keep hint after the first save
// into an empty store, the empty state's "Restore from a folder" action, and
// the once-per-store flag file. Engine-less: SurvivalHarness (survival.dart)
// stands in for the store, since `flutter test` runs with no libais.so.
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/main.dart';
import 'package:ais/survival.dart';

const _hintBit = 'private storage';

class _FakeStore {
  final rows = <TlRow>[];
  int _next = 1;
  int store(String keys, String value) {
    rows.insert(0, TlRow(_next, '2026-08-30T10:00:00Z', keys, value));
    return _next++;
  }
}

void main() {
  late Directory tmp;
  setUp(() => tmp = Directory.systemTemp.createTempSync('ais_survival'));
  tearDown(() => tmp.deleteSync(recursive: true));

  SurvivalHarness harness(_FakeStore s, {Future<bool?> Function()? restore}) =>
      SurvivalHarness(
        dir: tmp.path,
        timeline: () => List.of(s.rows),
        countLive: () => s.rows.length,
        store: s.store,
        pickAndRestore: restore ?? () async => null,
      );

  Future<void> pumpApp(WidgetTester tester, SurvivalHarness h) async {
    await tester.pumpWidget(MaterialApp(home: RecallPage(survival: h)));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));
  }

  Future<void> save(WidgetTester tester, String text) async {
    // Empty store: the empty state's own Add button; afterwards the FAB.
    final emptyAdd = find.text('Add something');
    await tester
        .tap(emptyAdd.evaluate().isNotEmpty ? emptyAdd : find.text('Add'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400)); // sheet animation
    await tester.enterText(
        find.widgetWithText(TextField, 'What to remember'), text);
    await tester.tap(find.text('Save'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400)); // sheet closes
  }

  // Past the "Saved." snackbar (4s) into the queued hint, or the silence
  // where the hint would have been.
  Future<void> drainSaveSnack(WidgetTester tester) async {
    await tester.pump(const Duration(seconds: 5));
    await tester.pump(const Duration(milliseconds: 400));
  }

  testWidgets('the keep hint follows the first save only', (tester) async {
    final s = _FakeStore();
    await pumpApp(tester, harness(s));
    expect(find.text('Add something'), findsOneWidget);

    await save(tester, 'first note');
    await drainSaveSnack(tester);
    expect(find.textContaining(_hintBit), findsOneWidget);
    expect(find.text('Set folder'), findsOneWidget);
    // The once-flag landed next to the store.
    expect(KeepHintFlag(tmp.path).shown, isTrue);

    // Dismiss the hint the way a user would, through its own action (the
    // auto-dismiss timer arms only on a later rebuild, which a still test
    // never produces). Settle first: mid-entrance it ignores pointers.
    // Under the harness the picker behind the action is a no-op.
    await tester.pumpAndSettle();
    await tester.tap(find.text('Set folder'));
    await tester.pumpAndSettle();
    expect(find.textContaining(_hintBit), findsNothing);

    await save(tester, 'second note');
    await drainSaveSnack(tester);
    expect(find.textContaining(_hintBit), findsNothing);
  });

  testWidgets('a store that was hinted before never hints again',
      (tester) async {
    KeepHintFlag(tmp.path).markShown(); // a previous session said it
    final s = _FakeStore();
    await pumpApp(tester, harness(s));
    await save(tester, 'first note');
    await drainSaveSnack(tester);
    expect(find.textContaining(_hintBit), findsNothing);
  });

  test('the once-flag persists across instances, per store', () {
    expect(keepHintDue(mobile: true, liveBefore: 0, shown: false), isTrue);
    expect(keepHintDue(mobile: true, liveBefore: 1, shown: false), isFalse);
    expect(keepHintDue(mobile: false, liveBefore: 0, shown: false), isFalse);
    expect(keepHintDue(mobile: true, liveBefore: 0, shown: true), isFalse);

    expect(KeepHintFlag(tmp.path).shown, isFalse);
    KeepHintFlag(tmp.path).markShown();
    expect(KeepHintFlag(tmp.path).shown, isTrue, reason: 'a fresh instance');
    final other = Directory.systemTemp.createTempSync('ais_survival_other');
    addTearDown(() => other.deleteSync(recursive: true));
    expect(KeepHintFlag(other.path).shown, isFalse, reason: 'per store');
  });

  testWidgets('the empty state offers Restore from a folder on mobile',
      (tester) async {
    await pumpApp(tester, harness(_FakeStore()));
    expect(find.text('Restore from a folder'), findsOneWidget);
  });

  testWidgets('restore runs the picker seam and lands on the records',
      (tester) async {
    final s = _FakeStore();
    var picks = 0;
    await pumpApp(tester, harness(s, restore: () async {
      picks++;
      s.store('venice hotel', 'hotel danieli');
      return true;
    }));

    await tester.tap(find.text('Restore from a folder'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));
    expect(picks, 1);
    expect(find.textContaining('hotel danieli'), findsOneWidget);
    expect(find.text('Restore from a folder'), findsNothing);
  });

  testWidgets('a folder with no copy reports inside the empty state',
      (tester) async {
    await pumpApp(tester, harness(_FakeStore(), restore: () async => false));
    await tester.tap(find.text('Restore from a folder'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text('No AIS copy found in that folder.'), findsOneWidget);
    expect(find.text('Restore from a folder'), findsOneWidget); // still empty
  });

  testWidgets('a cancelled restore changes nothing', (tester) async {
    await pumpApp(tester, harness(_FakeStore())); // seam answers null
    await tester.tap(find.text('Restore from a folder'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text('No AIS copy found in that folder.'), findsNothing);
    expect(find.text('Restore from a folder'), findsOneWidget);
  });
}
