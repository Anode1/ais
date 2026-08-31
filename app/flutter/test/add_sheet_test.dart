// The Add sheet's passphrase confirmation: with Encrypt on, a second obscured
// field must match the first EXACTLY (raw strings, no trimming) before the
// save runs. The sheet is pumped with an onSave that validates the way the
// real _addSave does (addSaveError) and records what would be stored -- the
// real one needs the engine, which `flutter test` never has.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/add_validation.dart';
import 'package:ais/main.dart';

void main() {
  final saved = <(String, String, bool, String)>[];

  Future<String?> onSave(
      {required String value,
      required String keys,
      required bool encrypt,
      required String passphrase,
      required String repeat}) async {
    final err = addSaveError(
        value: value,
        engineReady: true,
        syncing: false,
        encrypt: encrypt,
        passphrase: passphrase,
        passphraseRepeat: repeat,
        keys: keys);
    if (err != null) return err;
    saved.add((value, keys, encrypt, passphrase));
    return null;
  }

  // Open the sheet the way the app does, so close-on-success is real.
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
                            suggest: (_) => const [], onSave: onSave)),
                    child: const Text('open'))))));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
  }

  Future<void> fill(WidgetTester tester, String label, String text) async {
    await tester.enterText(find.widgetWithText(TextField, label), text);
    await tester.pump();
  }

  testWidgets('Encrypt on shows Repeat passphrase and the no-recovery line',
      (tester) async {
    await openSheet(tester);
    expect(find.widgetWithText(TextField, 'Repeat passphrase'), findsNothing);
    await tester.tap(find.byType(Switch));
    await tester.pump();
    final repeat = find.widgetWithText(TextField, 'Repeat passphrase');
    expect(repeat, findsOneWidget);
    expect(tester.widget<TextField>(repeat).obscureText, isTrue);
    expect(find.text('A lost passphrase cannot be recovered.'), findsOneWidget);
  });

  testWidgets('a mismatch blocks the save and says so inline', (tester) async {
    await openSheet(tester);
    await fill(tester, 'What to remember', 'the safe code');
    await tester.tap(find.byType(Switch));
    await tester.pump();
    await fill(tester, 'Passphrase', 'secret');
    await fill(tester, 'Repeat passphrase', 'secreT');
    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();
    expect(find.text('Passphrases do not match'), findsOneWidget);
    expect(saved, isEmpty);
    // the sheet stays open for the correction
    expect(find.text('Add to your memory'), findsOneWidget);
  });

  testWidgets('the check is raw: a trailing space is a mismatch',
      (tester) async {
    await openSheet(tester);
    await fill(tester, 'What to remember', 'x');
    await tester.tap(find.byType(Switch));
    await tester.pump();
    await fill(tester, 'Passphrase', 'secret');
    await fill(tester, 'Repeat passphrase', 'secret ');
    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();
    expect(find.text('Passphrases do not match'), findsOneWidget);
    expect(saved, isEmpty);
  });

  testWidgets('a match saves and closes the sheet', (tester) async {
    await openSheet(tester);
    await fill(tester, 'What to remember', 'the safe code');
    await tester.tap(find.byType(Switch));
    await tester.pump();
    await fill(tester, 'Passphrase', 'secret');
    await fill(tester, 'Repeat passphrase', 'secret');
    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();
    expect(saved, [('the safe code', '', true, 'secret')]);
    expect(find.text('Add to your memory'), findsNothing);
  });

  testWidgets('correcting the mismatch then saving works in one sheet',
      (tester) async {
    await openSheet(tester);
    await fill(tester, 'What to remember', 'x');
    await tester.tap(find.byType(Switch));
    await tester.pump();
    await fill(tester, 'Passphrase', 'pw');
    await fill(tester, 'Repeat passphrase', 'nope');
    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();
    expect(find.text('Passphrases do not match'), findsOneWidget);
    await fill(tester, 'Repeat passphrase', 'pw');
    await tester.tap(find.widgetWithText(FilledButton, 'Save'));
    await tester.pumpAndSettle();
    expect(saved.length, 1);
    expect(find.text('Add to your memory'), findsNothing);
  });
}
