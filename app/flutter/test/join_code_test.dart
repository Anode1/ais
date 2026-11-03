// The Scan or type a code dialog: Sync with no code says so and stays open;
// with one it returns what was typed.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/main.dart';

void main() {
  testWidgets('Sync without a code says so and keeps the dialog open',
      (tester) async {
    (String, String)? got;
    var closed = false;
    await tester.pumpWidget(MaterialApp(
        home: Scaffold(
            body: Builder(
                builder: (ctx) => TextButton(
                    onPressed: () async {
                      got = await askJoinCode(ctx);
                      closed = true;
                    },
                    child: const Text('open'))))));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    await tester.tap(find.widgetWithText(FilledButton, 'Sync'));
    await tester.pumpAndSettle();
    expect(find.text('Type the code shown on the other device'), findsOneWidget);
    expect(find.text('Scan or type a code'), findsOneWidget);
    expect(closed, isFalse);
    await tester.enterText(find.widgetWithText(TextField, 'Code'), ' abc ');
    await tester.tap(find.widgetWithText(FilledButton, 'Sync'));
    await tester.pumpAndSettle();
    expect(closed, isTrue);
    expect(got, ('http://', 'abc'));
  });
}
