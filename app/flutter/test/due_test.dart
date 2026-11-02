// The Due group at the top of Recent (doc/dev/GUI.md, "The Due group"): the
// rows under an elapsed due key, headed "Due", five shown and the rest behind
// "N more due"; Done and Later in the menu of a row with such a key; Done is a
// deferred key edit with the delete's Undo window. Engine-less: Recent, the
// due rows and the key edit are injected and recorded. due:2020 has elapsed on
// any real day, due:2999 has not. Bounded pump()s, never pumpAndSettle.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/main.dart';

const rows = [
  TlRow(3, '2026-10-28T10:00:00Z', 'tank due:2020', 'flush the tank'),
  TlRow(2, '2026-10-27T10:00:00Z', 'home due:2999', 'a later note'),
  TlRow(1, '2026-10-26T10:00:00Z', 'recipe', 'a note'),
];

class Calls {
  final update = <(int, String)>[];
}

// The due rows start as [due]; a key edit drops that id from them, as the
// engine's detach would.
Future<Calls> pumpPage(WidgetTester tester, {required List<TlRow> due}) async {
  final calls = Calls();
  var dueNow = [...due];
  await tester.pumpWidget(MaterialApp(
      home: RecallPage(
    tagLookup: (_) => const [],
    tagCloud: () => const [],
    recallLookup: (_, __) => const [],
    findLookup: (_) => const [],
    timelineLookup: (_) => rows,
    dueLookup: () => dueNow,
    updateHook: (id, keys) {
      calls.update.add((id, keys));
      dueNow = dueNow.where((r) => r.id != id).toList();
      return true;
    },
  )));
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 50));
  return calls;
}

Future<void> settle(WidgetTester tester) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 400));
}

Finder dueTile(int id) => find.byKey(ValueKey('due-$id'));
Finder dayTile(int id) => find.byKey(ValueKey(id));

// The test viewport is short: bring a row into the list's visible part.
Future<void> scrollTo(WidgetTester tester, Finder f) async {
  await tester.dragUntilVisible(f, find.byType(ListView), const Offset(0, -120));
  await tester.pump();
}

Future<void> openMenu(WidgetTester tester, Finder tile) async {
  await tester.tap(find.descendant(of: tile, matching: find.byIcon(Icons.more_vert)));
  await settle(tester);
}

Future<void> closeMenu(WidgetTester tester) async {
  await tester.tapAt(const Offset(5, 5)); // the barrier
  await settle(tester);
}

void main() {
  // The engine's string rule: the date part cut to its own length against
  // the day cut to the same; anything else after due: is a plain key.
  test('elapsedDueKeys keeps the keys whose day has come', () {
    expect(
        elapsedDueKeys(
            'a due:2020 due:2026-11 due:2026-11-02 due:2026-11-03 due:2027 '
            'due:soon due:2026-1 due:',
            '2026-11-02'),
        ['due:2020', 'due:2026-11', 'due:2026-11-02']);
    expect(elapsedDueKeys('', '2026-11-02'), isEmpty);
  });

  // The group heads Recent, its row shows the due key on the tag line, and
  // the same record keeps its place in its day group below.
  testWidgets('a due record heads Recent under Due and stays in its day',
      (tester) async {
    await pumpPage(tester, due: [rows[0]]);
    expect(find.text('Due'), findsOneWidget);
    expect(dueTile(3), findsOneWidget);
    expect(dayTile(3), findsOneWidget);
    expect(find.descendant(of: dueTile(3), matching: find.textContaining('due:2020')),
        findsOneWidget);
    expect(tester.getTopLeft(find.text('Due')).dy,
        lessThan(tester.getTopLeft(dueTile(3)).dy));
    expect(tester.getTopLeft(dueTile(3)).dy,
        lessThan(tester.getTopLeft(find.text('28 Oct 2026')).dy));
    expect(tester.getTopLeft(find.text('28 Oct 2026')).dy,
        lessThan(tester.getTopLeft(dayTile(3)).dy));
  });

  // Nothing due: no header, no group, Recent as before.
  testWidgets('no group when nothing is due', (tester) async {
    await pumpPage(tester, due: const []);
    expect(find.text('Due'), findsNothing);
    expect(find.textContaining('more due'), findsNothing);
    for (final r in rows) {
      expect(dayTile(r.id), findsOneWidget);
    }
  });

  // Six due rows: five show, the sixth sits behind "1 more due" until tapped.
  testWidgets('the sixth row hides behind N more due and expands in place',
      (tester) async {
    final six = [
      for (var i = 11; i <= 16; i++)
        TlRow(i, '2026-10-20T10:00:00Z', 'due:2020', 'reminder $i'),
    ];
    await pumpPage(tester, due: six);
    for (var i = 11; i <= 15; i++) {
      expect(dueTile(i), findsOneWidget);
    }
    await scrollTo(tester, find.text('1 more due'));
    expect(find.widgetWithText(TextButton, '1 more due'), findsOneWidget);
    expect(dueTile(16), findsNothing);
    await tester.tap(find.text('1 more due'));
    await tester.pump();
    expect(find.text('1 more due'), findsNothing);
    await scrollTo(tester, dueTile(16));
    expect(dueTile(16), findsOneWidget);
    expect(find.descendant(of: dueTile(16), matching: find.text('reminder 16')),
        findsOneWidget);
  });

  // Done and Later sit above Edit tags on a row with an elapsed due key, in
  // the group and in its day; a row without one, or with a future one, has
  // neither.
  testWidgets('Done and Later only on a row with an elapsed due key',
      (tester) async {
    await pumpPage(tester, due: [rows[0]]);
    for (final tile in [dueTile(3), dayTile(3)]) {
      await openMenu(tester, tile);
      expect(find.text('Done'), findsOneWidget);
      expect(find.text('Later'), findsOneWidget);
      double top(String t) => tester.getTopLeft(find.text(t)).dy;
      expect(top('Done'), lessThan(top('Later')));
      expect(top('Later'), lessThan(top('Edit tags')));
      await closeMenu(tester);
    }
    for (final tile in [dayTile(2), dayTile(1)]) {
      await tester.drag(find.byType(ListView), const Offset(0, -300)); // clear of the FAB
      await tester.pump();
      await openMenu(tester, tile);
      expect(find.text('Edit tags'), findsOneWidget);
      expect(find.text('Done'), findsNothing);
      expect(find.text('Later'), findsNothing);
      await closeMenu(tester);
    }
  });

  // Done: the row leaves the group at once, the snackbar offers UNDO, and the
  // detach reaches the seam only when the window lapses.
  testWidgets('Done sends -due after the Undo window', (tester) async {
    final calls = await pumpPage(tester, due: [rows[0]]);
    await openMenu(tester, dueTile(3));
    await tester.tap(find.text('Done'));
    await settle(tester);
    expect(find.text('Due'), findsNothing);
    expect(dueTile(3), findsNothing);
    expect(dayTile(3), findsOneWidget);
    expect(find.widgetWithText(SnackBar, 'Done'), findsOneWidget);
    expect(find.text('UNDO'), findsOneWidget);
    expect(calls.update, isEmpty);
    await tester.pump(const Duration(seconds: 5));
    expect(calls.update, [(3, '-due:2020')]);
    await settle(tester);
    expect(find.text('Due'), findsNothing);
  });

  testWidgets('Undo re-shows the row and sends nothing', (tester) async {
    final calls = await pumpPage(tester, due: [rows[0]]);
    await openMenu(tester, dueTile(3));
    await tester.tap(find.text('Done'));
    await settle(tester);
    expect(dueTile(3), findsNothing);
    await tester.tap(find.text('UNDO'));
    await settle(tester);
    expect(find.text('Due'), findsOneWidget);
    expect(dueTile(3), findsOneWidget);
    await tester.pump(const Duration(seconds: 5));
    expect(calls.update, isEmpty);
    expect(dueTile(3), findsOneWidget);
  });
}
