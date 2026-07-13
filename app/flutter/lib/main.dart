// main.dart -- AIS native client. Recall-first: a frosted search header, a
// results list, and a clear "Add" form. All logic is the C engine via AisEngine
// (ais_ffi.dart); this is just the surface. The header is a translucent
// (glassy) strip ABOVE the list -- never an overlay, so the list is always
// visible.
import 'dart:async';
import 'dart:io';
import 'dart:ui';
import 'dart:math';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:file_selector/file_selector.dart';
import 'package:path_provider/path_provider.dart';
import 'package:qr_flutter/qr_flutter.dart';
import 'package:share_plus/share_plus.dart';
import 'package:speech_to_text/speech_to_text.dart';
import 'ais_ffi.dart';

void main() => runApp(const AisApp());

// Session-only theme choice (System/Light/Dark). Lifted out of AisApp so the
// overflow menu's Theme picker can drive it via ValueListenableBuilder without
// a full app rebuild path. Not persisted -- resets to System on next launch.
final ValueNotifier<ThemeMode> themeModeNotifier =
    ValueNotifier(ThemeMode.system);

class AisApp extends StatelessWidget {
  const AisApp({super.key});
  @override
  Widget build(BuildContext context) {
    // One seed, two schemes: follow the system between light and dark. The
    // scaffold background and every surface come from the ColorScheme now (no
    // hardcoded light surfaces), so both themes read correctly.
    const seed = Color(0xFF1A0DAB);
    return ValueListenableBuilder<ThemeMode>(
      valueListenable: themeModeNotifier,
      builder: (context, mode, _) => MaterialApp(
        title: 'AIS',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          useMaterial3: true,
          colorScheme: ColorScheme.fromSeed(
              seedColor: seed, brightness: Brightness.light),
        ),
        darkTheme: ThemeData(
          useMaterial3: true,
          colorScheme:
              ColorScheme.fromSeed(seedColor: seed, brightness: Brightness.dark),
        ),
        themeMode: mode,
        home: const RecallPage(),
      ),
    );
  }
}

class RecallPage extends StatefulWidget {
  const RecallPage({super.key});
  @override
  State<RecallPage> createState() => _RecallPageState();
}

class _RecallPageState extends State<RecallPage> {
  final _q = TextEditingController();
  final _speech = SpeechToText();
  // Live search: each keystroke reschedules this; _recall() fires once typing
  // pauses, so the list filters as you type without pressing Search.
  Timer? _debounce;
  AisEngine? _ais;
  List<Hit> _results = const [];
  // Tags for the current recall/find results, keyed by record id. The recall Hit
  // carries only id+value, so tags are fetched ONCE when results load (here, not
  // in the itemBuilder, which reruns on scroll). Cleared whenever results reset.
  Map<int, String> _resultKeys = const {};
  List<TlRow> _tl = const [];
  List<TagRow> _tags = const [];
  int _tlBefore = 0; // keyset cursor: last id of the loaded timeline page
  bool _tlMore = false; // last page was full, so more may exist
  String _tlFrom = ''; // timeline date range, "YYYY-MM-DD" ('' = open)
  String _tlTo = '';
  static const int _tlPage = 100;
  String _view = 'timeline'; // recall | timeline | tags -- open on content, not a blank search
  // Ids optimistically removed from the lists but not yet committed to the
  // engine: they sit in their Undo window. A commit (snackbar closes without
  // Undo, or a second delete supersedes it) clears the id and calls del().
  final Set<int> _pendingDelete = {};
  // The live "Deleted / UNDO" snackbar per pending id, so a commit can dismiss
  // it -- once del() has run, UNDO would only fake-restore a row the engine no
  // longer holds. Cleared when the snackbar closes.
  final Map<int, ScaffoldFeatureController<SnackBar, SnackBarClosedReason>>
      _delSnack = {};
  bool _voice = false;
  bool _searched = false;
  // Full-text fallback is SECONDARY: only shown after a key search finds nothing
  // and the user explicitly taps "Search note text instead". True while find()
  // results are on screen; reset on any new key search, clear, or view change.
  bool _textSearch = false;
  String _status = 'opening index…';
  String _query = '';
  String _dir = '';
  int _ms = 0;
  // One sync at a time: a scanned deep link arrives off the platform channel, not
  // through the barrier dialog, so it could otherwise start a second sync on the
  // shared engine handle mid-sync (data race). This gates every sync entry point.
  bool _syncBusy = false;
  // Folder auto-sync (a Syncthing / cloud folder keeps devices in sync). Path is
  // remembered per-index in <dir>/syncfolder. Purely user-driven: a pass runs on
  // open, after a save/delete, and on the explicit "Sync now" control -- no
  // background polling. Empty = off.
  String _syncFolder = '';

  // Custom-scheme deep links (ais://sync?...). The native side (MainActivity /
  // AppDelegate) pushes live links as 'onLink' and holds a cold-start link for
  // 'getInitialLink'. Absent on desktop, where the calls just throw and are ignored.
  static const _linkChannel = MethodChannel('ais/deeplink');

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
      _status = 'Type tags, then Search. Tap Add to save.';
      _syncFolder = _loadSyncFolder();
      _loadTimeline(); // open showing recent items, not a blank search pane
      _runFolderSync(silent: true); // pull peer changes on open (opening is the user action)
    } catch (e) {
      _status = 'cannot open index: $e';
    }
    // Speech is initialized lazily on the first mic tap (see _listen), so the
    // permission prompt is tied to a user gesture rather than app launch.
    if (mounted) setState(() {});
    _wireDeepLinks();
  }


  // Custom-scheme deep links (ais://sync?...): register the live-link handler and
  // check for a link that cold-started the app. No plugin; the native side is thin.
  void _wireDeepLinks() {
    _linkChannel.setMethodCallHandler((call) async {
      if (call.method == 'onLink' && call.arguments is String) {
        await _handleLink(call.arguments as String);
      }
    });
    _linkChannel.invokeMethod<String>('getInitialLink').then((link) {
      if (link != null && link.isNotEmpty) _handleLink(link);
    }).catchError((_) {}); // no such channel on desktop; ignore
  }

  // A scanned ais://sync?host=IP:PORT&token=HEX link: the phone's own camera
  // opened it and the OS routed it here (no in-app scanner). Confirm first -- a
  // link can come from anywhere and a sync shares this device's records -- then join.
  Future<void> _handleLink(String link) async {
    if (_ais == null || !mounted) return;
    final Uri uri;
    try {
      uri = Uri.parse(link);
    } catch (_) {
      return;
    }
    if (uri.scheme != 'ais' || uri.host != 'sync') return;
    final host = uri.queryParameters['host'] ?? '';
    final token = uri.queryParameters['token'] ?? '';
    if (host.isEmpty || token.isEmpty) return;
    if (_syncBusy) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('A sync is already running. Finish it first.')));
      return;
    }
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Sync with this device?'),
        content: Text('Scanned $host.\n\nBoth devices will end up with the same '
            'records. Continue only if you just started Host on that device.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Sync')),
        ],
      ),
    );
    if (ok == true) await _runJoin('http://$host', token);
  }

  void _recall() {
    final keys = _normKeys(_q.text);
    if (_ais == null || keys.isEmpty) return;
    final t0 = DateTime.now();
    // Exclude ids still inside their Undo window, so a live re-query can't
    // resurrect a row the user just swiped away.
    final r = _ais!
        .recall(keys, orMode: false)
        .where((h) => !_pendingDelete.contains(h.id))
        .toList();
    final keysMap = {for (final h in r) h.id: _ais!.keysOf(h.id).trim()};
    setState(() {
      _query = keys;
      _searched = true;
      _textSearch = false; // a fresh key search leaves the text-fallback mode
      _ms = DateTime.now().difference(t0).inMilliseconds;
      _results = r;
      _resultKeys = keysMap;
    });
  }

  // Live/submitted search from the persistent header field. That field shows on
  // every tab, so typing must also bring the recall view forward -- otherwise
  // _recall() fills _results while the body still shows the timeline and the
  // header count desyncs from what's actually on screen.
  void _recallLive() {
    if (_view == 'recall') {
      _recall();
    } else {
      _setView('recall'); // flushes pending deletes, then _recall() (query non-empty)
    }
  }

  // The SECONDARY full-text fallback. Reached ONLY by an explicit tap on
  // "Search note text instead" from the empty key-search state -- never on
  // typing, debounce, submit, or view change. AIS stays key-first; this is the
  // "I forgot which tag I used" escape hatch, searching note VALUES via find().
  void _findText() {
    if (_ais == null || _query.isEmpty) return;
    final t0 = DateTime.now();
    final r = _ais!
        .find(_query)
        .where((h) => !_pendingDelete.contains(h.id))
        .toList();
    final keysMap = {for (final h in r) h.id: _ais!.keysOf(h.id).trim()};
    setState(() {
      _searched = true;
      _textSearch = true;
      _ms = DateTime.now().difference(t0).inMilliseconds;
      _results = r;
      _resultKeys = keysMap;
    });
  }

  void _setView(String v) {
    _flushPendingDeletes(); // commit pending deletes so a re-query can't resurrect them
    setState(() {
      _view = v;
      _textSearch = false; // leaving/returning to a view drops the text fallback
      _resultKeys = const {}; // stale on any view change; _recall repopulates
    });
    if (v == 'recall') {
      if (_q.text.trim().isNotEmpty) {
        _recall();
      } else {
        // empty query: blank the pane and show the hint, like the other GUIs
        setState(() {
          _results = const [];
          _searched = false;
          _status = 'Type tags, then Search.';
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
      // Hide ids inside their Undo window; take the cursor/more from the raw
      // page so pagination stays correct.
      _tl = page.where((r) => !_pendingDelete.contains(r.id)).toList();
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

  // Accept a comma as an optional key separator and ignore surplus whitespace,
  // so "home, wifi" and "home   wifi" both resolve to the tags home + wifi. The
  // engine tokenizes on spaces, so we just fold commas to spaces and collapse.
  String _normKeys(String s) =>
      s.replaceAll(',', ' ').trim().replaceAll(RegExp(r'\s+'), ' ');

  // A blob-backed value holds only an internal "blobs/<ts>.txt" path; the list
  // shows its resolved content via _display. In-place "Edit value" edits the raw
  // stored string, so on a blob it would show the path and, on Save, replace the
  // pointer with inline text -- orphaning the blob. So it's omitted for blobs,
  // the same way encrypted/away rows are.
  bool _isBlob(String v) => v.startsWith('blobs/');

  // A multi-line paste is stored out-of-line as a blob (blobs/<ts>.txt); the
  // record holds only that path, an internal detail. Blob resolution lives in
  // the C engine (ais_embed_display), shared with the CLI and `ais serve`, so
  // this viewer can't drift -- we never read blob files in Dart. Cache resolved
  // content by absolute path (blobs are immutable) to skip the FFI on rebuilds;
  // an absent blob resolves to its path (uncached), so _notHere still badges it.
  final Map<String, String> _blobCache = {};
  // Open a URL value in the external browser. Guards against launch failure so a
  // bad/unsupported link surfaces a hint instead of throwing.
  Future<void> _openUrl(String v) async {
    try {
      final uri = Uri.parse(v);
      if (!await canLaunchUrl(uri) ||
          !await launchUrl(uri, mode: LaunchMode.externalApplication)) {
        throw 'launch failed';
      }
    } catch (_) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("Couldn't open the link")));
    }
  }

  // Render a record's value. A URL is a tappable link (tap opens the external
  // browser; long-press/drag still selects and copies). Everything else keeps
  // the plain, verbatim rendering. Callers handle encrypted/blob cases first.
  // [maxLines] bounds a list row so a huge single-line paste can't blow up its
  // height; the detail page passes null to show the value in full.
  Widget _valueLabel(String v, ColorScheme cs, {int? maxLines}) {
    if (!_isUrl(v)) return SelectableText(_display(v), maxLines: maxLines);
    final shown = _display(v);
    // A link needs more than colour (colour-blind users can't see it) and must
    // be reachable by a screen reader / keyboard, which a bare TextSpan isn't.
    // Underline it and wrap it in a real link semantic.
    return Semantics(
      link: true,
      label: 'Link: $shown',
      child: SelectableText.rich(
        TextSpan(
          text: shown,
          style: TextStyle(
            color: cs.primary,
            decoration: TextDecoration.underline,
            decorationColor: cs.primary,
          ),
          recognizer: TapGestureRecognizer()..onTap = () => _openUrl(v),
        ),
        maxLines: maxLines,
      ),
    );
  }

  String _display(String v) {
    if (!v.startsWith('blobs/')) return v; // inline value: no FFI, verbatim
    final e = _ais;
    if (e == null) return v;
    final full = '$_dir/$v';
    final cached = _blobCache[full];
    if (cached != null) return cached;
    final shown = e.display(v);
    if (shown != v) _blobCache[full] = shown; // don't cache an absent/unresolved blob
    return shown;
  }

  // Hand the record's shareable text to the OS share sheet. For a URL the value
  // is the URL; otherwise it's the display text (same as Copy). Desktop/Linux
  // has limited share_plus support, so a failure is a graceful no-op with a hint
  // rather than a crash.
  Future<void> _share(String v) async {
    try {
      await SharePlus.instance.share(ShareParams(text: _display(v)));
    } catch (_) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("Sharing isn't available here")));
    }
  }

  // "Not here": the value points at a file that is absent on THIS device -- an
  // AIS blob not yet synced down, or a local-file reference added elsewhere.
  // http(s) URLs and inline text always resolve, so never a badge. Display-only.
  bool _notHere(String v) {
    try {
      var p = v;
      if (p.startsWith('aisc:@blobs/')) p = p.substring(6); // strip 'aisc:@'
      if (p.startsWith('blobs/')) return !File('$_dir/$p').existsSync();
      if (p.startsWith('http://') || p.startsWith('https://')) return false;
      if (p.startsWith('file://')) p = p.substring(7);
      final isLocal = p.startsWith('/') ||
          RegExp(r'^[A-Za-z]:[\\/]').hasMatch(p);
      if (isLocal) return !File(p).existsSync();
    } catch (_) {}
    return false;
  }

  // The subtle, muted "not on this device" marker reused by both lists.
  Widget _notHereBadge(ColorScheme cs) => Padding(
        padding: const EdgeInsets.only(left: 6),
        child: Tooltip(
          message: 'Not on this device. Open it on the desktop, or mount that disk.',
          child: Icon(Icons.cloud_off, size: 16, color: cs.outline),
        ),
      );

  // Switch the active index: type a folder path, reopen the engine there.
  // Sync (Receive): pull + merge from another device running `ais --export
  // --serve`. Off the UI isolate; every outcome gets a plain-language SnackBar.
  // The one-tap "Sync": pick a role, both devices converge (symmetric exchange).
  //
  // Two clearly-separated ways in, so they can't be confused:
  //   * A NEARBY DEVICE, live over Wi-Fi (Host / Join+scan) -- QR/camera.
  //   * A FILE you move by Drive / USB / email (Export / Import) -- no network.
  Future<void> _syncSheet() async {
    if (_ais == null) return;
    final choice = await showModalBottomSheet<String>(
      context: context,
      isScrollControlled: true, // the sheet can be tall (nearby + file + folder); let it scroll
      builder: (ctx) => SafeArea(
        child: SingleChildScrollView(
          child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
              child: Align(
                alignment: Alignment.centerLeft,
                child: Text('Sync with another device',
                    style: Theme.of(ctx).textTheme.titleMedium),
              ),
            ),
            // --- Live, over Wi-Fi: the camera/QR pairing path. ---
            _syncGroupLabel(ctx, 'A nearby device (same Wi-Fi)'),
            ListTile(
              leading: const Icon(Icons.wifi_tethering),
              title: const Text('Host a sync'),
              subtitle: const Text('Wait here; show a QR the other device scans'),
              onTap: () => Navigator.pop(ctx, 'host'),
            ),
            ListTile(
              leading: const Icon(Icons.qr_code_scanner),
              title: const Text('Join / scan a nearby device'),
              subtitle: const Text('Scan its QR with the camera, or type its address'),
              onTap: () => Navigator.pop(ctx, 'join'),
            ),
            const Divider(height: 24),
            // --- A file: no network; move it by Drive / USB / email. ---
            _syncGroupLabel(ctx, 'A file (move it by Drive, USB, or email)'),
            ListTile(
              leading: const Icon(Icons.save_alt),
              title: const Text('Export to a file'),
              subtitle: const Text('Save a copy of the whole index to a file'),
              onTap: () => Navigator.pop(ctx, 'export'),
            ),
            ListTile(
              leading: const Icon(Icons.folder_open),
              title: const Text('Import from a file'),
              subtitle: const Text('Merge in a file you exported on another device'),
              onTap: () => Navigator.pop(ctx, 'import'),
            ),
            const Divider(height: 24),
            // --- A shared folder: a tool like Syncthing keeps devices in sync. ---
            _syncGroupLabel(ctx, 'A shared folder (Syncthing or cloud)'),
            ListTile(
              leading: const Icon(Icons.folder_shared_outlined),
              title: Text(_syncFolder.isEmpty ? 'Set a sync folder' : 'Synced folder'),
              subtitle: Text(_syncFolder.isEmpty
                  // I5: a versioning cloud keeps old plaintext copies, which can
                  // defeat tombstones (a delete reappears); Syncthing does not.
                  ? 'Best with Syncthing. A versioning cloud (e.g. Dropbox) may keep deleted items.'
                  : _syncFolder),
              onTap: () => Navigator.pop(ctx, 'folder'),
            ),
            if (_syncFolder.isNotEmpty)
              ListTile(
                leading: const Icon(Icons.sync),
                title: const Text('Sync now'),
                subtitle: const Text('Pull peer changes and push yours'),
                onTap: () => Navigator.pop(ctx, 'folder-sync'),
              ),
            if (_syncFolder.isNotEmpty)
              ListTile(
                leading: const Icon(Icons.sync_disabled),
                title: const Text('Stop folder sync'),
                onTap: () => Navigator.pop(ctx, 'folder-off'),
              ),
            const SizedBox(height: 8),
          ],
        ),
        ),
      ),
    );
    switch (choice) {
      case 'host':
        await _syncHost();
        break;
      case 'join':
        await _syncJoin();
        break;
      case 'export':
        await _exportFile();
        break;
      case 'import':
        await _importFile();
        break;
      case 'folder':
        await _pickSyncFolder();
        break;
      case 'folder-sync':
        _runFolderSync(silent: false); // explicit "Sync now"
        break;
      case 'folder-off':
        setState(() => _syncFolder = '');
        await _saveSyncFolder('');
        break;
    }
  }

  // Read/persist the per-index sync-folder path (a plain file next to the store).
  String _loadSyncFolder() {
    try {
      final f = File('$_dir/syncfolder');
      if (f.existsSync()) return f.readAsStringSync().trim();
    } catch (_) {}
    return '';
  }

  Future<void> _saveSyncFolder(String path) async {
    try {
      await File('$_dir/syncfolder').writeAsString(path);
    } catch (_) {}
  }

  // Pick a shared folder and run the first sync pass. Desktop only for now: mobile
  // has no arbitrary-folder picker (Android's SAF returns a content:// URI, not a
  // POSIX path the C engine can open with opendir/fopen, and shared-storage paths
  // are gated by scoped storage). A device with Syncthing would need a real path;
  // wiring that safely is future work, so mobile says so plainly rather than half-fail.
  Future<void> _pickSyncFolder() async {
    if (_ais == null) return;
    final messenger = ScaffoldMessenger.of(context);
    String? dir;
    try {
      dir = await getDirectoryPath(); // desktop; mobile has no arbitrary-folder picker
    } catch (_) {
      dir = null;
    }
    if (dir == null || !mounted) {
      if (dir == null && mounted && (Platform.isAndroid || Platform.isIOS)) {
        messenger.showSnackBar(const SnackBar(
            content: Text('Folder sync is desktop-only for now.')));
      }
      return;
    }
    setState(() => _syncFolder = dir!);
    await _saveSyncFolder(dir);
    final ok = _ais!.syncFolder(dir);
    if (!mounted) return;
    messenger.showSnackBar(SnackBar(
        content: Text(ok ? 'Syncing with $dir' : 'Could not sync to that folder')));
    if (ok) _setView(_view);
  }

  // A LAN Host/Join sync runs on a BACKGROUND isolate holding the SAME engine handle;
  // flock on the shared fd does not exclude it, so a concurrent UI-isolate write would
  // race the store. Block a mutating action while any sync (even a hidden one) is in
  // flight; reads/browsing stay available. Returns true (and warns) if blocked.
  bool _syncBlocks() {
    if (!_syncBusy) return false;
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('A sync is running. Finish it first.')));
    }
    return true;
  }

  // One folder-sync pass (fast, local file I/O). Refreshes the view on a merge.
  // Skipped while a LAN sync holds the handle (avoids the same cross-isolate race).
  void _runFolderSync({bool silent = true}) {
    if (_ais == null || _syncFolder.isEmpty || _syncBusy) return;
    final ok = _ais!.syncFolder(_syncFolder);
    if (ok && mounted) _setView(_view);
    if (!silent && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(ok ? 'Folder synced' : 'Folder sync failed')));
    }
  }

  // A small primary-tinted section label for the two sync groups above.
  Widget _syncGroupLabel(BuildContext ctx, String text) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
        child: Align(
          alignment: Alignment.centerLeft,
          child: Text(text,
              style: Theme.of(ctx).textTheme.labelMedium?.copyWith(
                  color: Theme.of(ctx).colorScheme.primary)),
        ),
      );

  // Export the whole index to a single plaintext file (the "aisb" bundle) the
  // user can carry by any channel. Desktop gets a native Save dialog defaulting
  // to Downloads (a visible, shareable place -- not the .ais dotfolder); mobile
  // has no browsable filesystem, so it writes a temp copy and hands it to the OS
  // share sheet (Drive / email / Files). The write is fast, so it runs inline.
  Future<void> _exportFile() async {
    if (_ais == null) return;
    final messenger = ScaffoldMessenger.of(context);

    if (Platform.isAndroid || Platform.isIOS) {
      final tmp = await getTemporaryDirectory();
      final path = '${tmp.path}/ais-export.aisb';
      final rc = _ais!.exportBundle(path);
      if (!mounted) return;
      if (rc != 0) {
        messenger.showSnackBar(
            const SnackBar(content: Text('Could not write the export file.')));
        return;
      }
      try {
        await SharePlus.instance.share(
            ShareParams(files: [XFile(path)], text: 'AIS export'));
      } catch (_) {
        if (mounted) {
          messenger.showSnackBar(
              const SnackBar(content: Text("Sharing isn't available here")));
        }
      }
      return;
    }

    // Desktop: a real Save dialog, defaulting to Downloads.
    final downloads = await getDownloadsDirectory();
    final location = await getSaveLocation(
      acceptedTypeGroups: const [
        XTypeGroup(label: 'AIS bundle', extensions: ['aisb'])
      ],
      suggestedName: 'ais-export.aisb',
      initialDirectory: downloads?.path,
    );
    if (location == null || _ais == null) return; // cancelled
    final rc = _ais!.exportBundle(location.path);
    if (!mounted) return;
    messenger.showSnackBar(SnackBar(
        content: Text(rc == 0
            ? 'Exported to ${location.path}'
            : 'Could not write the file. Check the folder path.')));
  }

  // Import a plaintext bundle file and merge it into this index (same
  // tombstone-union LWW merge as live sync). Distinct from Join/scan: this
  // reads a file that reached this device by Drive / USB / email, no network.
  Future<void> _importFile() async {
    if (_ais == null) return;
    final messenger = ScaffoldMessenger.of(context);
    // On mobile the SAF picker filters by MIME, and the custom .aisb extension
    // maps to none -- so don't constrain the type group there, or the file
    // becomes unselectable. Desktop filters to .aisb for a tidy dialog.
    final mobile = Platform.isAndroid || Platform.isIOS;
    final file = await openFile(
      acceptedTypeGroups: mobile
          ? const []
          : const [XTypeGroup(label: 'AIS bundle', extensions: ['aisb'])],
      initialDirectory: mobile ? null : (await getDownloadsDirectory())?.path,
    );
    if (file == null || _ais == null) return; // cancelled
    final rc = _ais!.importBundle(file.path);
    if (!mounted) return;
    final String msg;
    switch (rc) {
      case 0:
        msg = 'Merged. This index now includes the file’s records.';
        break;
      case -2:
        msg = 'This file is from an incompatible version.';
        break;
      default:
        msg = 'Couldn’t read the file.';
    }
    messenger.showSnackBar(SnackBar(content: Text(msg)));
    if (rc == 0) _setView(_view); // refresh with merged records
  }

  // Join: connect to a device that is hosting; both converge (bidirectional).
  Future<void> _syncJoin() async {
    final urlCtrl = TextEditingController(text: 'http://');
    final tokCtrl = TextEditingController();
    final go = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Join a sync'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: urlCtrl,
              autofocus: true,
              decoration: const InputDecoration(
                  labelText: 'Address', hintText: 'http://192.168.1.5:8766'),
            ),
            TextField(
              controller: tokCtrl,
              onSubmitted: (_) => Navigator.pop(ctx, true), // Enter = Sync
              decoration: const InputDecoration(labelText: 'Token'),
            ),
            const SizedBox(height: 8),
            Text(
                'On the other device: open Sync, choose Host, and read off its '
                'address and token.',
                style: Theme.of(ctx).textTheme.bodySmall),
          ],
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Sync')),
        ],
      ),
    );
    if (go != true || _ais == null) return;
    final url = urlCtrl.text.trim();
    final token = tokCtrl.text.trim();
    if (url.isEmpty || token.isEmpty) return;
    await _runJoin(url, token);
  }

  // Run the bidirectional join for an address + token, whether typed into the
  // Join dialog or parsed from a scanned ais:// link. Blocks the UI while the
  // sync isolate holds the shared engine handle (a data race otherwise).
  Future<void> _runJoin(String url, String token) async {
    if (_ais == null || !mounted) return;
    final messenger = ScaffoldMessenger.of(context);
    if (_syncBusy) {
      messenger.showSnackBar(const SnackBar(
          content: Text('A sync is already running. Finish it first.')));
      return;
    }
    _flushPendingDeletes(); // commit any in-flight delete BEFORE the sync isolate starts
    _syncBusy = true;
    final fut = _ais!.pullAsync(url, token, bidir: true);
    final hidden = await showDialog<bool>(
          context: context,
          barrierDismissible: false,
          builder: (_) => _SyncWaitDialog(
              title: 'Join a sync',
              waiting: 'Syncing...',
              note: 'You can hide this; syncing continues in the background.',
              done: fut),
        ) ??
        false;
    final rc = await fut;
    _syncBusy = false;
    if (!mounted) return;
    if (hidden && rc != 0) return; // don't surprise with a late failure snackbar
    final String msg;
    switch (rc) {
      case 0:
        msg = 'Synced. Both devices now have the same records.';
        break;
      case -1:
        msg = 'That address looks wrong. Use http://host:port.';
        break;
      default:
        msg = 'Could not sync. Same Wi-Fi? Check the host is waiting and the token is right.';
    }
    messenger.showSnackBar(SnackBar(content: Text(msg)));
    if (rc == 0) _setView(_view); // refresh with merged records
  }

  // Host: wait for another device to join; both converge (bidirectional). The
  // address + token stay visible while waiting.
  Future<void> _syncHost() async {
    if (_ais == null) return;
    final messenger = ScaffoldMessenger.of(context);
    if (_syncBusy) {
      messenger.showSnackBar(const SnackBar(
          content: Text('A sync is already running. Finish it first.')));
      return;
    }
    final ip = await _lanIp();
    if (!mounted) return;
    if (ip == null) {
      messenger.showSnackBar(const SnackBar(
          content: Text("Couldn't find your Wi-Fi address. Are you on Wi-Fi?")));
      return;
    }
    const port = 8766;
    final token = _genToken();
    // Same ais:// pairing link the desktop web host encodes, so one QR format
    // feeds the one deep-link handler (see _handleLink). Another phone scans it
    // with its camera; a desktop can type the address + token instead.
    final link =
        'ais://sync?host=${Uri.encodeQueryComponent('$ip:$port')}&token=$token';
    final detail = 'http://$ip:$port\ntoken: $token\n\n'
        'desktop:  ais --sync http://$ip:$port --token $token';

    _flushPendingDeletes(); // commit any in-flight delete BEFORE the sync isolate starts
    _syncBusy = true;
    final fut = _ais!.serveAsync(port, token, bidir: true); // blocks up to ~120s
    final hidden = await showDialog<bool>(
          context: context,
          barrierDismissible: false,
          builder: (_) => _SyncWaitDialog(
              title: 'Host a sync',
              qrData: link,
              commandLabel: 'Or type the address and token on the other device:',
              command: detail,
              waiting: 'Waiting for the other device...',
              note: 'You can hide this; hosting keeps waiting in the background.',
              done: fut),
        ) ??
        false;
    final rc = await fut;
    _syncBusy = false;
    if (!mounted) return;
    // If the user hid the dialog and it then timed out, don't surprise them with
    // a late failure snackbar ~2 min later; a success is still worth announcing.
    if (hidden && rc != 0) return;
    final String msg;
    switch (rc) {
      case 0:
        msg = 'Synced. Both devices now have the same records.';
        break;
      case -3:
        msg = 'Port 8766 is busy. Is a sync already running? Try again in a moment.';
        break;
      default:
        msg = 'No device joined in time. Try again.';
    }
    messenger.showSnackBar(SnackBar(content: Text(msg)));
    if (rc == 0) _setView(_view);
  }

  // A random 128-bit token as 32 hex chars (the peer must supply the same one).
  String _genToken() {
    final r = Random.secure();
    return List.generate(
        16, (_) => r.nextInt(256).toRadixString(16).padLeft(2, '0')).join();
  }

  // This device's LAN IPv4: prefer a private-range (Wi-Fi/LAN) address over a
  // VPN/cellular one; fall back to the first non-loopback address. Null if none.
  Future<String?> _lanIp() async {
    try {
      final ifs = await NetworkInterface.list(
          type: InternetAddressType.IPv4, includeLoopback: false);
      String? fallback;
      for (final ni in ifs) {
        for (final a in ni.addresses) {
          if (a.isLoopback) continue;
          fallback ??= a.address;
          if (_isPrivate(a.address)) return a.address;
        }
      }
      return fallback;
    } catch (_) {}
    return null;
  }

  // RFC 1918 private ranges (10/8, 172.16/12, 192.168/16) = a LAN/Wi-Fi address.
  static bool _isPrivate(String ip) {
    final p = ip.split('.');
    if (p.length != 4) return false;
    final a = int.tryParse(p[0]) ?? 0, b = int.tryParse(p[1]) ?? 0;
    return a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
  }

  Future<void> _changeStore() async {
    final ctrl = TextEditingController(text: _dir);
    final picked = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Library'),
        content: TextField(
          controller: ctrl,
          autofocus: true,
          onSubmitted: (v) => Navigator.pop(ctx, v.trim()), // Enter = Open
          decoration: const InputDecoration(hintText: 'full path to a .ais Library folder'),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, ctrl.text.trim()), child: const Text('Open')),
        ],
      ),
    );
    if (picked == null || picked.isEmpty || picked == _dir) return;
    // Commit any in-flight swipe-delete against the CURRENT library BEFORE swapping.
    // Otherwise its deferred snackbar-close would fire del(id) on the NEW library and
    // silently delete an unrelated same-numbered record there.
    _flushPendingDeletes();
    try {
      // Open the new index FIRST; only if that succeeds do we close the old one
      // and swap. Otherwise a bad path would throw after close(), leaving _ais
      // pointing at a closed handle -- every later call a use-after-close.
      Directory(picked).createSync(recursive: true);
      final next = AisEngine(picked);
      _ais?.close();
      _ais = next;
      // Desktop: remember the choice the same way `ais --default` does, so the
      // next launch (and the CLI) opens it too. Mobile's index is fixed.
      if (!Platform.isAndroid && !Platform.isIOS) AisIndex.setDefault(picked);
      setState(() {
        _dir = picked;
        _syncFolder = _loadSyncFolder(); // sync-folder is per-index
        _results = const [];
        _resultKeys = const {};
        _tl = const [];
        _tlBefore = 0;
        _tlMore = false;
        _tlFrom = '';
        _tlTo = '';
        _tags = const [];
        _view = 'recall';
        _searched = false;
        _textSearch = false;
        _query = '';
        _q.clear();
      });
    } catch (e) {
      setState(() => _status = 'cannot open: $e');
    }
  }

  // Session-only theme picker (System/Light/Dark) driven from the overflow menu.
  // Feeds the module-level themeModeNotifier that AisApp listens on.
  Future<void> _pickTheme() async {
    final current = themeModeNotifier.value;
    final picked = await showDialog<ThemeMode>(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: const Text('Theme'),
        children: [
          for (final o in const [
            (ThemeMode.system, 'System'),
            (ThemeMode.light, 'Light'),
            (ThemeMode.dark, 'Dark'),
          ])
            ListTile(
              title: Text(o.$2),
              trailing:
                  o.$1 == current ? const Icon(Icons.check) : null,
              onTap: () => Navigator.pop(ctx, o.$1),
            ),
        ],
      ),
    );
    if (picked != null) themeModeNotifier.value = picked;
  }

  // Simple About dialog: app name + the current store path.
  void _showAbout() {
    showAboutDialog(
      context: context,
      applicationName: 'AIS',
      children: [
        Text(_dir.isEmpty ? 'Library: (default)' : 'Library: $_dir'),
      ],
    );
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
                // Translucent surface (not white) so the frosted strip reads in
                // light AND dark; the blur behind it still shows the list.
                color: cs.surface.withValues(alpha: 0.6),
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
                          style: Theme.of(context)
                              .textTheme
                              .bodySmall
                              ?.copyWith(color: cs.onSurfaceVariant),
                        ),
                      // Config home: gathers the store/sync/theme/about actions
                      // that used to be sub-48dp header text-links. Standard
                      // 48dp target, top-right where mainstream apps put it.
                      PopupMenuButton<String>(
                        icon: const Icon(Icons.more_vert),
                        tooltip: 'Settings',
                        onSelected: (a) async {
                          switch (a) {
                            case 'store':
                              await _changeStore();
                              break;
                            case 'sync':
                              await _syncSheet();
                              break;
                            case 'theme':
                              await _pickTheme();
                              break;
                            case 'about':
                              _showAbout();
                              break;
                          }
                        },
                        itemBuilder: (_) => [
                          PopupMenuItem(
                            value: 'store',
                            enabled: _ais != null,
                            child: const ListTile(
                              contentPadding: EdgeInsets.zero,
                              leading: Icon(Icons.folder_outlined),
                              title: Text('Change Library'),
                            ),
                          ),
                          PopupMenuItem(
                            value: 'sync',
                            enabled: _ais != null,
                            child: const ListTile(
                              contentPadding: EdgeInsets.zero,
                              leading: Icon(Icons.sync),
                              title: Text('Sync'),
                            ),
                          ),
                          const PopupMenuItem(
                            value: 'theme',
                            child: ListTile(
                              contentPadding: EdgeInsets.zero,
                              leading: Icon(Icons.brightness_6_outlined),
                              title: Text('Theme'),
                            ),
                          ),
                          const PopupMenuItem(
                            value: 'about',
                            child: ListTile(
                              contentPadding: EdgeInsets.zero,
                              leading: Icon(Icons.info_outline),
                              title: Text('About'),
                            ),
                          ),
                        ],
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
                            onSubmitted: (_) => _recallLive(),
                            onChanged: (v) {
                              // Debounced live filter: reschedule on each
                              // keystroke, run _recall() once typing pauses.
                              _debounce?.cancel();
                              if (v.trim().isEmpty) {
                                // cleared: fall back to the hint, don't search
                                setState(() {
                                  _results = const [];
                                  _resultKeys = const {};
                                  _searched = false;
                                  _textSearch = false; // cleared query drops the fallback too
                                  _status = 'Type tags, then Search.';
                                });
                                return;
                              }
                              _debounce = Timer(
                                  const Duration(milliseconds: 280), _recallLive);
                            },
                            decoration: InputDecoration(
                              hintText: 'type tags to filter',
                              prefixIcon: const Icon(Icons.search),
                              filled: true,
                              fillColor: cs.surfaceContainerHighest
                                  .withValues(alpha: 0.85),
                              border: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(28),
                                borderSide: BorderSide.none,
                              ),
                              // mic on mobile so the first tap can request permission
                              suffixIcon:
                                  (_voice || Platform.isAndroid || Platform.isIOS)
                                      ? IconButton(
                                          icon: const Icon(Icons.mic),
                                          tooltip: 'Voice search',
                                          onPressed: _listen)
                                      : null,
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        FilledButton(
                          onPressed: () => _setView('recall'),
                          style: FilledButton.styleFrom(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 22, vertical: 18),
                            shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(24)),
                          ),
                          child: const Text('Search'),
                        ),
                      ],
                    ),
                    const SizedBox(height: 4),
                    // Store path stays visible as a NON-interactive muted label;
                    // changing it (and sync) now live in the top-right menu.
                    Text(
                      _dir.isEmpty ? 'Library: (default)' : 'Library: $_dir',
                      style: Theme.of(context)
                          .textTheme
                          .bodySmall
                          ?.copyWith(color: cs.onSurfaceVariant),
                      overflow: TextOverflow.ellipsis,
                    ),
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
      // Float the bar over the soft keyboard: Scaffold resizes the body above
      // the keyboard but not this slot, so pad it up by the keyboard height
      // (animated). The FAB anchors above the bar, so it rides up too.
      bottomNavigationBar: AnimatedPadding(
        duration: const Duration(milliseconds: 100),
        padding: EdgeInsets.only(bottom: MediaQuery.of(context).viewInsets.bottom),
        child: NavigationBar(
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
              label: 'Recent'),
          NavigationDestination(
              icon: Icon(Icons.label_outline),
              selectedIcon: Icon(Icons.label),
              label: 'Tags'),
        ],
        ),
      ),
    );
  }

  Widget _centerMsg(String msg, ColorScheme cs) => Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Text(msg, textAlign: TextAlign.center, style: TextStyle(color: cs.onSurfaceVariant)),
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

  // The standard delete: pull the row from the visible list NOW and offer Undo
  // in a SnackBar; only touch the engine if the window closes without an Undo
  // tap. No engine-side undo needed -- del() is deferred, not reversed. Swipe
  // and the ⋮ "Delete" menu both route here, so they behave identically.
  // Commit a still-pending delete for real. Idempotent: only fires if the id is
  // still pending (not undone, not already committed). Input-driven so it never
  // depends on the snackbar's animated close alone.
  void _commitDelete(int id) {
    if (_pendingDelete.remove(id) && _ais != null) _ais!.del(id);
    // A committed delete must not keep offering UNDO -- that would only
    // fake-restore a record the engine no longer has. Dismiss its snackbar if
    // it is still up. (When the commit comes from the snackbar's own .closed
    // callback, the id is already off the map, so this is a no-op there.)
    _delSnack.remove(id)?.close();
  }

  // Flush every pending delete now -- called before a new delete and before any
  // view re-query, so a "deleted" row can never resurrect on refresh.
  void _flushPendingDeletes() {
    for (final id in _pendingDelete.toList()) {
      _commitDelete(id);
    }
  }

  void _deferDelete(int id) {
    if (_syncBlocks()) {
      if (mounted) _setView(_view); // restore a swipe-dismissed row (nothing was deleted)
      return;
    }
    _flushPendingDeletes(); // a new delete commits any prior still-pending one
    final messenger = ScaffoldMessenger.of(context);
    // Snapshot from whichever list(s) hold the id, so Undo restores it in place.
    final recallIdx = _results.indexWhere((h) => h.id == id);
    final Hit? recallHit = recallIdx >= 0 ? _results[recallIdx] : null;
    final tlIdx = _tl.indexWhere((r) => r.id == id);
    final TlRow? tlRow = tlIdx >= 0 ? _tl[tlIdx] : null;
    if (recallHit == null && tlRow == null) {
      // The id left both loaded lists during an async gap (e.g. an Edit-tags
      // re-query dropped it before Detail -> Delete ran). Don't silently no-op:
      // honor the delete outright. No snapshot to restore, so there's no Undo,
      // just a plain confirmation.
      if (_ais != null) _ais!.del(id);
      messenger.showSnackBar(const SnackBar(content: Text('Deleted')));
      return;
    }
    _pendingDelete.add(id);
    setState(() {
      if (recallHit != null) _results = [..._results]..removeAt(recallIdx);
      if (tlRow != null) _tl = [..._tl]..removeAt(tlIdx);
    });
    var undone = false;
    final ctl = messenger.showSnackBar(SnackBar(
      content: const Text('Deleted'),
      duration: const Duration(seconds: 4),
      action: SnackBarAction(
        label: 'UNDO',
        onPressed: () {
          // If the delete already committed (a later delete or a view change
          // flushed it), the snackbar should be gone -- but guard anyway so
          // UNDO can never fake-restore a record the engine no longer has.
          if (!_pendingDelete.contains(id)) return;
          undone = true;
          _pendingDelete.remove(id);
          setState(() {
            if (recallHit != null) {
              final l = [..._results];
              l.insert(recallIdx.clamp(0, l.length), recallHit);
              _results = l;
            }
            if (tlRow != null) {
              final l = [..._tl];
              l.insert(tlIdx.clamp(0, l.length), tlRow);
              _tl = l;
            }
          });
        },
      ),
    ));
    _delSnack[id] = ctl;
    ctl.closed.then((_) {
      _delSnack.remove(id);
      // Closed for any reason other than an Undo tap: commit the delete. No-op if
      // it was already flushed by a later delete or a view change.
      if (undone) return;
      _commitDelete(id);
      // Re-sync the visible view now the delete is real. Guard on _pendingDelete
      // so a re-query can't resurrect a row still inside its own Undo window.
      if (_pendingDelete.isEmpty && mounted) {
        _setView(_view);
        _runFolderSync(silent: true); // the delete settled: push it to peers
      }
    });
  }

  // The red swipe-away background revealed under an endToStart Dismissible.
  Widget _deleteBg(ColorScheme cs) => Container(
        color: cs.error,
        alignment: Alignment.centerRight,
        padding: const EdgeInsets.only(right: 24),
        child: Icon(Icons.delete, color: cs.onError),
      );

  // A friendly, actionable empty / first-run state: an icon, a line about what
  // AIS is for, and a filled button that opens the same Add sheet as the FAB.
  Widget _emptyState(ColorScheme cs,
          {required IconData icon, required String line}) =>
      Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 48, color: cs.outline),
              const SizedBox(height: 16),
              Text(line,
                  textAlign: TextAlign.center,
                  style: Theme.of(context)
                      .textTheme
                      .bodyLarge
                      ?.copyWith(color: cs.onSurfaceVariant)),
              const SizedBox(height: 20),
              FilledButton.icon(
                onPressed: _ais == null ? null : _showAdd,
                icon: const Icon(Icons.add),
                label: const Text('Add something'),
              ),
            ],
          ),
        ),
      );

  // Edit a record's tags as chips: each × detaches a tag, the field appends a
  // new one; on Apply we send the minimal +tag/-tag delta to the engine.
  Future<void> _editKeys(Hit hit) async {
    if (_ais == null) return;
    if (_syncBlocks()) return; // don't write while a sync holds the handle
    final original = _ais!.keysOf(hit.id).split(RegExp(r'\s+'))
        .where((t) => t.isNotEmpty).toList();
    final tags = [...original];
    final ctrl = TextEditingController();
    final focus = FocusNode();

    // Split on spaces/commas so a pasted (or submitted) multi-word string
    // becomes one chip PER tag -- matching how the engine tokenizes on update,
    // so the chips never disagree with what gets stored.
    void addToken(StateSetter setDlg, String raw) {
      final parts =
          raw.split(RegExp(r'[,\s]+')).where((p) => p.isNotEmpty).toList();
      final fresh = parts.where((p) => !tags.contains(p)).toList();
      if (fresh.isEmpty) return;
      setDlg(() => tags.addAll(fresh));
    }

    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setDlg) => AlertDialog(
          title: const Text('Edit tags'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (tags.isNotEmpty)
                Wrap(
                  spacing: 6,
                  runSpacing: 4,
                  children: [
                    for (final t in tags)
                      InputChip(
                        label: Text(t),
                        visualDensity: VisualDensity.compact,
                        onDeleted: () => setDlg(() => tags.remove(t)),
                      ),
                  ],
                ),
              const SizedBox(height: 8),
              TextField(
                controller: ctrl,
                focusNode: focus,
                autofocus: true,
                decoration: const InputDecoration(hintText: 'Add a tag'),
                onChanged: (v) {
                  // a space or comma commits the token in progress
                  if (v.endsWith(' ') || v.endsWith(',')) {
                    addToken(setDlg, v.substring(0, v.length - 1));
                    ctrl.clear();
                  }
                },
                onSubmitted: (v) {
                  addToken(setDlg, v);
                  ctrl.clear();
                  focus.requestFocus();
                },
              ),
            ],
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Apply')),
          ],
        ),
      ),
    );
    if (ok != true || _ais == null) return;
    // fold any token(s) still sitting in the field into the final set
    for (final p in ctrl.text.split(RegExp(r'[,\s]+')).where((p) => p.isNotEmpty)) {
      if (!tags.contains(p)) tags.add(p);
    }
    final delta = <String>[
      for (final t in original) if (!tags.contains(t)) '-$t',
      for (final t in tags) if (!original.contains(t)) t,
    ].join(' ');
    if (delta.isEmpty) return;
    _ais!.update(hit.id, delta);
    if (mounted) {
      ScaffoldMessenger.of(context)
          .showSnackBar(const SnackBar(content: Text('Tags updated')));
    }
    _setView(_view); // refresh whichever view is showing (matches _editValue),
    // not a blind _recall() that no-ops off the recall tab and leaves stale tags
  }

  // Fix a record's value in place: the engine keeps its id and timeline slot.
  // Only offered for plain (non-encrypted, here) values — the raw text edits
  // safely. Encrypted/away rows omit the menu item instead.
  // Returns the new value when it actually changed (so a caller — e.g. the
  // detail page — can refresh its display), or null on cancel/no-op/failure.
  Future<String?> _editValue(int id, String oldValue) async {
    if (_ais == null) return null;
    if (_syncBlocks()) return null; // don't write while a sync holds the handle
    final messenger = ScaffoldMessenger.of(context);
    final ctrl = TextEditingController(text: oldValue);
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Edit value'),
        content: TextField(
          controller: ctrl,
          autofocus: true,
          keyboardType: TextInputType.multiline,
          minLines: 1,
          maxLines: 8,
          decoration: const InputDecoration(border: OutlineInputBorder()),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Save')),
        ],
      ),
    );
    if (ok != true || _ais == null) return null;
    final newValue = ctrl.text;
    // no-op on empty (after trim) or unchanged; don't trim into the stored value
    if (newValue.trim().isEmpty || newValue == oldValue) return null;
    final done = _ais!.setValue(id, oldValue, newValue);
    if (!mounted) return null;
    messenger.showSnackBar(SnackBar(
        content: Text(done ? 'Value updated' : "Couldn't update the value")));
    if (done) _setView(_view); // refresh whichever view is showing
    return done ? newValue : null;
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

  // A full-screen detail/edit page for one record — the natural home for its
  // actions. Opened by TAPPING a recall or timeline row (the row ⋮ menu stays
  // as a secondary path). Reuses the same handlers the menu uses (_editValue /
  // _editKeys / _share / _deferDelete) and refreshes its own value/tags after
  // an in-place edit, so a change shows without going back. `ts` is the save
  // time when known (timeline rows carry it; recall hits don't).
  void _openDetail(int id, String value, {String ts = ''}) {
    Navigator.of(context).push(MaterialPageRoute(
      builder: (routeCtx) {
        // Local, mutable copies so an in-place edit can refresh this page.
        var curValue = value;
        var keys = _ais?.keysOf(id).trim() ?? '';
        return StatefulBuilder(
          builder: (ctx, setLocal) {
            final cs = Theme.of(ctx).colorScheme;
            final isSecret = curValue.startsWith('aisc:');
            final away = _notHere(curValue);
            final tagList = keys
                .split(RegExp(r'\s+'))
                .where((t) => t.isNotEmpty)
                .toList();
            // Save time (local): date, plus HH:MM when the ts carries a clock.
            String prettyTs() {
              final dt = ts.isEmpty ? null : DateTime.tryParse(ts)?.toLocal();
              if (dt == null) return '';
              String p2(int n) => n.toString().padLeft(2, '0');
              final d = '${dt.year}-${p2(dt.month)}-${p2(dt.day)}';
              final t =
                  ts.contains('T') ? ' · ${p2(dt.hour)}:${p2(dt.minute)}' : '';
              return '${_fmtDay(d)}$t';
            }

            final when = prettyTs();
            return Scaffold(
              appBar: AppBar(
                title: const Text('Details'),
                actions: [
                  if (isSecret)
                    IconButton(
                      icon: const Icon(Icons.lock_open),
                      tooltip: 'Reveal',
                      onPressed: () => _revealHit(Hit(id, curValue)),
                    ),
                  IconButton(
                    icon: const Icon(Icons.copy_outlined),
                    tooltip: 'Copy',
                    onPressed: () {
                      Clipboard.setData(ClipboardData(text: _display(curValue)));
                      ScaffoldMessenger.of(ctx).showSnackBar(
                          const SnackBar(content: Text('Copied')));
                    },
                  ),
                  // sharing ciphertext / a missing blob is useless; gate like the row
                  if (!isSecret && !away)
                    IconButton(
                      icon: const Icon(Icons.share_outlined),
                      tooltip: 'Share',
                      onPressed: () => _share(curValue),
                    ),
                  PopupMenuButton<String>(
                    icon: const Icon(Icons.more_vert),
                    tooltip: 'More',
                    onSelected: (a) async {
                      if (a == 'value') {
                        // reuse the row's editor; refresh on a real change
                        final nv = await _editValue(id, curValue);
                        if (nv != null) setLocal(() => curValue = nv);
                      } else if (a == 'tags') {
                        await _editKeys(Hit(id, curValue));
                        // re-read the tags so the chips reflect the edit
                        setLocal(() => keys = _ais?.keysOf(id).trim() ?? '');
                      } else if (a == 'delete') {
                        Navigator.of(ctx).pop(); // back to the list first…
                        _deferDelete(id); // …then remove it with an Undo snackbar
                      }
                    },
                    itemBuilder: (_) => [
                      // plain values edit as raw text; encrypted/away/blob rows omit this
                      if (!isSecret && !away && !_isBlob(curValue))
                        const PopupMenuItem(
                            value: 'value', child: Text('Edit value')),
                      const PopupMenuItem(value: 'tags', child: Text('Edit tags')),
                      const PopupMenuItem(value: 'delete', child: Text('Delete')),
                    ],
                  ),
                ],
              ),
              body: ListView(
                padding: const EdgeInsets.all(20),
                children: [
                  // The value in full: selectable, tappable when a URL. Encrypted
                  // and away rows keep the same treatment they have in the list.
                  if (isSecret)
                    Row(children: [
                      Icon(Icons.lock_outline, size: 18, color: cs.outline),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text('encrypted, tap Reveal to read',
                            style: TextStyle(color: cs.onSurfaceVariant)),
                      ),
                    ])
                  else
                    Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Expanded(
                          child: DefaultTextStyle.merge(
                            style: Theme.of(ctx).textTheme.titleMedium,
                            child: _valueLabel(curValue, cs),
                          ),
                        ),
                        if (away) _notHereBadge(cs),
                      ],
                    ),
                  const SizedBox(height: 24),
                  // Tags as chips; tap one to recall everything filed under it.
                  Text('Tags',
                      style: Theme.of(ctx)
                          .textTheme
                          .labelMedium
                          ?.copyWith(color: cs.primary)),
                  const SizedBox(height: 8),
                  if (tagList.isEmpty)
                    Text('(no tags)', style: TextStyle(color: cs.onSurfaceVariant))
                  else
                    Wrap(
                      spacing: 6,
                      runSpacing: 4,
                      children: [
                        for (final t in tagList)
                          ActionChip(
                            label: Text(t),
                            onPressed: () {
                              Navigator.of(ctx).pop();
                              _q.text = t;
                              _setView('recall');
                            },
                          ),
                      ],
                    ),
                  if (when.isNotEmpty) ...[
                    const SizedBox(height: 24),
                    Text('Saved',
                        style: Theme.of(ctx)
                            .textTheme
                            .labelMedium
                            ?.copyWith(color: cs.primary)),
                    const SizedBox(height: 4),
                    Text(when,
                        style: Theme.of(ctx)
                            .textTheme
                            .bodyMedium
                            ?.copyWith(color: cs.onSurfaceVariant)),
                  ],
                ],
              ),
            );
          },
        );
      },
    ));
  }

  Widget _recallBody(ColorScheme cs) {
    if (_results.isEmpty) {
      // Text fallback already ran (via explicit tap) and also found nothing.
      if (_textSearch) return _centerMsg('No text match either.', cs);
      // Key search found nothing: offer the SECONDARY full-text fallback, not
      // a dead end. This is the only place the offer appears.
      if (_searched && _query.isNotEmpty) return _noTagMatch(cs);
      if (_searched) return _centerMsg('No results for "$_query"', cs);
      // First-run / not-yet-searched: an error keeps its plain message; a healthy
      // engine shows the friendly, actionable empty state instead of a bare hint.
      if (_ais == null) return _centerMsg(_status, cs);
      return _emptyState(cs,
          icon: Icons.note_add_outlined,
          line: 'Save a link, a note, or a fact, then find it later by its tags.');
    }
    // find() results reuse the exact recall row builder; a quiet header above the
    // list is the only thing marking them as TEXT matches rather than tag matches.
    final list = ListView.separated(
      padding: const EdgeInsets.only(bottom: 88),
      itemCount: _results.length,
      separatorBuilder: (_, __) => const Divider(height: 1),
      itemBuilder: (_, i) {
        final hit = _results[i];
        final v = hit.value;
        final isSecret = v.startsWith('aisc:');
        final away = _notHere(v);
        return Dismissible(
          key: ValueKey(hit.id),
          direction: DismissDirection.endToStart,
          background: _deleteBg(cs),
          onDismissed: (_) => _deferDelete(hit.id),
          child: ListTile(
          // Primary path: tap the row to open its detail/edit page (the ⋮ menu
          // on the right stays as a quick secondary path).
          onTap: () => _openDetail(hit.id, v),
          title: Row(mainAxisSize: MainAxisSize.min, children: [
            Flexible(
              child: isSecret
                  ? Row(mainAxisSize: MainAxisSize.min, children: [
                      Icon(Icons.lock_outline, size: 16, color: cs.outline),
                      const SizedBox(width: 6),
                      Text('encrypted', style: TextStyle(color: cs.onSurfaceVariant)),
                    ])
                  : _valueLabel(v, cs, maxLines: 3),
            ),
            if (away) _notHereBadge(cs),
          ]),
          // Show the record's tags (fetched once when results loaded), so a recall
          // row reads the same as a timeline row rather than value-only.
          subtitle: Text(
            (_resultKeys[hit.id]?.isNotEmpty ?? false)
                ? _resultKeys[hit.id]!
                : '(no tags)',
            style: Theme.of(context)
                .textTheme
                .bodyMedium
                ?.copyWith(color: cs.onSurfaceVariant),
          ),
          trailing: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (isSecret)
                IconButton(
                  icon: const Icon(Icons.lock_open),
                  tooltip: 'Reveal',
                  onPressed: () => _revealHit(hit),
                ),
              // copy / edit keys / delete behind one overflow -> clean rows
              PopupMenuButton<String>(
                icon: const Icon(Icons.more_vert),
                tooltip: 'More',
                onSelected: (a) {
                  if (a == 'copy') {
                    Clipboard.setData(ClipboardData(text: _display(v)));
                    ScaffoldMessenger.of(context)
                        .showSnackBar(const SnackBar(content: Text('Copied')));
                  } else if (a == 'share') {
                    _share(v);
                  } else if (a == 'value') {
                    _editValue(hit.id, v);
                  } else if (a == 'edit') {
                    _editKeys(hit);
                  } else if (a == 'delete') {
                    _deferDelete(hit.id);
                  }
                },
                itemBuilder: (_) => [
                  const PopupMenuItem(value: 'copy', child: Text('Copy')),
                  // sharing ciphertext / a missing blob is useless; gate like Edit value
                  if (!isSecret && !away)
                    const PopupMenuItem(value: 'share', child: Text('Share')),
                  // plain values edit as raw text; encrypted/away/blob rows omit this
                  if (!isSecret && !away && !_isBlob(v))
                    const PopupMenuItem(value: 'value', child: Text('Edit value')),
                  const PopupMenuItem(value: 'edit', child: Text('Edit tags')),
                  const PopupMenuItem(value: 'delete', child: Text('Delete')),
                ],
              ),
            ],
          ),
          ),
        );
      },
    );
    if (_textSearch) {
      return Column(children: [_textMatchHeader(cs), Expanded(child: list)]);
    }
    return list;
  }

  // The empty key-search state: state plainly that no TAG matched, then offer the
  // secondary full-text fallback as a quiet TextButton -- not a primary action.
  Widget _noTagMatch(ColorScheme cs) => Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text('No tag match for "$_query".',
                  textAlign: TextAlign.center,
                  style: Theme.of(context)
                      .textTheme
                      .bodyLarge
                      ?.copyWith(color: cs.onSurfaceVariant)),
              const SizedBox(height: 12),
              TextButton.icon(
                onPressed: _findText,
                icon: const Icon(Icons.search, size: 18),
                label: const Text('Search note text instead'),
              ),
            ],
          ),
        ),
      );

  // A thin, quiet marker above find() results so the user knows these are note-
  // TEXT matches, not tag matches -- the fallback, kept visually subordinate.
  Widget _textMatchHeader(ColorScheme cs) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 4),
        child: Row(children: [
          Icon(Icons.search, size: 14, color: cs.onSurfaceVariant),
          const SizedBox(width: 6),
          Expanded(
            child: Text('Text matches for "$_query"',
                style: Theme.of(context)
                    .textTheme
                    .bodySmall
                    ?.copyWith(color: cs.onSurfaceVariant)),
          ),
        ]),
      );

  // Page on with the keyset cursor: fetch the next page of records older than
  // the last id we hold, append it, and advance the cursor.
  void _loadMoreTimeline() {
    if (_ais == null) return;
    final page = _ais!.timeline(before: _tlBefore, count: _tlPage, from: _tlFrom, to: _tlTo);
    setState(() {
      _tl = [..._tl, ...page.where((r) => !_pendingDelete.contains(r.id))];
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
          onPressed: () => _pickDate(isFrom: true),
        ),
        const SizedBox(width: 8),
        InputChip(
          label: Text(_tlTo.isEmpty ? 'To' : 'To: $_tlTo'),
          onPressed: () => _pickDate(isFrom: false),
        ),
        if (on)
          IconButton(
            icon: const Icon(Icons.clear, size: 18),
            tooltip: 'Clear range',
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
        Expanded(
          child: _emptyState(cs,
              icon: Icons.note_add_outlined,
              line: 'Nothing saved yet. Add your first note or link.'),
        ),
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
              style: Theme.of(context)
                  .textTheme
                  .titleSmall
                  ?.copyWith(color: cs.onSurface)),
        ));
      }
      final time = (dt != null && r.ts.contains('T')) ? '${p2(dt.hour)}:${p2(dt.minute)} · ' : '';
      items.add(Dismissible(
        key: ValueKey(r.id),
        direction: DismissDirection.endToStart,
        background: _deleteBg(cs),
        onDismissed: (_) => _deferDelete(r.id),
        child: ListTile(
        // Tap opens the detail/edit page; carry the ts so it can show the save
        // time. The ⋮ menu stays as the secondary path.
        onTap: () => _openDetail(r.id, r.value, ts: r.ts),
        title: Row(mainAxisSize: MainAxisSize.min, children: [
          Flexible(
            child: r.value.startsWith('aisc:')
                ? Row(mainAxisSize: MainAxisSize.min, children: [
                    Icon(Icons.lock_outline, size: 16, color: cs.outline),
                    const SizedBox(width: 6),
                    Text('encrypted', style: TextStyle(color: cs.outline)),
                  ])
                : _valueLabel(r.value, cs, maxLines: 3),
          ),
          if (_notHere(r.value)) _notHereBadge(cs),
        ]),
        subtitle: Text('$time${r.keys.isEmpty ? '(no tags)' : r.keys}',
            style: Theme.of(context)
                .textTheme
                .bodyMedium
                ?.copyWith(color: cs.onSurfaceVariant)),
        // same copy / edit tags / delete overflow as recall rows, so items
        // found only in the timeline (e.g. a keyless add) stay actionable.
        trailing: PopupMenuButton<String>(
          icon: const Icon(Icons.more_vert),
          tooltip: 'More',
          onSelected: (a) {
            final hit = Hit(r.id, r.value);
            if (a == 'copy') {
              Clipboard.setData(ClipboardData(text: _display(r.value)));
              ScaffoldMessenger.of(context)
                  .showSnackBar(const SnackBar(content: Text('Copied')));
            } else if (a == 'share') {
              _share(r.value);
            } else if (a == 'value') {
              _editValue(r.id, r.value);
            } else if (a == 'edit') {
              _editKeys(hit);
            } else if (a == 'delete') {
              _deferDelete(r.id);
            }
          },
          itemBuilder: (_) => [
            const PopupMenuItem(value: 'copy', child: Text('Copy')),
            // sharing ciphertext / a missing blob is useless; gate like Edit value
            if (!r.value.startsWith('aisc:') && !_notHere(r.value))
              const PopupMenuItem(value: 'share', child: Text('Share')),
            // plain values edit as raw text; encrypted/away/blob rows omit this
            if (!r.value.startsWith('aisc:') && !_notHere(r.value) && !_isBlob(r.value))
              const PopupMenuItem(value: 'value', child: Text('Edit value')),
            const PopupMenuItem(value: 'edit', child: Text('Edit tags')),
            const PopupMenuItem(value: 'delete', child: Text('Delete')),
          ],
        ),
      ),
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
    bool ppShow = false;                  // reveal toggle for the sealing passphrase
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
            Text('Add to your memory',
                style: Theme.of(ctx).textTheme.titleMedium),
            const SizedBox(height: 14),
            TextField(
              controller: keysCtrl,
              decoration: const InputDecoration(
                labelText: 'Tags (space-separated, optional)',
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
              Switch(
                value: encrypt,
                onChanged: (b) => setSheet(() => encrypt = b),
              ),
              const Text('Encrypt'),
            ]),
            if (encrypt)
              Padding(
                padding: const EdgeInsets.only(bottom: 4),
                child: TextField(
                  controller: ppCtrl,
                  obscureText: !ppShow,
                  decoration: InputDecoration(
                    labelText: 'Passphrase',
                    border: const OutlineInputBorder(),
                    suffixIcon: IconButton(
                      icon: Icon(ppShow ? Icons.visibility_off : Icons.visibility),
                      tooltip: ppShow ? 'Hide' : 'Show',
                      onPressed: () => setSheet(() => ppShow = !ppShow),
                    ),
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
                      if (_syncBlocks()) return; // don't write while a sync holds the handle
                      final keys = _normKeys(keysCtrl.text);
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
                      // Actually SHOW what was just saved: switch to the recall view
                      // under its tags, or to Recent for a keyless save (it lives only
                      // in the timeline). Bare _recall() would fill results without
                      // leaving the current tab -> the saved item stays invisible.
                      if (keys.isEmpty) {
                        _setView('timeline');
                      } else {
                        _setView('recall');
                      }
                      _runFolderSync(silent: true); // push the new record to peers
                      messenger.showSnackBar(SnackBar(
                          content: Text(keys.isEmpty ? 'Saved (no tags)' : 'Saved under: $keys')));
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
    _debounce?.cancel();
    _ais?.close();
    super.dispose();
  }
}

// A non-dismissible barrier dialog shown while a sync FFI call blocks: it keeps
// the UI from touching the shared engine handle during the sync, shows an
// optional desktop command (send), and closes itself when the future completes
// (peer done, or timeout). Used by both receive and send.
class _SyncWaitDialog extends StatefulWidget {
  final String title;
  final String? qrData;       // ais:// pairing link; shown as a QR on host, null hides it
  final String? commandLabel; // line shown above the command (host); null hides it
  final String? command;      // shown on host; null on join
  final String waiting;
  final String? note;         // honest caption: hiding does NOT stop the sync
  final Future<int> done;
  const _SyncWaitDialog(
      {required this.title,
      this.qrData,
      this.commandLabel,
      this.command,
      required this.waiting,
      this.note,
      required this.done});
  @override
  State<_SyncWaitDialog> createState() => _SyncWaitDialogState();
}

class _SyncWaitDialogState extends State<_SyncWaitDialog> {
  @override
  void initState() {
    super.initState();
    // Auto-close when the sync finishes; pop(false) marks "completed on its own"
    // so the caller can tell it apart from the user hiding it (pop(true)).
    widget.done.whenComplete(() {
      if (mounted) Navigator.of(context).pop(false);
    });
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
        title: Text(widget.title),
        // Bound the width: QrImageView is a CustomPaint with no intrinsic size,
        // so an unbounded-width AlertDialog content collapses it and the whole
        // card never paints (the Host pane bug; Join has no QR, so it was fine).
        content: SizedBox(
          width: 260,
          child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (widget.qrData != null) ...[
              Container(
                color: Colors.white,
                padding: const EdgeInsets.all(8),
                child: QrImageView(
                  data: widget.qrData!,
                  version: QrVersions.auto,
                  size: 180,
                  backgroundColor: Colors.white,
                  errorStateBuilder: (ctx, err) => const SizedBox(
                    width: 180,
                    height: 180,
                    child: Center(child: Text('QR unavailable')),
                  ),
                ),
              ),
              const SizedBox(height: 8),
              Text("Scan with the other phone's camera to join.",
                  style: Theme.of(context).textTheme.bodySmall),
              const SizedBox(height: 16),
            ],
            if (widget.command != null) ...[
              Text(widget.commandLabel ?? ''),
              const SizedBox(height: 8),
              SelectableText(widget.command!,
                  style: Theme.of(context)
                      .textTheme
                      .bodySmall
                      ?.copyWith(fontFamily: 'monospace')),
              const SizedBox(height: 16),
            ],
            Row(mainAxisSize: MainAxisSize.min, children: [
              const SizedBox(
                  width: 16,
                  height: 16,
                  child: CircularProgressIndicator(strokeWidth: 2)),
              const SizedBox(width: 12),
              Flexible(child: Text(widget.waiting)),
            ]),
            if (widget.note != null) ...[
              const SizedBox(height: 12),
              Text(widget.note!,
                  style: Theme.of(context)
                      .textTheme
                      .bodySmall
                      ?.copyWith(color: Theme.of(context).colorScheme.onSurfaceVariant)),
            ],
          ],
        )),
        actions: [
          // "Hide", not "Cancel": an in-flight network sync can't be safely
          // aborted mid-merge, so this only dismisses the dialog -- the sync
          // keeps running (see note above) and the result arrives as a snackbar.
          TextButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('Hide')),
        ],
      );
}
