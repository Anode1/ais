// Value-ranked search: a key search lists the tag matches first, then the
// engine's value matches (find) under one "matched in the value" separator,
// each record once. The page runs with no engine (CI builds no libais.so), so
// RecallPage's recall/find lookups are injected. Bounded pump()s throughout:
// the failed engine open leaves a perpetual spinner, never pumpAndSettle.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/main.dart';

const sep = 'matched in the value';

void main() {
  // Pump the page, type [q] in the header field and sit out the 280ms debounce.
  Future<void> search(WidgetTester tester, RecallPage page, String q) async {
    await tester.pumpWidget(MaterialApp(home: page));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));
    await tester.enterText(find.byType(TextField).first, q);
    await tester.pump(const Duration(milliseconds: 300));
    await tester.pump();
  }

  double top(WidgetTester tester, String text) =>
      tester.getTopLeft(find.text(text)).dy;

  testWidgets('a value-only match appears after the separator', (tester) async {
    await search(
        tester,
        RecallPage(
            recallLookup: (_, __) => const [Hit(1, 'tagged note')],
            findLookup: (_) => const [Hit(2, 'buy milk')]),
        'milk');
    expect(find.text(sep), findsOneWidget);
    expect(find.text('buy milk'), findsOneWidget);
    expect(top(tester, 'tagged note'), lessThan(top(tester, sep)));
    expect(top(tester, 'buy milk'), greaterThan(top(tester, sep)));
  });

  testWidgets('a record matching both ways appears once, before the separator',
      (tester) async {
    await search(
        tester,
        RecallPage(
            recallLookup: (_, __) => const [Hit(1, 'buy milk')],
            findLookup: (_) =>
                const [Hit(1, 'buy milk'), Hit(2, 'oat milk recipe')]),
        'milk');
    expect(find.text('buy milk'), findsOneWidget); // not repeated below
    expect(find.text(sep), findsOneWidget);
    expect(top(tester, 'buy milk'), lessThan(top(tester, sep)));
    expect(top(tester, 'oat milk recipe'), greaterThan(top(tester, sep)));
  });

  testWidgets('values match with no tag match: only the value section',
      (tester) async {
    await search(
        tester,
        RecallPage(
            recallLookup: (_, __) => const [],
            findLookup: (_) => const [Hit(2, 'buy milk')]),
        'milk');
    expect(find.text(sep), findsOneWidget);
    expect(top(tester, 'buy milk'), greaterThan(top(tester, sep)));
  });

  testWidgets('both halves empty keeps the existing empty state',
      (tester) async {
    await search(
        tester,
        RecallPage(
            recallLookup: (_, __) => const [], findLookup: (_) => const []),
        'milk');
    expect(find.text(sep), findsNothing);
    expect(find.text('No tag match for "milk".'), findsOneWidget);
    expect(find.text('Search note text instead'), findsOneWidget);
  });
}
