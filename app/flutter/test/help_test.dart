// The Help page: reachable from the overflow menu and from the empty start
// screen, its links open the right URLs, and every control its text names
// still exists under that label in main.dart.
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/help.dart';
import 'package:ais/main.dart';
import 'package:ais/survival.dart';

void main() {
  late Directory tmp;
  setUp(() => tmp = Directory.systemTemp.createTempSync('ais_help'));
  tearDown(() => tmp.deleteSync(recursive: true));

  // Default: a phone's portrait viewport. short: the 800x600 test surface,
  // where the empty state's three actions do not fit.
  Future<void> pumpEmptyApp(WidgetTester tester, {bool short = false}) async {
    if (!short) {
      tester.view.physicalSize = const Size(1080, 2400);
      tester.view.devicePixelRatio = 2.625;
      addTearDown(tester.view.reset);
    }
    await tester.pumpWidget(MaterialApp(
        home: RecallPage(
            survival: SurvivalHarness(
      dir: tmp.path,
      timeline: () => const [],
      countLive: () => 0,
      store: (_, __) => 1,
      pickAndRestore: () async => null,
    ))));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));
  }

  // Each section title is on the page, and each link hands its URL to open().
  testWidgets('the page shows every section and opens its links',
      (tester) async {
    final opened = <String>[];
    await tester.pumpWidget(MaterialApp(home: HelpPage(open: opened.add)));
    for (final (title, _) in helpSections) {
      await tester.scrollUntilVisible(find.text(title), 200);
      expect(find.text(title), findsOneWidget);
    }
    for (final label in [
      'Report a problem',
      'Source code and documentation',
      'Privacy policy'
    ]) {
      await tester.scrollUntilVisible(find.text(label), 200);
      await tester.tap(find.text(label));
    }
    expect(opened, [kIssuesUrl, kSourceUrl, kPrivacyUrl]);
  });

  // A fresh install shows "How it works" next to Add, and it opens Help.
  testWidgets('the empty start screen opens Help', (tester) async {
    await pumpEmptyApp(tester);
    await tester.tap(find.text('How it works'));
    await tester.pumpAndSettle();
    expect(find.byType(HelpPage), findsOneWidget);
  });

  // On a screen too short to show it, "How it works" is reached by scrolling.
  testWidgets('a short screen scrolls to How it works', (tester) async {
    await pumpEmptyApp(tester, short: true);
    await tester.ensureVisible(find.text('How it works'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('How it works'));
    await tester.pumpAndSettle();
    expect(find.byType(HelpPage), findsOneWidget);
  });

  // The overflow menu carries Help, and it opens the same page.
  testWidgets('the overflow menu opens Help', (tester) async {
    await pumpEmptyApp(tester);
    await tester.tap(find.byIcon(Icons.more_vert));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Help'));
    await tester.pumpAndSettle();
    expect(find.byType(HelpPage), findsOneWidget);
  });

  // The text names controls by their label; a renamed control makes it a lie.
  test('every control the help names is a label in main.dart', () {
    final src = File('lib/main.dart').readAsStringSync();
    final text = helpSections.map((s) => s.$2).join(' ');
    for (final label in [
      'Match any tag',
      'Search note text instead',
      'Encrypt',
      'Sync & backup',
      'Keep a copy in a folder',
      'Restore from a folder',
      'Host a sync',
      'Recent',
      'Tags',
    ]) {
      expect(text, contains(label), reason: 'help no longer names $label');
      expect(src, contains("'$label'"), reason: 'main.dart lost $label');
    }
  });
}
