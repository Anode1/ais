// The microphone through the Voice seam: the typed query stays until a final
// result replaces it, the mic shows a session is open, and a session that
// heard nothing says so once and leaves the query alone.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/ais_ffi.dart';
import 'package:ais/main.dart';

class FakeVoice implements Voice {
  void Function(String status)? onStatus;
  void Function(String error)? onError;
  void Function(String words, bool isFinal)? onResult;
  int stops = 0;

  @override
  Future<bool> init(
      {required void Function(String status) onStatus,
      required void Function(String error) onError}) async {
    this.onStatus = onStatus;
    this.onError = onError;
    return true;
  }

  @override
  Future<void> listen(void Function(String words, bool isFinal) onResult) async {
    this.onResult = onResult;
  }

  @override
  Future<void> stop() async => stops++;
  @override
  void cancel() {}
}

void main() {
  late FakeVoice voice;
  late List<String> searched;

  Future<void> pumpPage(WidgetTester tester) async {
    voice = FakeVoice();
    searched = [];
    await tester.pumpWidget(MaterialApp(
        home: RecallPage(
      voice: voice,
      tagLookup: (_) => const [],
      tagCloud: () => const [],
      recallLookup: (k, _) {
        searched.add(k);
        return const [Hit(1, 'a note')];
      },
      findLookup: (_) => const [],
    )));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));
  }

  final field = find.byType(TextField).first;
  String fieldText(WidgetTester tester) =>
      tester.widget<TextField>(field).controller!.text;

  Future<void> typeAndTapMic(WidgetTester tester) async {
    await tester.enterText(field, 'wifi');
    await tester.pump(const Duration(milliseconds: 300));
    await tester.pump();
    await tester.tap(find.byTooltip('Voice search'));
    await tester.pump();
  }

  // Tapping the mic opens a session, shows it, and clears nothing.
  testWidgets('the mic shows it is listening and keeps the typed query',
      (tester) async {
    await pumpPage(tester);
    await typeAndTapMic(tester);
    expect(voice.onResult, isNotNull);
    expect(find.byIcon(Icons.mic), findsOneWidget);
    expect(find.byTooltip('Stop listening'), findsOneWidget);
    expect(fieldText(tester), 'wifi');
    voice.onResult!('', false); // an empty partial result changes nothing
    await tester.pump();
    expect(fieldText(tester), 'wifi');
  });

  // An empty final result: one snackbar line, the query as it was, mic idle.
  testWidgets('nothing heard says so and leaves the query', (tester) async {
    await pumpPage(tester);
    await typeAndTapMic(tester);
    searched.clear();
    voice.onResult!('', true);
    await tester.pump();
    expect(find.text('Nothing heard'), findsOneWidget);
    expect(fieldText(tester), 'wifi');
    expect(find.byIcon(Icons.mic_none), findsOneWidget);
    expect(searched, isEmpty);
  });

  // A recognizer error is the same outcome.
  testWidgets('an error says Nothing heard and leaves the query', (tester) async {
    await pumpPage(tester);
    await typeAndTapMic(tester);
    voice.onError!('error_speech_timeout');
    await tester.pump();
    expect(find.text('Nothing heard'), findsOneWidget);
    expect(fieldText(tester), 'wifi');
    expect(find.byIcon(Icons.mic_none), findsOneWidget);
  });

  // The final words replace the query and run the search.
  testWidgets('a final result replaces the query and searches', (tester) async {
    await pumpPage(tester);
    await typeAndTapMic(tester);
    searched.clear();
    voice.onResult!('hot', false);
    await tester.pump();
    expect(fieldText(tester), 'wifi');
    voice.onResult!('hotel', true);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));
    expect(fieldText(tester), 'hotel');
    expect(searched, contains('hotel'));
    expect(find.byIcon(Icons.mic_none), findsOneWidget);
  });

  // A second tap while listening stops the session.
  testWidgets('a tap while listening stops', (tester) async {
    await pumpPage(tester);
    await typeAndTapMic(tester);
    await tester.tap(find.byTooltip('Stop listening'));
    await tester.pump();
    expect(voice.stops, 1);
    expect(find.byIcon(Icons.mic_none), findsOneWidget);
  });
}
