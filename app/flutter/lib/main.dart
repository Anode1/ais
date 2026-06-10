// main.dart -- AIS native client. Recall-first (like the web/Tk GUIs): a search
// box, a mic (native speech-to-text), a results list, and a "+" to put. All the
// logic is the C engine via AisEngine (ais_ffi.dart); this is just the surface.
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:speech_to_text/speech_to_text.dart';
import 'ais_ffi.dart';

void main() => runApp(const AisApp());

class AisApp extends StatelessWidget {
  const AisApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'AIS',
        theme: ThemeData(useMaterial3: true, colorSchemeSeed: const Color(0xFF1A0DAB)),
        home: const RecallPage(),
      );
}

class RecallPage extends StatefulWidget {
  const RecallPage({super.key});
  @override
  State<RecallPage> createState() => _RecallPageState();
}

class _RecallPageState extends State<RecallPage> {
  final _q = TextEditingController();
  final _speech = SpeechToText();
  AisEngine? _ais;
  List<String> _results = const [];
  bool _voice = false;
  String _status = 'opening index…';
  String _query = '';
  int _ms = 0;

  @override
  void initState() {
    super.initState();
    _init();
  }

  // Desktop shares the user's REAL index (same default the CLI uses), so the app
  // sees the data you already stored. Mobile uses the app's private dir.
  Future<String> _indexDir() async {
    if (Platform.isAndroid || Platform.isIOS) {
      final docs = await getApplicationDocumentsDirectory();
      return '${docs.path}/ais';
    }
    final env = Platform.environment;
    final ai = env['AIS_INDEX'];
    if (ai != null && ai.isNotEmpty) return ai;
    final xdg = env['XDG_DATA_HOME'];
    if (xdg != null && xdg.isNotEmpty) return '$xdg/ais';
    return '${env['HOME']}/.local/share/ais';
  }

  Future<void> _init() async {
    try {
      final dir = await _indexDir();
      Directory(dir).createSync(recursive: true);
      _ais = AisEngine(dir);
      _status = 'index: $dir';
    } catch (e) {
      _status = 'cannot open index: $e';
    }
    try {
      _voice = await _speech.initialize(); // unsupported on desktop -> stays false
    } catch (_) {
      _voice = false;
    }
    if (mounted) setState(() {});
  }

  void _recall() {
    final keys = _q.text.trim();
    if (_ais == null || keys.isEmpty) return;
    final t0 = DateTime.now();
    final r = _ais!.recall(keys);
    setState(() {
      _query = keys;
      _ms = DateTime.now().difference(t0).inMilliseconds;
      _results = r;
    });
  }

  Future<void> _listen() async {
    if (!_voice) return;
    await _speech.listen(onResult: (r) {
      _q.text = r.recognizedWords;
      if (r.finalResult) _recall();
    });
  }

  @override
  Widget build(BuildContext context) {
    final muted = TextStyle(color: Colors.grey.shade600, fontSize: 13);
    final feedback = _query.isEmpty
        ? _status
        : '${_results.length} result${_results.length == 1 ? '' : 's'} for "$_query"  ·  $_ms ms';
    return Scaffold(
      appBar: AppBar(title: const Text('AIS')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Row(children: [
            Expanded(
              child: TextField(
                controller: _q,
                autofocus: true,
                textInputAction: TextInputAction.search,
                decoration: const InputDecoration(
                    hintText: 'ask or type keys…', border: OutlineInputBorder()),
                onSubmitted: (_) => _recall(),
              ),
            ),
            if (_voice)
              IconButton(icon: const Icon(Icons.mic), tooltip: 'Speak', onPressed: _listen),
            IconButton(icon: const Icon(Icons.search), tooltip: 'Recall', onPressed: _recall),
          ]),
          const SizedBox(height: 10),
          Text(feedback, style: muted),       // always-visible feedback line
          const SizedBox(height: 6),
          Expanded(
            child: ListView.separated(
              itemCount: _results.length,
              separatorBuilder: (_, __) => const Divider(height: 1),
              itemBuilder: (_, i) => ListTile(
                dense: true,
                title: SelectableText(_results[i]),
              ),
            ),
          ),
        ]),
      ),
      floatingActionButton: FloatingActionButton(
        tooltip: 'Put',
        onPressed: _ais == null ? null : _showAdd,
        child: const Icon(Icons.add),
      ),
    );
  }

  void _showAdd() {
    final v = TextEditingController();
    final keys = _q.text.trim();
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      builder: (ctx) => Padding(
        padding: EdgeInsets.only(
            bottom: MediaQuery.of(ctx).viewInsets.bottom, left: 16, right: 16, top: 16),
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          Text(keys.isEmpty ? 'Type keys first, then +' : 'Put under: $keys'),
          const SizedBox(height: 8),
          TextField(
            controller: v,
            decoration: const InputDecoration(hintText: 'value', border: OutlineInputBorder()),
          ),
          const SizedBox(height: 8),
          ElevatedButton(
            onPressed: () {
              if (keys.isNotEmpty && v.text.trim().isNotEmpty) {
                _ais?.store(keys, v.text.trim());
                Navigator.pop(ctx);
                _recall();
              }
            },
            child: const Text('Put'),
          ),
          const SizedBox(height: 16),
        ]),
      ),
    );
  }

  @override
  void dispose() {
    _ais?.close();
    super.dispose();
  }
}
