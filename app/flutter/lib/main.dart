// main.dart -- AIS native client. Recall-first: a frosted search header, a
// results list, and a clear "Add" form. All logic is the C engine via AisEngine
// (ais_ffi.dart); this is just the surface. The header is a translucent
// (glassy) strip ABOVE the list -- never an overlay, so the list is always
// visible.
import 'dart:io';
import 'dart:ui';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';
import 'package:speech_to_text/speech_to_text.dart';
import 'ais_ffi.dart';

void main() => runApp(const AisApp());

class AisApp extends StatelessWidget {
  const AisApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'AIS',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          useMaterial3: true,
          colorSchemeSeed: const Color(0xFF1A0DAB),
          scaffoldBackgroundColor: const Color(0xFFEFEFF6),
        ),
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
  bool _searched = false;
  String _status = 'opening index…';
  String _query = '';
  String _dir = '';
  int _ms = 0;

  @override
  void initState() {
    super.initState();
    _init();
  }

  // Desktop shares the user's REAL index (same default the CLI uses); mobile
  // uses the app's private dir.
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
      _dir = dir;
      _status = 'Type keys, then search. Tap Add to store.';
    } catch (e) {
      _status = 'cannot open index: $e';
    }
    try {
      _voice = await _speech.initialize(); // unsupported on desktop -> false
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
      _searched = true;
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

  bool _isUrl(String v) => v.startsWith('http://') || v.startsWith('https://');

  // Switch the active index: type a folder path, reopen the engine there.
  Future<void> _changeStore() async {
    final ctrl = TextEditingController(text: _dir);
    final picked = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Store (index folder)'),
        content: TextField(
          controller: ctrl,
          autofocus: true,
          decoration: const InputDecoration(hintText: 'full path to a .ais index folder'),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, ctrl.text.trim()), child: const Text('Open')),
        ],
      ),
    );
    if (picked == null || picked.isEmpty || picked == _dir) return;
    try {
      _ais?.close();
      Directory(picked).createSync(recursive: true);
      _ais = AisEngine(picked);
      setState(() {
        _dir = picked;
        _results = const [];
        _searched = false;
        _query = '';
        _q.clear();
      });
    } catch (e) {
      setState(() => _status = 'cannot open: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Scaffold(
      body: SafeArea(
        child: Column(children: [
          // frosted header strip (sized to its content; list goes below it)
          ClipRect(
            child: BackdropFilter(
              filter: ImageFilter.blur(sigmaX: 14, sigmaY: 14),
              child: Container(
                padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
                color: Colors.white.withValues(alpha: 0.6),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(children: [
                      Text('AIS', style: Theme.of(context).textTheme.titleLarge),
                      const Spacer(),
                      if (_searched)
                        Text(
                          '${_results.length} result${_results.length == 1 ? '' : 's'} · $_ms ms',
                          style: TextStyle(color: cs.outline, fontSize: 12),
                        ),
                    ]),
                    const SizedBox(height: 8),
                    TextField(
                      controller: _q,
                      autofocus: true,
                      textInputAction: TextInputAction.search,
                      onSubmitted: (_) => _recall(),
                      decoration: InputDecoration(
                        hintText: 'type keys to recall…',
                        prefixIcon: const Icon(Icons.search),
                        filled: true,
                        fillColor: Colors.white.withValues(alpha: 0.85),
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(28),
                          borderSide: BorderSide.none,
                        ),
                        suffixIcon: _voice
                            ? IconButton(icon: const Icon(Icons.mic), onPressed: _listen)
                            : null,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Row(children: [
                      Expanded(
                        child: Text(
                          _dir.isEmpty ? 'store: (default)' : 'store: $_dir',
                          style: TextStyle(color: cs.outline, fontSize: 12),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                      TextButton(
                        onPressed: _ais == null ? null : _changeStore,
                        style: TextButton.styleFrom(
                          padding: const EdgeInsets.symmetric(horizontal: 8),
                          minimumSize: Size.zero,
                          tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        ),
                        child: const Text('change'),
                      ),
                    ]),
                  ],
                ),
              ),
            ),
          ),

          // results (always visible, fills the rest)
          Expanded(
            child: _results.isEmpty
                ? Center(
                    child: Padding(
                      padding: const EdgeInsets.all(24),
                      child: Text(
                        _searched ? 'No results for "$_query"' : _status,
                        textAlign: TextAlign.center,
                        style: TextStyle(color: cs.outline),
                      ),
                    ),
                  )
                : ListView.separated(
                    padding: const EdgeInsets.only(bottom: 88),
                    itemCount: _results.length,
                    separatorBuilder: (_, __) => const Divider(height: 1),
                    itemBuilder: (_, i) {
                      final v = _results[i];
                      return ListTile(
                        title: SelectableText(
                          v,
                          style: TextStyle(color: _isUrl(v) ? cs.primary : null),
                        ),
                        trailing: IconButton(
                          icon: const Icon(Icons.copy, size: 18),
                          tooltip: 'Copy',
                          onPressed: () {
                            Clipboard.setData(ClipboardData(text: v));
                            ScaffoldMessenger.of(context).showSnackBar(
                                const SnackBar(content: Text('Copied')));
                          },
                        ),
                      );
                    },
                  ),
          ),
        ]),
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: _ais == null ? null : _showAdd,
        icon: const Icon(Icons.add),
        label: const Text('Add'),
      ),
    );
  }

  // The Add form: value first (the thing to remember), keys second (optional,
  // prefilled from the search box). No hidden dependency on the search field.
  void _showAdd() {
    final valCtrl = TextEditingController();
    final keysCtrl = TextEditingController(text: _q.text.trim());
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      showDragHandle: true,
      builder: (ctx) => Padding(
        padding: EdgeInsets.only(
            bottom: MediaQuery.of(ctx).viewInsets.bottom + 16,
            left: 16, right: 16, top: 4),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const Text('Add to your memory',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600)),
            const SizedBox(height: 14),
            TextField(
              controller: valCtrl,
              autofocus: true,
              minLines: 1,
              maxLines: 5,
              decoration: const InputDecoration(
                labelText: 'What to remember',
                hintText: 'a link, a note, a phone number…',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: keysCtrl,
              decoration: const InputDecoration(
                labelText: 'Keys (space-separated, optional)',
                hintText: 'e.g. venice italy hotel',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 16),
            FilledButton.icon(
              icon: const Icon(Icons.check),
              label: const Text('Save'),
              onPressed: () {
                final value = valCtrl.text.trim();
                if (value.isEmpty || _ais == null) return;
                final keys = keysCtrl.text.trim();
                _ais!.store(keys, value);
                Navigator.pop(ctx);
                _q.text = keys;
                _recall(); // show what was just saved
                ScaffoldMessenger.of(context).showSnackBar(SnackBar(
                    content: Text(keys.isEmpty ? 'Saved (no keys)' : 'Saved under: $keys')));
              },
            ),
          ],
        ),
      ),
    );
  }

  @override
  void dispose() {
    _ais?.close();
    super.dispose();
  }
}
