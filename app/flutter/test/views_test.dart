// Characterisation of the three views as they stand: Search, Recent, Tags,
// the header field, the suggestion row and the overflow menu. Engine-less:
// recall, find, the tag lookup and the cloud are injected and every call is
// recorded, so a change that alters an old behaviour goes red here. The failed
// engine open leaves a spinner, so bounded pump()s, never pumpAndSettle.
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

List<String> lookup(String prefix) => ['recipe', 'record', 'home', 'hotel']
    .where((t) => t.startsWith(prefix.toLowerCase()))
    .toList();

// Every call the page makes through its seams, in order.
class Calls {
  final recall = <(String, bool)>[];
  final find = <String>[];
}

Future<Calls> pumpPage(WidgetTester tester,
    {List<Hit> hits = const [Hit(1, 'a note'), Hit(2, 'another')],
    List<Hit> found = const [],
    List<TagRow> tags = cloud}) async {
  final calls = Calls();
  await tester.pumpWidget(MaterialApp(
      home: RecallPage(
    tagLookup: lookup,
    tagCloud: () => tags,
    recallLookup: (k, or) {
      calls.recall.add((k, or));
      return hits;
    },
    findLookup: (n) {
      calls.find.add(n);
      return found;
    },
  )));
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 50));
  return calls;
}

Finder navLabel(String label) => find.descendant(
    of: find.byType(NavigationBar), matching: find.text(label));

Future<void> openTab(WidgetTester tester, String label) async {
  await tester.tap(navLabel(label));
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 50));
}

int selectedTab(WidgetTester tester) =>
    tester.widget<NavigationBar>(find.byType(NavigationBar)).selectedIndex;

Finder get field => find.byType(TextField).first;

Future<void> type(WidgetTester tester, String text) async {
  await tester.enterText(field, text);
  await tester.pump(const Duration(milliseconds: 300));
  await tester.pump();
}

String fieldText(WidgetTester tester) =>
    tester.widget<TextField>(field).controller!.text;

void main() {
  group('Search', () {
    // Nothing runs inside the 280ms debounce; after it the count, the rows
    // and the Search tab show, from any view.
    testWidgets('typing searches after the debounce and brings Search forward',
        (tester) async {
      final calls = await pumpPage(tester);
      expect(selectedTab(tester), 1); // opens on Recent
      await tester.enterText(field, 'recipe');
      await tester.pump(const Duration(milliseconds: 200));
      expect(calls.recall, isEmpty);
      expect(find.text('2 results'), findsNothing);
      await tester.pump(const Duration(milliseconds: 100));
      await tester.pump();
      expect(calls.recall, [('recipe', false)]);
      expect(selectedTab(tester), 0);
      expect(find.text('2 results'), findsOneWidget);
      expect(find.widgetWithText(ListTile, 'a note'), findsOneWidget);
      expect(find.widgetWithText(ListTile, 'another'), findsOneWidget);
    });

    // recall gets the keys with commas and runs of spaces folded to one
    // space; find gets the field text only trimmed.
    testWidgets('recall gets normalised keys, find the trimmed text',
        (tester) async {
      final calls = await pumpPage(tester);
      await type(tester, '  recipe,  chicken ');
      expect(calls.recall.last, ('recipe chicken', false));
      expect(calls.find.last, 'recipe,  chicken');
    });

    // One hit reads "1 result", singular.
    testWidgets('one hit reads 1 result', (tester) async {
      await pumpPage(tester, hits: const [Hit(1, 'a note')]);
      await type(tester, 'recipe');
      expect(find.text('1 result'), findsOneWidget);
    });

    // A row's subtitle comes from the engine's keysOf, which tagLookup does
    // not stand in for: engine-less every row reads "(no tags)".
    testWidgets('a row without engine keys reads (no tags)', (tester) async {
      await pumpPage(tester);
      await type(tester, 'recipe');
      expect(find.descendant(
              of: find.widgetWithText(ListTile, 'a note'),
              matching: find.text('(no tags)')),
          findsOneWidget);
    });

    // A record hit once per link shows one row, with the further links counted.
    testWidgets('a record with several links shows once with +N more links',
        (tester) async {
      await pumpPage(tester, hits: const [
        Hit(1, 'a note'),
        Hit(1, 'a note'),
        Hit(2, 'another'),
        Hit(2, 'another'),
        Hit(2, 'another'),
      ]);
      await type(tester, 'recipe');
      expect(find.text('2 results'), findsOneWidget);
      expect(find.text('(no tags)  ·  +1 more link'), findsOneWidget);
      expect(find.text('(no tags)  ·  +2 more links'), findsOneWidget);
    });

    // Enter runs the search at once, without waiting for the debounce.
    testWidgets('Enter searches at once', (tester) async {
      final calls = await pumpPage(tester);
      await tester.enterText(field, 'recipe');
      await tester.testTextInput.receiveAction(TextInputAction.search);
      await tester.pump();
      expect(calls.recall.first, ('recipe', false));
      expect(find.text('2 results'), findsOneWidget);
      await tester.pump(const Duration(milliseconds: 300)); // drain the debounce
    });

    // Clearing the field drops the rows and the count and shows the hint,
    // without a search.
    testWidgets('clearing the field shows the hint and no rows',
        (tester) async {
      final calls = await pumpPage(tester);
      await type(tester, 'recipe');
      final n = calls.recall.length;
      await type(tester, '');
      expect(calls.recall.length, n);
      expect(find.text('Type tags, then Search.'), findsOneWidget);
      expect(find.text('2 results'), findsNothing);
      expect(find.text('a note'), findsNothing);
      expect(selectedTab(tester), 0);
    });

    // Opening Search with an empty field shows the hint and searches nothing.
    testWidgets('Search with an empty field shows the hint', (tester) async {
      final calls = await pumpPage(tester);
      await openTab(tester, 'Search');
      expect(find.text('Type tags, then Search.'), findsOneWidget);
      expect(calls.recall, isEmpty);
    });

    // Text of only separators normalises to no keys: no recall runs.
    testWidgets('a query of only commas does not search', (tester) async {
      final calls = await pumpPage(tester);
      await openTab(tester, 'Search');
      await type(tester, ', ,');
      expect(calls.recall, isEmpty);
      expect(find.text('Type tags, then Search.'), findsOneWidget);
    });

    // AND is the default; the Any tag chip re-runs the search at once in OR
    // mode, and a second tap returns to AND.
    testWidgets('Any tag switches recall to OR and back', (tester) async {
      final calls = await pumpPage(tester);
      await type(tester, 'recipe home');
      expect(calls.recall.last, ('recipe home', false));
      final chip = find.widgetWithText(FilterChip, 'Any tag');
      expect(tester.widget<FilterChip>(chip).selected, isFalse);
      await tester.tap(chip);
      await tester.pump();
      expect(calls.recall.last, ('recipe home', true));
      expect(tester.widget<FilterChip>(chip).selected, isTrue);
      await tester.tap(chip);
      await tester.pump();
      expect(calls.recall.last, ('recipe home', false));
    });

    // OR mode stays on for the next typed search.
    testWidgets('Any tag holds for the next search', (tester) async {
      final calls = await pumpPage(tester);
      await openTab(tester, 'Search');
      await tester.tap(find.widgetWithText(FilterChip, 'Any tag'));
      await tester.pump();
      expect(calls.recall, isEmpty); // nothing typed: nothing to re-run
      await type(tester, 'home');
      expect(calls.recall.last, ('home', true));
    });

    // A suggestion tap completes the token and searches the completed text
    // at once, from Recent too.
    testWidgets('a suggestion tap completes and searches', (tester) async {
      final calls = await pumpPage(tester);
      await tester.enterText(field, 'home rec');
      await tester.pump();
      await tester.tap(find.widgetWithText(ActionChip, 'recipe'));
      await tester.pump();
      expect(fieldText(tester), 'home recipe ');
      expect(calls.recall.single, ('home recipe', false));
      expect(selectedTab(tester), 0);
      expect(find.text('2 results'), findsOneWidget);
      await tester.pump(const Duration(milliseconds: 300));
      expect(calls.recall.length, 1); // the debounce was cancelled
    });

    // The count belongs to Search: Recent and Tags never show it, and
    // returning to Search re-runs the query in the field.
    testWidgets('the count shows on Search only and returns with it',
        (tester) async {
      final calls = await pumpPage(tester);
      await type(tester, 'recipe');
      await openTab(tester, 'Recent');
      expect(find.text('2 results'), findsNothing);
      await openTab(tester, 'Tags');
      expect(find.text('2 results'), findsNothing);
      final n = calls.recall.length;
      await openTab(tester, 'Search');
      expect(calls.recall.length, n + 1);
      expect(calls.recall.last, ('recipe', false));
      expect(find.text('2 results'), findsOneWidget);
    });
  });

  group('Tags', () {
    // The cloud shows as given, top to bottom, each tag with its count.
    testWidgets('the cloud lists every tag with its count in order',
        (tester) async {
      await pumpPage(tester);
      await openTab(tester, 'Tags');
      double top(String k) =>
          tester.getTopLeft(find.widgetWithText(ListTile, k)).dy;
      for (var i = 0; i < cloud.length; i++) {
        final row = find.widgetWithText(ListTile, cloud[i].key);
        expect(find.descendant(of: row, matching: find.text('${cloud[i].count}')),
            findsOneWidget);
        if (i > 0) expect(top(cloud[i - 1].key), lessThan(top(cloud[i].key)));
      }
    });

    // An empty cloud says so.
    testWidgets('an empty cloud reads No tags yet.', (tester) async {
      await pumpPage(tester, tags: const []);
      await openTab(tester, 'Tags');
      expect(find.text('No tags yet.'), findsOneWidget);
    });

    // A tap puts the tag in the field, opens Search and runs it.
    testWidgets('tapping a tag searches it', (tester) async {
      final calls = await pumpPage(tester);
      await openTab(tester, 'Tags');
      await tester.tap(find.widgetWithText(ListTile, 'Record'));
      await tester.pump();
      expect(fieldText(tester), 'Record');
      expect(selectedTab(tester), 0);
      expect(calls.recall.single, ('Record', false));
      expect(find.text('2 results'), findsOneWidget);
    });

    // The Tags view offers no suggestion chips: its list is the completions.
    testWidgets('typing on Tags shows no suggestion row', (tester) async {
      final calls = await pumpPage(tester);
      await openTab(tester, 'Tags');
      await type(tester, 'rec');
      expect(find.byType(ActionChip), findsNothing);
      expect(calls.recall, isEmpty);
    });

    // A full first page (100) means more may exist: a loading row ends the
    // list. A shorter cloud ends on its last tag.
    testWidgets('a full page of 100 tags ends in a loading row',
        (tester) async {
      final big = [for (var i = 0; i < 100; i++) TagRow('t$i', 100 - i)];
      await pumpPage(tester, tags: big);
      await openTab(tester, 'Tags');
      await tester.dragUntilVisible(find.byType(CircularProgressIndicator),
          find.byType(ListView), const Offset(0, -500));
      expect(find.text('t99'), findsOneWidget);
    });

    testWidgets('99 tags end on the last tag, no loading row', (tester) async {
      final big = [for (var i = 0; i < 99; i++) TagRow('t$i', 100 - i)];
      await pumpPage(tester, tags: big);
      await openTab(tester, 'Tags');
      await tester.dragUntilVisible(
          find.text('t98'), find.byType(ListView), const Offset(0, -500));
      await tester.drag(find.byType(ListView), const Offset(0, -2000));
      await tester.pump();
      expect(find.text('t98'), findsOneWidget);
      expect(find.byType(CircularProgressIndicator), findsNothing);
    });
  });

  group('Views and menu', () {
    // The bar reads Search, Recent, Tags; the page opens on Recent and each
    // tap selects its destination.
    testWidgets('the bottom bar switches among the three views',
        (tester) async {
      await pumpPage(tester);
      final labels = tester
          .widgetList<NavigationDestination>(find.byType(NavigationDestination))
          .map((d) => d.label)
          .toList();
      expect(labels, ['Search', 'Recent', 'Tags']);
      expect(selectedTab(tester), 1);
      await openTab(tester, 'Tags');
      expect(selectedTab(tester), 2);
      expect(find.widgetWithText(ListTile, 'recipe'), findsOneWidget);
      await openTab(tester, 'Search');
      expect(selectedTab(tester), 0);
      expect(find.text('Type tags, then Search.'), findsOneWidget);
      await openTab(tester, 'Recent');
      expect(selectedTab(tester), 1);
    });

    // The field's hint names what it takes.
    testWidgets('the search field hint', (tester) async {
      await pumpPage(tester);
      expect(tester.widget<TextField>(field).decoration!.hintText,
          'type tags to filter');
    });

    // With no engine the Add button is shown but disabled.
    testWidgets('Add is disabled with no engine', (tester) async {
      await pumpPage(tester);
      final fab = find.widgetWithText(FloatingActionButton, 'Add');
      expect(fab, findsOneWidget);
      expect(tester.widget<FloatingActionButton>(fab).onPressed, isNull);
    });

    // Seven entries in this order; the four that need an open library are
    // disabled without one, Theme, Help and About are not.
    testWidgets('the overflow menu: order and what needs the engine',
        (tester) async {
      await pumpPage(tester);
      await tester.tap(find.byTooltip('Settings'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      final items = tester
          .widgetList<PopupMenuItem<String>>(
              find.byType(PopupMenuItem<String>))
          .toList();
      expect(items.map((i) => i.value).toList(),
          ['store', 'sync', 'links', 'cleanup', 'theme', 'help', 'about']);
      expect(items.map((i) => i.enabled).toList(),
          [false, false, false, false, true, true, true]);
      for (final t in [
        'Change Library',
        'Sync & backup',
        'Import bookmarks',
        'Free up space',
        'Theme',
        'Help',
        'About',
      ]) {
        expect(find.text(t), findsOneWidget);
      }
      expect(find.text('Not backed up yet'), findsOneWidget);
      expect(find.text("Pocket, Instapaper, Raindrop, or a browser's bookmarks file"),
          findsOneWidget);
      double top(String t) => tester.getTopLeft(find.text(t)).dy;
      expect(top('Change Library'), lessThan(top('Sync & backup')));
      expect(top('Free up space'), lessThan(top('Theme')));
      expect(top('Help'), lessThan(top('About')));
    });

    // Theme opens a three-way choice with the current mode ticked.
    testWidgets('Theme offers System, Light, Dark', (tester) async {
      await pumpPage(tester);
      await tester.tap(find.byTooltip('Settings'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      await tester.tap(find.text('Theme'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      for (final t in ['System', 'Light', 'Dark']) {
        expect(find.widgetWithText(ListTile, t), findsOneWidget);
      }
      final ticked = themeModeNotifier.value == ThemeMode.system
          ? 'System'
          : themeModeNotifier.value == ThemeMode.light
              ? 'Light'
              : 'Dark';
      expect(
          find.descendant(
              of: find.widgetWithText(ListTile, ticked),
              matching: find.byIcon(Icons.check)),
          findsOneWidget);
      expect(find.byIcon(Icons.check), findsOneWidget);
    });

    // About names the app and, with no library loaded, an unknown engine and
    // the default library.
    testWidgets('About with no engine', (tester) async {
      await pumpPage(tester);
      await tester.tap(find.byTooltip('Settings'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      await tester.tap(find.text('About'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      expect(find.textContaining('engine: unknown'), findsOneWidget);
      expect(find.text('Library: (default)'), findsOneWidget);
      expect(find.text('Free software: GPL 2 or later, or MIT.'), findsOneWidget);
      expect(find.text('Source code'), findsOneWidget);
    });

    // The engine gate promises the real error and Retry when the open FAILS
    // (main.dart _engineGate). Here the open fails (no libais), so Recent must
    // not spin forever.
    testWidgets('a failed engine open shows the error and Retry',
        (tester) async {
      await pumpPage(tester);
      await tester.pump(const Duration(seconds: 1));
      expect(find.widgetWithText(FilledButton, 'Retry'), findsOneWidget);
    });
  });
}
