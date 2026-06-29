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
  List<Hit> _results = const [];
  List<TlRow> _tl = const [];
  List<TagRow> _tags = const [];
  int _tlBefore = 0; // keyset cursor: last id of the loaded timeline page
  bool _tlMore = false; // last page was full, so more may exist
  String _tlFrom = ''; // timeline date range, "YYYY-MM-DD" ('' = open)
  String _tlTo = '';
  static const int _tlPage = 100;
  String _view = 'recall'; // recall | timeline | tags
  bool _matchOr = false; // false = AND (default; engine relaxes to OR if empty); true = OR
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

  // Desktop shares the user's REAL index (the same one the CLI resolves: nearest
  // .ais/, ~/.ais/config, else ~/.ais) via the engine, so no env vars and no
  // duplicated logic. Mobile uses the app's private dir.
  Future<String> _indexDir() async {
    if (Platform.isAndroid || Platform.isIOS) {
      final docs = await getApplicationDocumentsDirectory();
      return '${docs.path}/ais';
    }
    final dir = AisIndex.locate();
    if (dir == null || dir.isEmpty) {
      throw Exception('cannot resolve the default index');
    }
    return dir;
  }

  Future<void> _init() async {
    try {
      final dir = await _indexDir();
      Directory(dir).createSync(recursive: true);
      _ais = AisEngine(dir);
      _dir = dir;
      _status = 'Type keys, then Get. Tap Add to save.';
    } catch (e) {
      _status = 'cannot open index: $e';
    }
    // Speech is initialized lazily on the first mic tap (see _listen), so the
    // permission prompt is tied to a user gesture rather than app launch.
    if (mounted) setState(() {});
  }

  void _recall() {
    final keys = _q.text.trim();
    if (_ais == null || keys.isEmpty) return;
    final t0 = DateTime.now();
    final r = _ais!.recall(keys, orMode: _matchOr);
    setState(() {
      _query = keys;
      _searched = true;
      _ms = DateTime.now().difference(t0).inMilliseconds;
      _results = r;
    });
  }

  void _setView(String v) {
    setState(() => _view = v);
    if (v == 'recall') {
      if (_q.text.trim().isNotEmpty) {
        _recall();
      } else {
        // empty query: blank the pane and show the hint, like the other GUIs
        setState(() {
          _results = const [];
          _searched = false;
          _status = 'Type keys, then Get.';
        });
      }
    } else if (v == 'timeline') {
      _loadTimeline();
    } else {
      setState(() => _tags = _ais?.tags() ?? const []);
    }
  }

  // Fresh timeline load: reset the cursor and pull page one from the newest,
  // within the current [_tlFrom, _tlTo] range.
  void _loadTimeline() {
    final page = _ais?.timeline(before: 0, count: _tlPage, from: _tlFrom, to: _tlTo) ?? const [];
    setState(() {
      _tl = page;
      _tlBefore = page.isNotEmpty ? page.last.id : 0;
      _tlMore = page.length == _tlPage;
    });
  }

  // Open a date picker for one bound; on pick, store it and reload from page one.
  Future<void> _pickDate({required bool isFrom}) async {
    final cur = isFrom ? _tlFrom : _tlTo;
    final init = cur.isNotEmpty ? DateTime.tryParse(cur) ?? DateTime.now() : DateTime.now();
    final d = await showDatePicker(
      context: context,
      initialDate: init,
      firstDate: DateTime(2000),
      lastDate: DateTime(2100),
    );
    if (d == null) return;
    final s = '${d.year.toString().padLeft(4, '0')}-${d.month.toString().padLeft(2, '0')}-${d.day.toString().padLeft(2, '0')}';
    if (isFrom) {
      _tlFrom = s;
    } else {
      _tlTo = s;
    }
    _loadTimeline();
  }

  void _clearRange() {
    if (_tlFrom.isEmpty && _tlTo.isEmpty) return;
    _tlFrom = '';
    _tlTo = '';
    _loadTimeline();
  }

  static const _months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
  String _fmtDay(String d) {
    final p = d.split('-');
    if (p.length < 3) return d;
    return '${int.parse(p[2])} ${_months[int.parse(p[1]) - 1]} ${p[0]}';
  }

  Future<void> _listen() async {
    // First tap: init the recognizer, which triggers the runtime mic-permission
    // prompt on Android/iOS. _voice stays false on desktop or if denied.
    if (!_voice) {
      try {
        _voice = await _speech.initialize();
      } catch (_) {
        _voice = false;
      }
      if (mounted) setState(() {});
      if (!_voice) {
        if (mounted) setState(() => _status = 'Microphone unavailable or permission denied');
        return;
      }
    }
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
      // Desktop: remember the choice the same way `ais --default` does, so the
      // next launch (and the CLI) opens it too. Mobile's index is fixed.
      if (!Platform.isAndroid && !Platform.isIOS) AisIndex.setDefault(picked);
      setState(() {
        _dir = picked;
        _results = const [];
        _tl = const [];
        _tlBefore = 0;
        _tlMore = false;
        _tlFrom = '';
        _tlTo = '';
        _tags = const [];
        _view = 'recall';
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
    // Views (Search/Timeline/Tags) live in the bottom NavigationBar below; the
    // header is just search + Get + the store row.
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
                    Row(
                      children: [
                        Expanded(
                          child: TextField(
                            controller: _q,
                            autofocus: true,
                            textInputAction: TextInputAction.search,
                            onSubmitted: (_) => _recall(),
                            decoration: InputDecoration(
                              hintText: 'type keys, then Get',
                              prefixIcon: const Icon(Icons.search),
                              filled: true,
                              fillColor: Colors.white.withValues(alpha: 0.85),
                              border: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(28),
                                borderSide: BorderSide.none,
                              ),
                              // mic on mobile so the first tap can request permission
                              suffixIcon:
                                  (_voice || Platform.isAndroid || Platform.isIOS)
                                      ? IconButton(
                                          icon: const Icon(Icons.mic),
                                          onPressed: _listen)
                                      : null,
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        ElevatedButton(
                          onPressed: () => _setView('recall'),
                          style: ElevatedButton.styleFrom(
                            backgroundColor: cs.primary,
                            foregroundColor: cs.onPrimary,
                            padding: const EdgeInsets.symmetric(
                                horizontal: 22, vertical: 18),
                            shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(24)),
                          ),
                          child: const Text('Get'),
                        ),
                      ],
                    ),
                    Row(
                      children: [
                        Checkbox(
                          value: _matchOr,
                          visualDensity: VisualDensity.compact,
                          materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                          onChanged: (v) {
                            setState(() => _matchOr = v ?? false);
                            if (_q.text.trim().isNotEmpty) _recall();
                          },
                        ),
                        const Text('Match any key', style: TextStyle(fontSize: 13)),
                      ],
                    ),
                    const SizedBox(height: 4),
                    Row(children: [
                      Flexible(
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

          // body (always visible, fills the rest): recall / timeline / tags
          Expanded(child: _body(cs)),
        ]),
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: _ais == null ? null : _showAdd,
        icon: const Icon(Icons.add),
        label: const Text('Add'),
      ),
      // Three peer destinations, thumb-reachable. The label is "Search"; the
      // internal view key stays 'recall' (RecallPage / _view). The header's
      // "Get" is the ACTION button -- one word per concept, no two "Get"s.
      bottomNavigationBar: NavigationBar(
        selectedIndex:
            ['recall', 'timeline', 'tags'].indexOf(_view).clamp(0, 2),
        onDestinationSelected: (i) =>
            _setView(const ['recall', 'timeline', 'tags'][i]),
        destinations: const [
          NavigationDestination(
              icon: Icon(Icons.search_outlined),
              selectedIcon: Icon(Icons.search),
              label: 'Search'),
          NavigationDestination(
              icon: Icon(Icons.schedule_outlined),
              selectedIcon: Icon(Icons.schedule),
              label: 'Timeline'),
          NavigationDestination(
              icon: Icon(Icons.label_outline),
              selectedIcon: Icon(Icons.label),
              label: 'Tags'),
        ],
      ),
    );
  }

  Widget _centerMsg(String msg, ColorScheme cs) => Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Text(msg, textAlign: TextAlign.center, style: TextStyle(color: cs.outline)),
        ),
      );

  Widget _body(ColorScheme cs) {
    switch (_view) {
      case 'timeline':
        return _timelineBody(cs);
      case 'tags':
        return _tagsBody(cs);
      default:
        return _recallBody(cs);
    }
  }

  // Delete one record by id, after a confirm; then refresh the results.
  Future<void> _deleteHit(Hit hit) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Delete this record?'),
        content: SelectableText(hit.value),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Delete')),
        ],
      ),
    );
    if (ok != true || _ais == null) return;
    _ais!.del(hit.id);
    _recall();
  }

  // Edit a record's keys by id: a bare KEY attaches, a -KEY detaches.
  Future<void> _editKeys(Hit hit) async {
    final ctrl = TextEditingController();
    final text = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Edit keys'),
        content: TextField(
          controller: ctrl,
          autofocus: true,
          onSubmitted: (v) => Navigator.pop(ctx, v),
          decoration: const InputDecoration(hintText: 'a KEY adds it, -KEY removes it'),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, ctrl.text), child: const Text('Apply')),
        ],
      ),
    );
    if (text == null || text.trim().isEmpty || _ais == null) return;
    _ais!.update(hit.id, text.trim());
    _recall();
  }

  // Reveal an encrypted ("aisc:") hit: ask for the passphrase, decrypt
  // in-process, and show the cleartext in a dialog (encrypted documents are
  // revealed via the CLI).
  Future<void> _revealHit(Hit hit) async {
    final ctrl = TextEditingController();
    final pass = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Reveal'),
        content: TextField(
          controller: ctrl,
          obscureText: true,
          autofocus: true,
          onSubmitted: (v) => Navigator.pop(ctx, v),
          decoration: const InputDecoration(labelText: 'Passphrase'),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, ctrl.text), child: const Text('Reveal')),
        ],
      ),
    );
    if (pass == null || pass.isEmpty || _ais == null) return;
    final clear = await _ais!.revealAsync(hit.value, pass);   // off the UI isolate
    if (!mounted) return;
    if (clear == null) {
      ScaffoldMessenger.of(context)
          .showSnackBar(const SnackBar(content: Text('Could not decrypt')));
      return;
    }
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Decrypted'),
        content: SelectableText(clear),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Close')),
        ],
      ),
    );
  }

  Widget _recallBody(ColorScheme cs) {
    if (_results.isEmpty) {
      return _centerMsg(_searched ? 'No results for "$_query"' : _status, cs);
    }
    return ListView.separated(
      padding: const EdgeInsets.only(bottom: 88),
      itemCount: _results.length,
      separatorBuilder: (_, __) => const Divider(height: 1),
      itemBuilder: (_, i) {
        final hit = _results[i];
        final v = hit.value;
        final isSecret = v.startsWith('aisc:');
        return ListTile(
          title: isSecret
              ? Row(mainAxisSize: MainAxisSize.min, children: [
                  Icon(Icons.lock_outline, size: 16, color: cs.outline),
                  const SizedBox(width: 6),
                  Text('encrypted', style: TextStyle(color: cs.outline)),
                ])
              : SelectableText(v,
                  style: TextStyle(color: _isUrl(v) ? cs.primary : null)),
          trailing: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (isSecret)
                IconButton(
                  icon: const Icon(Icons.lock_open, size: 18),
                  tooltip: 'Reveal',
                  onPressed: () => _revealHit(hit),
                ),
              IconButton(
                icon: const Icon(Icons.copy, size: 18),
                tooltip: 'Copy',
                onPressed: () {
                  Clipboard.setData(ClipboardData(text: v));
                  ScaffoldMessenger.of(context)
                      .showSnackBar(const SnackBar(content: Text('Copied')));
                },
              ),
              IconButton(
                icon: const Icon(Icons.edit_outlined, size: 18),
                tooltip: 'Edit keys',
                onPressed: () => _editKeys(hit),
              ),
              IconButton(
                icon: const Icon(Icons.delete_outline, size: 18),
                tooltip: 'Delete',
                onPressed: () => _deleteHit(hit),
              ),
            ],
          ),
        );
      },
    );
  }

  // Page on with the keyset cursor: fetch the next page of records older than
  // the last id we hold, append it, and advance the cursor.
  void _loadMoreTimeline() {
    if (_ais == null) return;
    final page = _ais!.timeline(before: _tlBefore, count: _tlPage, from: _tlFrom, to: _tlTo);
    setState(() {
      _tl = [..._tl, ...page];
      if (page.isNotEmpty) _tlBefore = page.last.id;
      _tlMore = page.length == _tlPage;
    });
  }

  // A compact from/to date-range filter, shown above the timeline list only.
  Widget _rangeBar(ColorScheme cs) {
    final on = _tlFrom.isNotEmpty || _tlTo.isNotEmpty;
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 0),
      child: Row(children: [
        InputChip(
          label: Text(_tlFrom.isEmpty ? 'From' : 'From: $_tlFrom'),
          visualDensity: VisualDensity.compact,
          onPressed: () => _pickDate(isFrom: true),
        ),
        const SizedBox(width: 8),
        InputChip(
          label: Text(_tlTo.isEmpty ? 'To' : 'To: $_tlTo'),
          visualDensity: VisualDensity.compact,
          onPressed: () => _pickDate(isFrom: false),
        ),
        if (on)
          IconButton(
            icon: const Icon(Icons.clear, size: 18),
            tooltip: 'Clear range',
            visualDensity: VisualDensity.compact,
            onPressed: _clearRange,
          ),
      ]),
    );
  }

  // Timeline: dateless rows surface first, then newest; grouped by day.
  Widget _timelineBody(ColorScheme cs) {
    if (_tl.isEmpty) {
      return Column(children: [
        _rangeBar(cs),
        Expanded(child: _centerMsg('Nothing saved yet.', cs)),
      ]);
    }
    final items = <Widget>[];
    String? day;
    String p2(int n) => n.toString().padLeft(2, '0');
    for (final r in _tl) {
      // engine stores UTC ('…Z'); show local. An old local ts (no Z) parses as local.
      final dt = r.ts.isEmpty ? null : DateTime.tryParse(r.ts)?.toLocal();
      final d = dt == null ? '' : '${dt.year}-${p2(dt.month)}-${p2(dt.day)}';
      if (d != day) {
        day = d;
        items.add(Padding(
          padding: const EdgeInsets.fromLTRB(16, 18, 16, 4),
          child: Text(d.isEmpty ? '(undated)' : _fmtDay(d),
              style: TextStyle(fontSize: 14, color: cs.onSurface, fontWeight: FontWeight.w700)),
        ));
      }
      final time = (dt != null && r.ts.contains('T')) ? '${p2(dt.hour)}:${p2(dt.minute)} · ' : '';
      items.add(ListTile(
        title: SelectableText(r.value,
            style: TextStyle(color: _isUrl(r.value) ? cs.primary : null)),
        subtitle: Text('$time${r.keys.isEmpty ? '(no keys)' : r.keys}',
            style: TextStyle(color: cs.onSurfaceVariant, fontSize: 13)),
      ));
    }
    if (_tlMore) {
      items.add(Padding(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 8),
        child: Center(
          child: OutlinedButton(
            onPressed: _loadMoreTimeline,
            child: const Text('Load more'),
          ),
        ),
      ));
    }
    return Column(children: [
      _rangeBar(cs),
      Expanded(
        child: ListView(padding: const EdgeInsets.only(bottom: 88), children: items),
      ),
    ]);
  }

  // Tags: every key with its count, busiest first; tap a key to recall it.
  Widget _tagsBody(ColorScheme cs) {
    if (_tags.isEmpty) return _centerMsg('No tags yet.', cs);
    return ListView.separated(
      padding: const EdgeInsets.only(bottom: 88),
      itemCount: _tags.length,
      separatorBuilder: (_, __) => const Divider(height: 1),
      itemBuilder: (_, i) {
        final t = _tags[i];
        return ListTile(
          title: Text(t.key, style: TextStyle(color: cs.primary)),
          trailing: Chip(label: Text('${t.count}'), visualDensity: VisualDensity.compact),
          onTap: () {
            _q.text = t.key;
            _setView('recall');
          },
        );
      },
    );
  }

  // The Add form: value first (the thing to remember), keys second (optional,
  // prefilled from the search box). No hidden dependency on the search field.
  void _showAdd() {
    final valCtrl = TextEditingController();
    final keysCtrl = TextEditingController(text: _q.text.trim());
    final ppCtrl = TextEditingController();
    bool encrypt = false;                 // off by default
    bool saving = false;                  // true while the off-isolate encrypt runs
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      showDragHandle: true,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setSheet) => Padding(
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
              controller: keysCtrl,
              decoration: const InputDecoration(
                labelText: 'Keys (space-separated, optional)',
                hintText: 'e.g. venice italy hotel',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: valCtrl,
              autofocus: true,
              minLines: 1,
              maxLines: 3,
              decoration: const InputDecoration(
                labelText: 'What to remember',
                hintText: 'a link, a note, a phone number…',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 4),
            Row(children: [
              Checkbox(
                value: encrypt,
                onChanged: (b) => setSheet(() => encrypt = b ?? false),
              ),
              const Text('Encrypt'),
            ]),
            if (encrypt)
              Padding(
                padding: const EdgeInsets.only(bottom: 4),
                child: TextField(
                  controller: ppCtrl,
                  obscureText: true,
                  decoration: const InputDecoration(
                    labelText: 'Passphrase',
                    border: OutlineInputBorder(),
                  ),
                ),
              ),
            const SizedBox(height: 16),
            FilledButton.icon(
              icon: saving
                  ? const SizedBox(
                      width: 16, height: 16,
                      child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.check),
              label: Text(saving ? 'Encrypting…' : 'Save'),
              onPressed: saving
                  ? null
                  : () async {
                      final value = valCtrl.text.trim();
                      if (value.isEmpty || _ais == null) return;
                      final keys = keysCtrl.text.trim();
                      final messenger = ScaffoldMessenger.of(context);
                      final nav = Navigator.of(ctx);
                      if (encrypt) {
                        if (ppCtrl.text.isEmpty) {
                          messenger.showSnackBar(const SnackBar(
                              content: Text('Enter a passphrase to encrypt')));
                          return;
                        }
                        setSheet(() => saving = true);
                        await _ais!.storeEncryptedAsync(keys, value, ppCtrl.text);
                      } else {
                        _ais!.store(keys, value);
                      }
                      if (!mounted) return;
                      nav.pop();
                      _q.text = keys;
                      _recall(); // show what was just saved
                      messenger.showSnackBar(SnackBar(
                          content: Text(keys.isEmpty ? 'Saved (no keys)' : 'Saved under: $keys')));
                    },
            ),
          ],
        ),
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
