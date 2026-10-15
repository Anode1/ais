// The header's filter row, the Tags view's prefix filter and the results
// count. Engine-less: recall, find and the tag cloud are injected, and the
// failed engine open leaves a spinner, so bounded pump()s, never pumpAndSettle.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/main.dart';

const cloud = [
  TagRow('recipe', 9),
  TagRow('Record', 4),
  TagRow('home', 3),
  TagRow('hotel', 1),
];

Future<void> pumpPage(WidgetTester tester) async {
  await tester.pumpWidget(MaterialApp(
      home: RecallPage(
    tagCloud: () => cloud,
    recallLookup: (_, __) => const [Hit(1, 'a note'), Hit(2, 'another')],
    findLookup: (_) => const [],
  )));
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 50));
}

Future<void> openTab(WidgetTester tester, String label) async {
  await tester.tap(find.descendant(
      of: find.byType(NavigationBar), matching: find.text(label)));
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 50));
}

Future<void> type(WidgetTester tester, String text) async {
  await tester.enterText(find.byType(TextField).first, text);
  await tester.pump(const Duration(milliseconds: 300));
  await tester.pump();
}

void main() {
  // Each chip shows only on the view it acts on.
  testWidgets('Recent shows Date, Search shows Any tag, Tags shows neither',
      (tester) async {
    await pumpPage(tester);
    expect(find.widgetWithText(FilterChip, 'Date'), findsOneWidget);
    expect(find.widgetWithText(FilterChip, 'Any tag'), findsNothing);

    await openTab(tester, 'Search');
    expect(find.widgetWithText(FilterChip, 'Any tag'), findsOneWidget);
    expect(find.widgetWithText(FilterChip, 'Date'), findsNothing);

    await openTab(tester, 'Tags');
    expect(find.byType(FilterChip), findsNothing);
  });

  // The chip faces are 34dp tall, sit 16dp under the search field, and keep
  // the 48dp tap target both platforms ask for.
  testWidgets('the filter row sits 16dp under the search bar', (tester) async {
    await pumpPage(tester);
    final field = tester.getRect(find.byType(TextField).first);
    final chip = find.widgetWithText(FilterChip, 'Date');
    final chipBox = tester.getRect(find.descendant(
        of: chip, matching: find.byType(Material)).first);
    expect(chipBox.height, 34);
    expect(chipBox.top - field.bottom, 16);
    expect(tester.getSize(chip).height, 48); // the tap target
  });

  testWidgets('the results header shows the count alone', (tester) async {
    await pumpPage(tester);
    await type(tester, 'note');
    expect(find.text('2 results'), findsOneWidget);
    expect(find.textContaining(' ms'), findsNothing);
  });

  // Typing on the Tags view narrows the list by prefix, case-insensitive,
  // and stays on Tags instead of jumping to Search.
  testWidgets('the Tags view filters by the search text', (tester) async {
    await pumpPage(tester);
    await openTab(tester, 'Tags');
    for (final t in cloud) {
      expect(find.text(t.key), findsOneWidget);
    }
    await type(tester, 'REC');
    expect(find.widgetWithText(ListTile, 'recipe'), findsOneWidget);
    expect(find.widgetWithText(ListTile, 'Record'), findsOneWidget);
    expect(find.widgetWithText(ListTile, 'home'), findsNothing);
    expect(find.widgetWithText(ListTile, 'hotel'), findsNothing);
    expect(find.text('2 results'), findsNothing); // still on Tags

    await type(tester, 'ho');
    expect(find.widgetWithText(ListTile, 'home'), findsOneWidget);
    expect(find.widgetWithText(ListTile, 'hotel'), findsOneWidget);
    expect(find.widgetWithText(ListTile, 'recipe'), findsNothing);

    await type(tester, 'xyz');
    expect(find.text('No tag starts with "xyz".'), findsOneWidget);

    await type(tester, '');
    expect(find.byType(ListTile), findsNWidgets(cloud.length));
  });

  // One-line 56dp rows, the count trailing, no dividers between them.
  testWidgets('tag rows are 56dp with the count trailing', (tester) async {
    await pumpPage(tester);
    await openTab(tester, 'Tags');
    final row = find.widgetWithText(ListTile, 'recipe');
    expect(tester.getSize(row).height, 56);
    expect(find.descendant(of: row, matching: find.text('9')), findsOneWidget);
    expect(find.byType(Divider), findsNothing);
  });

  group('dateRangeLabel', () {
    final now = DateTime(2026, 10, 14);
    test('within a month', () {
      expect(dateRangeLabel('2026-08-12', '2026-08-20', now: now), '12–20 Aug');
    });
    test('one day', () {
      expect(dateRangeLabel('2026-08-12', '2026-08-12', now: now), '12 Aug');
    });
    test('across months', () {
      expect(dateRangeLabel('2026-07-28', '2026-08-03', now: now),
          '28 Jul – 3 Aug');
    });
    test('another year names it', () {
      expect(dateRangeLabel('2025-08-12', '2025-08-20', now: now),
          '12–20 Aug 2025');
    });
    test('across years names both', () {
      expect(dateRangeLabel('2025-12-28', '2026-01-03', now: now),
          '28 Dec 2025 – 3 Jan 2026');
    });
  });
}
