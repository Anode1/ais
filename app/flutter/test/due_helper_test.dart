// The due date helper (doc/dev/GUI.md, "The due date helper"): a calendar
// icon inside the Add sheet's Tags field and the Edit tags field, tooltip
// "Remind me on a day", whose picker writes due:YYYY-MM-DD as text or as a
// chip, replacing a due:DATE tag already there; Cancel changes nothing. The
// real showDatePicker is driven: it opens on today, so OK picks today.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/add_validation.dart';
import 'package:ais/help.dart';
import 'package:ais/main.dart';

const tip = 'Remind me on a day';
const day = '2027-04-15';

Future<String?> onSave(
    {required String value,
    required String keys,
    required bool encrypt,
    required String passphrase,
    required String repeat}) async =>
    addSaveError(
        value: value,
        engineReady: true,
        syncing: false,
        encrypt: encrypt,
        passphrase: passphrase,
        passphraseRepeat: repeat,
        keys: keys);

Future<void> openSheet(WidgetTester tester, {String keys = ''}) async {
  await tester.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Builder(
              builder: (ctx) => TextButton(
                  onPressed: () => showModalBottomSheet(
                      context: ctx,
                      isScrollControlled: true,
                      useSafeArea: true,
                      showDragHandle: true,
                      builder: (_) => AddSheet(
                          initialKeys: keys,
                          suggest: (_) => const [],
                          onSave: onSave)),
                  child: const Text('open'))))));
  await tester.tap(find.text('open'));
  await tester.pumpAndSettle();
}

// What the dialog popped with: null until Apply or Cancel.
class Popped {
  List<String>? tags;
}

Future<Popped> openDialog(WidgetTester tester,
    {List<String> initial = const ['home']}) async {
  final popped = Popped();
  await tester.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Builder(
              builder: (ctx) => TextButton(
                  onPressed: () async {
                    popped.tags = await showDialog<List<String>>(
                        context: ctx,
                        builder: (_) => EditTagsDialog(
                            initial: initial, suggest: (_) => const []));
                  },
                  child: const Text('open'))))));
  await tester.tap(find.text('open'));
  await tester.pumpAndSettle();
  return popped;
}

Finder get icon => find.byTooltip(tip);
Finder get tagsField => find.widgetWithText(TextField, 'Tags');
TextEditingController tagsCtrl(WidgetTester tester) =>
    tester.widget<TextField>(tagsField).controller!;

// The picker's own buttons, not the dialog's Cancel under it.
Finder pickerButton(String label) => find.descendant(
    of: find.byType(DatePickerDialog), matching: find.text(label));

void main() {
  group('withDueTag', () {
    test('appends after a space', () {
      expect(withDueTag('', day), 'due:$day');
      expect(withDueTag('wifi guest', day), 'wifi guest due:$day');
      expect(withDueTag('wifi guest ', day), 'wifi guest due:$day');
    });

    test('replaces a due:DATE token of each shape, in place', () {
      expect(withDueTag('a due:2027 b', day), 'a due:$day b');
      expect(withDueTag('a due:2027-04 b', day), 'a due:$day b');
      expect(withDueTag('due:2026-11-02 b', day), 'due:$day b');
      expect(withDueTag('a,due:2027,b', day), 'a,due:$day,b');
    });

    test('a second due:DATE token goes; due:soon stays', () {
      expect(withDueTag('a due:2027 due:2028', day), 'a due:$day');
      expect(withDueTag('a due:soon', day), 'a due:soon due:$day');
      expect(withDueTag('due:2027-1', day), 'due:2027-1 due:$day');
    });
  });

  test('withDueChip replaces in place or appends', () {
    expect(withDueChip(['home'], day), ['home', 'due:$day']);
    expect(withDueChip(['due:2027', 'home', 'due:2028'], day),
        ['due:$day', 'home']);
    expect(withDueChip(['due:soon'], day), ['due:soon', 'due:$day']);
  });

  group('Add sheet', () {
    testWidgets('the Tags field carries the icon; OK writes today, caret after',
        (tester) async {
      await openSheet(tester, keys: 'wifi guest');
      expect(find.descendant(of: tagsField, matching: icon), findsOneWidget);
      await tester.tap(icon);
      await tester.pumpAndSettle();
      await tester.tap(pickerButton('OK'));
      await tester.pumpAndSettle();
      final tag = 'due:${todayLocal()}';
      final v = tagsCtrl(tester).value;
      expect(v.text, 'wifi guest $tag');
      expect(v.selection.baseOffset, v.text.length);
    });

    testWidgets('OK replaces a due:DATE tag; Cancel changes nothing',
        (tester) async {
      await openSheet(tester, keys: 'due:2027 wifi');
      await tester.tap(icon);
      await tester.pumpAndSettle();
      await tester.tap(pickerButton('Cancel'));
      await tester.pumpAndSettle();
      expect(tagsCtrl(tester).text, 'due:2027 wifi');
      await tester.tap(icon);
      await tester.pumpAndSettle();
      await tester.tap(pickerButton('OK'));
      await tester.pumpAndSettle();
      final tag = 'due:${todayLocal()}';
      final v = tagsCtrl(tester).value;
      expect(v.text, '$tag wifi');
      expect(v.selection.baseOffset, tag.length);
    });
  });

  group('Edit tags', () {
    testWidgets('the field carries the icon; OK commits a chip',
        (tester) async {
      final popped = await openDialog(tester);
      expect(icon, findsOneWidget);
      await tester.tap(icon);
      await tester.pumpAndSettle();
      await tester.tap(pickerButton('OK'));
      await tester.pumpAndSettle();
      final tag = 'due:${todayLocal()}';
      expect(find.widgetWithText(InputChip, tag), findsOneWidget);
      await tester.tap(find.text('Apply'));
      await tester.pumpAndSettle();
      expect(popped.tags, ['home', tag]);
    });

    testWidgets('OK replaces a due:DATE chip; Cancel changes nothing',
        (tester) async {
      final popped = await openDialog(tester, initial: ['due:2027-04', 'home']);
      await tester.tap(icon);
      await tester.pumpAndSettle();
      await tester.tap(pickerButton('Cancel'));
      await tester.pumpAndSettle();
      expect(find.widgetWithText(InputChip, 'due:2027-04'), findsOneWidget);
      await tester.tap(icon);
      await tester.pumpAndSettle();
      await tester.tap(pickerButton('OK'));
      await tester.pumpAndSettle();
      final tag = 'due:${todayLocal()}';
      expect(find.widgetWithText(InputChip, 'due:2027-04'), findsNothing);
      await tester.tap(find.text('Apply'));
      await tester.pumpAndSettle();
      expect(popped.tags, [tag, 'home']);
    });
  });

  // The Help text is GUI.md's, under its own heading, in both builds.
  testWidgets('Help shows Reminders after Save', (tester) async {
    for (final sections in [helpSections, helpSectionsIos]) {
      final titles = sections.map((s) => s.$1).toList();
      expect(titles.indexOf('Reminders'), titles.indexOf('Save') + 1);
    }
    await tester.pumpWidget(MaterialApp(home: HelpPage(open: (_) {})));
    await tester.scrollUntilVisible(find.text('Reminders'), 200);
    expect(find.text('Reminders'), findsOneWidget);
    expect(find.textContaining('The calendar icon beside Tags'), findsOneWidget);
  });
}
