// Copy on a locked note never puts ciphertext on the clipboard: it opens the
// unlock flow instead, and the Unlocked dialog has its own Copy.
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/main.dart';

void main() {
  final copied = <String>[];
  setUp(() {
    copied.clear();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(SystemChannels.platform, (call) async {
      if (call.method == 'Clipboard.setData') {
        copied.add((call.arguments as Map)['text'] as String);
      }
      return null;
    });
  });

  // A search whose one hit is locked, with the row on screen.
  Future<void> pumpLockedHit(WidgetTester tester) async {
    await tester.pumpWidget(MaterialApp(
        home: RecallPage(
      tagLookup: (_) => const [],
      tagCloud: () => const [],
      recallLookup: (_, __) => const [Hit(1, 'aisc:ciphertext')],
      findLookup: (_) => const [],
    )));
    await tester.pump();
    await tester.enterText(find.byType(TextField).first, 'wifi');
    await tester.pump(const Duration(milliseconds: 300));
    await tester.pump();
    expect(find.text('locked'), findsOneWidget);
  }

  Future<void> settle(WidgetTester tester) async {
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
  }

  // The details page's Copy on a locked note asks for the password.
  testWidgets('Copy on the details page of a locked note opens Unlock',
      (tester) async {
    await pumpLockedHit(tester);
    await tester.tap(find.text('locked'));
    await settle(tester);
    expect(find.text('Details'), findsOneWidget);
    await tester.tap(find.byTooltip('Copy'));
    await settle(tester);
    expect(find.widgetWithText(FilledButton, 'Unlock'), findsOneWidget);
    expect(find.text('Copied'), findsNothing);
    expect(copied, isEmpty);
  });

  // The row menu's Copy does the same.
  testWidgets('Copy in a locked row menu opens Unlock', (tester) async {
    await pumpLockedHit(tester);
    await tester.tap(find.byTooltip('More'));
    await settle(tester);
    await tester.tap(find.text('Copy'));
    await settle(tester);
    expect(find.widgetWithText(FilledButton, 'Unlock'), findsOneWidget);
    expect(copied, isEmpty);
  });

  // The Unlocked dialog copies the plain text, nothing else.
  testWidgets('the Unlocked dialog copies the plain text', (tester) async {
    await tester.pumpWidget(MaterialApp(
        home: Scaffold(
            body: Builder(
                builder: (ctx) => TextButton(
                    onPressed: () => showUnlocked(ctx, 'hunter2'),
                    child: const Text('open'))))));
    await tester.tap(find.text('open'));
    await settle(tester);
    expect(find.text('Unlocked'), findsOneWidget);
    await tester.tap(find.text('Copy'));
    await settle(tester);
    expect(copied, ['hunter2']);
    expect(find.text('Copied'), findsOneWidget);
  });
}
