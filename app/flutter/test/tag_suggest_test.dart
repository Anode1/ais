// Tag autocomplete: ONE widget (TagSuggestRow) serves the Add sheet, the
// Edit-tags dialog and the header search field -- three hand-rolled rows is
// how they drift apart. Each site is pumped here with an injected lookup:
// `flutter test` runs with no engine, so the real tag cloud is out of reach.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/main.dart';

// The fake tag cloud, busiest first, prefix-matched like the real _tagMatches.
List<String> lookup(String prefix) => ['recipe', 'record', 'home', 'hotel']
    .where((t) => t.startsWith(prefix.toLowerCase()))
    .toList();

Future<String?> saveNever(
        {required String value,
        required String keys,
        required bool encrypt,
        required String passphrase,
        required String repeat}) async =>
    fail('the suggestion tests never save');

void main() {
  group('TagSuggestRow, standalone', () {
    Future<TextEditingController> pumpRow(WidgetTester tester,
        {String text = ''}) async {
      final ctrl = TextEditingController(text: text);
      addTearDown(ctrl.dispose);
      await tester.pumpWidget(MaterialApp(
          home: Scaffold(
              body: TagSuggestRow(controller: ctrl, lookup: lookup))));
      return ctrl;
    }

    testWidgets('an empty token shows nothing', (tester) async {
      await pumpRow(tester);
      expect(find.byType(ActionChip), findsNothing);
    });

    testWidgets('a trailing space (token just completed) shows nothing',
        (tester) async {
      await pumpRow(tester, text: 'rec ');
      expect(find.byType(ActionChip), findsNothing);
    });

    testWidgets('the TRAILING token is matched, commas split like spaces',
        (tester) async {
      await pumpRow(tester, text: 'home,rec');
      expect(find.widgetWithText(ActionChip, 'recipe'), findsOneWidget);
      expect(find.widgetWithText(ActionChip, 'record'), findsOneWidget);
      expect(find.widgetWithText(ActionChip, 'hotel'), findsNothing);
    });

    testWidgets('a tap completes the token and appends a trailing space',
        (tester) async {
      final ctrl = await pumpRow(tester, text: 'home rec');
      await tester.tap(find.widgetWithText(ActionChip, 'recipe'));
      await tester.pump();
      expect(ctrl.text, 'home recipe ');
      expect(ctrl.selection.baseOffset, ctrl.text.length);
      // the completed token leaves an empty trailing token: chips gone
      expect(find.byType(ActionChip), findsNothing);
    });

    testWidgets('matching is case-insensitive', (tester) async {
      await pumpRow(tester, text: 'REC');
      expect(find.widgetWithText(ActionChip, 'recipe'), findsOneWidget);
    });
  });

  group('site: the Add sheet tags field', () {
    testWidgets('chips appear for the prefix and a tap completes it',
        (tester) async {
      await tester.pumpWidget(const MaterialApp(
          home: Scaffold(
              body: AddSheet(
                  initialKeys: 'food ', suggest: lookup, onSave: saveNever))));
      await tester.enterText(
          find.widgetWithText(TextField, 'Tags (space-separated, optional)'),
          'food rec');
      await tester.pump();
      expect(find.widgetWithText(ActionChip, 'recipe'), findsOneWidget);
      await tester.tap(find.widgetWithText(ActionChip, 'recipe'));
      await tester.pump();
      final field = tester.widget<TextField>(find.widgetWithText(
          TextField, 'Tags (space-separated, optional)'));
      expect(field.controller!.text, 'food recipe ');
      expect(find.byType(ActionChip), findsNothing);
    });
  });

  group('site: the Edit-tags dialog', () {
    testWidgets('a tap commits the tag straight to a chip; Apply returns it',
        (tester) async {
      List<String>? result;
      await tester.pumpWidget(MaterialApp(
          home: Scaffold(
              body: Builder(
                  builder: (ctx) => TextButton(
                      onPressed: () async {
                        result = await showDialog<List<String>>(
                            context: ctx,
                            builder: (_) => const EditTagsDialog(
                                initial: ['home'], suggest: lookup));
                      },
                      child: const Text('open'))))));
      await tester.tap(find.text('open'));
      await tester.pumpAndSettle();

      // an already-chipped tag is not re-offered
      await tester.enterText(find.byType(TextField), 'ho');
      await tester.pump();
      expect(find.widgetWithText(ActionChip, 'hotel'), findsOneWidget);
      expect(find.widgetWithText(ActionChip, 'home'), findsNothing);

      await tester.enterText(find.byType(TextField), 'rec');
      await tester.pump();
      await tester.tap(find.widgetWithText(ActionChip, 'recipe'));
      await tester.pump();
      // committed as a chip, not left in the field
      expect(find.widgetWithText(InputChip, 'recipe'), findsOneWidget);
      expect(tester.widget<TextField>(find.byType(TextField)).controller!.text,
          isEmpty);

      await tester.tap(find.text('Apply'));
      await tester.pumpAndSettle();
      expect(result, ['home', 'recipe']);
    });
  });

  group('site: the header search field', () {
    // RecallPage runs with no engine here (its open fails and the body shows a
    // perpetual spinner), so bounded pump()s, never pumpAndSettle.
    testWidgets('chips appear under the search field and a tap completes',
        (tester) async {
      await tester.pumpWidget(
          const MaterialApp(home: RecallPage(tagLookup: lookup)));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 50));

      final search = find.byType(TextField).first;
      await tester.enterText(search, 'rec');
      await tester.pump();
      expect(find.widgetWithText(ActionChip, 'recipe'), findsOneWidget);
      expect(find.widgetWithText(ActionChip, 'record'), findsOneWidget);

      await tester.tap(find.widgetWithText(ActionChip, 'recipe'));
      await tester.pump();
      expect(
          tester.widget<TextField>(search).controller!.text, 'recipe ');
      expect(find.byType(ActionChip), findsNothing);
      // the tap cancelled the typing debounce and re-ran the search itself;
      // drain any remaining frame work without waiting on the spinner
      await tester.pump(const Duration(milliseconds: 400));
    });
  });
}
