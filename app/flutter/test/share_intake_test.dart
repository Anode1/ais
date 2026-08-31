// Android share intake: text arriving over the ais/share channel must open
// the Add sheet with the value prefilled, whether the share cold-started the
// app (getInitialShared) or landed while it ran (onShared). The channel is
// mocked; the page runs with no engine (its open fails and the body shows a
// perpetual spinner), so bounded pump()s, never pumpAndSettle.
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/main.dart';

void main() {
  const chan = MethodChannel('ais/share');

  Future<void> pumpApp(WidgetTester tester) async {
    await tester.pumpWidget(const MaterialApp(home: RecallPage()));
    await tester.pump(); // _init settles (engine open fails, channels wired)
    await tester.pump(const Duration(milliseconds: 50));
  }

  Future<void> settleSheet(WidgetTester tester) async {
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400)); // sheet animation
  }

  testWidgets('cold start: getInitialShared prefills the Add sheet',
      (tester) async {
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(chan,
        (call) async => call.method == 'getInitialShared' ? 'milk eggs bread' : null);
    addTearDown(() => tester.binding.defaultBinaryMessenger
        .setMockMethodCallHandler(chan, null));
    await pumpApp(tester);
    await settleSheet(tester);
    expect(find.text('Add to your memory'), findsOneWidget);
    expect(find.widgetWithText(TextField, 'milk eggs bread'), findsOneWidget);
  });

  testWidgets('warm: an onShared push opens the Add sheet prefilled',
      (tester) async {
    await pumpApp(tester);
    expect(find.text('Add to your memory'), findsNothing);
    await tester.binding.defaultBinaryMessenger.handlePlatformMessage(
        'ais/share',
        const StandardMethodCodec()
            .encodeMethodCall(const MethodCall('onShared', 'a shared link')),
        (_) {});
    await settleSheet(tester);
    expect(find.text('Add to your memory'), findsOneWidget);
    expect(find.widgetWithText(TextField, 'a shared link'), findsOneWidget);
  });

  testWidgets('a non-string or unknown call opens nothing', (tester) async {
    await pumpApp(tester);
    await tester.binding.defaultBinaryMessenger.handlePlatformMessage(
        'ais/share',
        const StandardMethodCodec()
            .encodeMethodCall(const MethodCall('onShared', 42)),
        (_) {});
    await settleSheet(tester);
    expect(find.text('Add to your memory'), findsNothing);
  });
}
