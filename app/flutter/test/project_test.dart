// The project chip (doc/dev/project-chip/SPEC.md): one tag the app keeps
// active, chosen from the Tags view. Recent lists only its items, a search is
// ANDed with it, the Add sheet starts with it. Engine-less: the seams are
// injected and recorded, and with no library dir the project lives in memory
// for the test. Bounded pump()s, never pumpAndSettle.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/main.dart';

const cloud = [TagRow('recipe', 9), TagRow('home', 3), TagRow('hotel', 1)];
const rows = [
  TlRow(1, '2026-10-28T10:00:00Z', 'recipe home', 'a note'),
  TlRow(2, '2026-10-27T10:00:00Z', 'hotel', 'another'),
];

class Calls {
  final recall = <(String, bool)>[];
  final timeline = <String>[];
}

Future<Calls> pumpPage(WidgetTester tester) async {
  final calls = Calls();
  await tester.pumpWidget(MaterialApp(
      home: RecallPage(
    tagLookup: (_) => const [],
    tagCloud: () => cloud,
    recallLookup: (k, or) {
      calls.recall.add((k, or));
      return const [Hit(1, 'a note'), Hit(2, 'another')];
    },
    findLookup: (_) => const [],
    timelineLookup: (key) {
      calls.timeline.add(key);
      return rows
          .where((r) => key.isEmpty || (!rowsGone && r.keys.split(' ').contains(key)))
          .toList();
    },
  )));
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 50));
  return calls;
}

// Long enough for a menu or a snackbar to finish opening.
Future<void> settle(WidgetTester tester) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 400));
}

Future<void> openTab(WidgetTester tester, String label) async {
  await tester.tap(find.descendant(
      of: find.byType(NavigationBar), matching: find.text(label)));
  await settle(tester);
}

int selectedTab(WidgetTester tester) =>
    tester.widget<NavigationBar>(find.byType(NavigationBar)).selectedIndex;

Finder tile(String key) => find.widgetWithText(ListTile, key);

// The row's menu, then its first item.
Future<void> rowMenu(WidgetTester tester, String key, String item) async {
  await tester.tap(find.descendant(
      of: tile(key), matching: find.byIcon(Icons.more_vert)));
  await settle(tester);
  await tester.tap(find.text(item));
  await settle(tester);
}

Future<void> choose(WidgetTester tester, String key) async {
  await openTab(tester, 'Tags');
  await rowMenu(tester, key, 'Make it the project');
}

Finder chip(String key) => find.widgetWithText(InputChip, key);

Future<void> type(WidgetTester tester, String text) async {
  await tester.enterText(find.byType(TextField).first, text);
  await tester.pump(const Duration(milliseconds: 300));
  await tester.pump();
}

void main() {
  group('choosing', () {
    testWidgets('a row menu makes the tag the project: chip, row first, once a notice',
        (tester) async {
      await pumpPage(tester);
      await openTab(tester, 'Tags');
      expect(find.byType(InputChip), findsNothing);
      await rowMenu(tester, 'home', 'Make it the project');
      expect(chip('home'), findsOneWidget);
      expect(find.text('Working in home on this device. New items get this tag.'),
          findsOneWidget);
      final first = tester.widget<ListTile>(find.byType(ListTile).first);
      expect((first.title as Text).data, 'home');
      expect(tester.widget<TextField>(find.byType(TextField).first).decoration!.hintText,
          'more tags');
      await tester.tap(find.descendant(
          of: tile('home'), matching: find.byIcon(Icons.more_vert)));
      await settle(tester);
      expect(find.text('Leave project'), findsOneWidget);
      expect(find.text('Make it the project'), findsNothing);
    });
  });

  group('the views inside a project', () {
    testWidgets('Recent asks for the project page and omits the project tag',
        (tester) async {
      final calls = await pumpPage(tester);
      await choose(tester, 'recipe');
      await openTab(tester, 'Recent');
      expect(calls.timeline.last, 'recipe');
      expect(tile('a note'), findsOneWidget);
      expect(tile('another'), findsNothing);
      expect(find.descendant(of: tile('a note'), matching: find.textContaining('home')),
          findsOneWidget);
      expect(find.descendant(of: tile('a note'), matching: find.textContaining('recipe')),
          findsNothing);
    });

    testWidgets('a search is ANDed with the project; Any tag ORs the typed tags only',
        (tester) async {
      final calls = await pumpPage(tester);
      await choose(tester, 'recipe');
      await openTab(tester, 'Search');
      await type(tester, 'hotel');
      expect(calls.recall.last, ('recipe hotel', false));
      await tester.tap(find.widgetWithText(FilterChip, 'Any tag'));
      await settle(tester);
      expect(calls.recall.last, ('hotel', true));
    });

    testWidgets('the results end with the scope; Search all items runs it everywhere once',
        (tester) async {
      final calls = await pumpPage(tester);
      await choose(tester, 'recipe');
      await openTab(tester, 'Search');
      await type(tester, 'hotel');
      expect(find.textContaining('Only items tagged recipe'), findsOneWidget);
      await tester.tap(find.text('Search all items'));
      await settle(tester);
      expect(calls.recall.last, ('hotel', false));
      expect(chip('recipe'), findsOneWidget);
      expect(find.textContaining('Only items tagged'), findsNothing);
      await type(tester, 'home');
      expect(calls.recall.last, ('recipe home', false));
    });

    testWidgets('the empty search names the project, the welcome card stays away',
        (tester) async {
      await pumpPage(tester);
      await choose(tester, 'recipe');
      await openTab(tester, 'Search');
      expect(find.text('Type tags to search inside recipe.'), findsOneWidget);
      expect(find.textContaining('Save a link, a note'), findsNothing);
      expect(find.widgetWithText(FloatingActionButton, 'Add'), findsOneWidget);
    });

    testWidgets('the first-project notice replaces a queued snackbar', (tester) async {
      await pumpPage(tester);
      final m = ScaffoldMessenger.of(tester.element(find.byType(NavigationBar)));
      m.showSnackBar(const SnackBar(content: Text('Saved under: home')));
      m.showSnackBar(const SnackBar(content: Text('Saved under: trip')));
      await settle(tester);
      await choose(tester, 'recipe');
      expect(find.text('Working in recipe on this device. New items get this tag.'),
          findsOneWidget);
      expect(find.textContaining('Saved under'), findsNothing);
    });

    testWidgets('Recent coming back empty ends the project', (tester) async {
      final calls = await pumpPage(tester);
      await choose(tester, 'hotel');
      await openTab(tester, 'Recent');
      expect(tile('another'), findsOneWidget);
      // the project's one item is gone: the next load finds nothing under it
      rowsGone = true;
      await openTab(tester, 'Tags');
      await openTab(tester, 'Recent');
      rowsGone = false;
      expect(find.text('Nothing left in hotel. The project ended.'), findsOneWidget);
      expect(find.byType(InputChip), findsNothing);
      expect(calls.timeline.last, '');
    });
  });

  group('leaving', () {
    testWidgets("the chip's cross leaves the project, Undo brings it back",
        (tester) async {
      await pumpPage(tester);
      await choose(tester, 'recipe');
      await tester.tap(find.byTooltip('Leave project'));
      await settle(tester);
      expect(find.byType(InputChip), findsNothing);
      expect(find.text('Left recipe'), findsOneWidget);
      await tester.tap(find.text('UNDO'));
      await settle(tester);
      expect(chip('recipe'), findsOneWidget);
    });

    testWidgets("the chip's name opens its menu; Switch project opens Tags",
        (tester) async {
      await pumpPage(tester);
      await choose(tester, 'recipe');
      await openTab(tester, 'Recent');
      await tester.tap(find.descendant(of: chip('recipe'), matching: find.text('recipe')));
      await settle(tester);
      expect(find.text('Switch project'), findsOneWidget);
      expect(find.text('Leave project'), findsOneWidget);
      expect(find.text('Kept on this device'), findsOneWidget);
      await tester.tap(find.text('Switch project'));
      await settle(tester);
      expect(selectedTab(tester), 2);
      expect(chip('recipe'), findsOneWidget);
    });
  });

  group('the Add sheet', () {
    final saved = <String>[];
    Future<String?> onSave(
        {required String value,
        required String keys,
        required bool encrypt,
        required String passphrase,
        required String repeat}) async {
      saved.add(keys);
      return null;
    }

    Future<void> openSheet(WidgetTester tester) async {
      saved.clear();
      await tester.pumpWidget(MaterialApp(
          home: Scaffold(
              body: Builder(
                  builder: (ctx) => TextButton(
                      onPressed: () => showModalBottomSheet(
                          context: ctx,
                          isScrollControlled: true,
                          builder: (_) => AddSheet(
                              project: 'recipe',
                              suggest: (_) => const [],
                              onSave: onSave)),
                      child: const Text('open'))))));
      await tester.tap(find.text('open'));
      await tester.pumpAndSettle();
    }

    Future<void> save(WidgetTester tester, String tags) async {
      await tester.enterText(find.widgetWithText(TextField, 'What to remember'), 'x');
      await tester.enterText(find.widgetWithText(TextField, 'Tags'), tags);
      await tester.pump();
      await tester.tap(find.text('Save'));
      await tester.pumpAndSettle();
    }

    testWidgets('the chip leads the tag field and is saved as a tag', (tester) async {
      await openSheet(tester);
      expect(chip('recipe'), findsOneWidget);
      await save(tester, 'alpha');
      expect(saved, ['recipe alpha']);
    });

    testWidgets('typed by hand it counts once', (tester) async {
      await openSheet(tester);
      await save(tester, 'recipe beta');
      expect(saved, ['recipe beta']);
    });

    testWidgets('its cross leaves this one item out', (tester) async {
      await openSheet(tester);
      await tester.tap(find.byTooltip('Leave this item out of recipe'));
      await tester.pump();
      expect(find.byType(InputChip), findsNothing);
      await save(tester, 'gamma');
      expect(saved, ['gamma']);
    });
  });
}

// Flipped by the "Recent coming back empty" test: the lookup then has nothing
// under any project.
bool rowsGone = false;
