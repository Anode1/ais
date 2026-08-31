// main.dart -- AIS native client. All logic is the C engine via AisEngine
// (ais_ffi.dart). The header is a translucent strip ABOVE the list, never an
// overlay, so the list stays visible.
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
import 'record_rows.dart';
import 'add_validation.dart';
import 'version.dart';

void main() async {
  // Read the saved theme BEFORE the first frame: loading it afterwards paints one
  // frame of the wrong one, which is exactly the flash the setting exists to stop.
  WidgetsFlutterBinding.ensureInitialized();
  themeModeNotifier.value = await loadThemeMode();
  runApp(const AisApp());
}

// The theme choice, driven from the overflow menu via a ValueListenableBuilder and
// kept across launches in the app's own support dir -- NOT in the index: the index
// folder is what Syncthing carries between devices, and a screen preference set on
// the phone has no business changing the laptop's.
final ValueNotifier<ThemeMode> themeModeNotifier =
    ValueNotifier(ThemeMode.system);

Future<File> _themeFile() async =>
    File('${(await getApplicationSupportDirectory()).path}/theme');

/// The saved choice, or System when nothing is saved yet or the file is unreadable
/// (a fresh install, a cleared app dir): the app opens, it just opens as System.
Future<ThemeMode> loadThemeMode() async {
  try {
    final f = await _themeFile();
    if (!f.existsSync()) return ThemeMode.system;
    switch (f.readAsStringSync().trim()) {
      case 'light':
        return ThemeMode.light;
      case 'dark':
        return ThemeMode.dark;
      default:
        return ThemeMode.system;
    }
  } catch (_) {
    return ThemeMode.system;
  }
}

/// Best-effort: a theme that cannot be written is still applied for this session.
Future<void> saveThemeMode(ThemeMode m) async {
  try {
    final f = await _themeFile();
    f.parent.createSync(recursive: true);
    f.writeAsStringSync(m == ThemeMode.light
        ? 'light'
        : m == ThemeMode.dark
            ? 'dark'
            : 'system');
  } catch (e) {
    debugPrint('AIS: could not save the theme choice: $e');
  }
}

class AisApp extends StatelessWidget {
  const AisApp({super.key});
  @override
  Widget build(BuildContext context) {
    // One seed, two schemes; every surface comes from the ColorScheme.
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
  // Test seam for the tag-suggestion rows: `flutter test` runs with no engine
  // (CI builds no libais.so), so the lookup is injectable. Null = the engine's
  // whole tag cloud (_tagMatches).
  final List<String> Function(String prefix)? tagLookup;
  // Same seam for the search path: recall and find are injectable so the
  // search tests run engine-less too. Null = the real engine.
  final List<Hit> Function(String keys, bool orMode)? recallLookup;
  final List<Hit> Function(String needle)? findLookup;
  const RecallPage(
      {super.key, this.tagLookup, this.recallLookup, this.findLookup});
  @override
  State<RecallPage> createState() => _RecallPageState();
}

class _RecallPageState extends State<RecallPage> with WidgetsBindingObserver {
  final _q = TextEditingController();
  final _qFocus = FocusNode(); // so a suggestion tap can hand focus back
  final _speech = SpeechToText();
  // Live search: each keystroke reschedules this, _recall() fires on the pause.
  Timer? _debounce;
  AisEngine? _ais;
  List<Hit> _results = const [];
  // id -> how many FURTHER links that record holds beyond the one shown.
  Map<int, int> _resultExtra = const {};
  // Tags for the current recall/find results, keyed by record id. The recall Hit
  // carries only id+value, so tags are fetched ONCE when results load (here, not
  // in the itemBuilder, which reruns on scroll). Cleared whenever results reset.
  Map<int, String> _resultKeys = const {};
  // Ids in _results that matched the query only in their VALUE (via find()),
  // not by tag. They sit at the tail of _results, rendered under one
  // "matched in the value" separator. Empty = no value section.
  Set<int> _valueOnly = const {};
  List<TlRow> _tl = const [];
  List<TagRow> _tags = const [];
  int _tlBefore = 0; // keyset cursor: last id of the loaded timeline page
  bool _tlMore = false; // last page was full, so more may exist
  String _tlFrom = ''; // timeline date range, "YYYY-MM-DD" ('' = open)
  String _tlTo = '';
  static const int _tlPage = 100;
  // Recall is keyset-paged: the cursor is the largest id shown (recall emits
  // ascending). find() is unpaged.
  static const int _recallPage = 100;
  int _recallBefore = 0;
  bool _recallMore = false;
  // Tags likewise: the cursor is the (count, key) of the last row, in the
  // busiest-first order the engine emits.
  static const int _tagsPage = 100;
  int _tagsAfterCount = 0;
  String _tagsAfterKey = '';
  bool _tagsMore = false;
  // Multi-tag search mode: false = AND (under EVERY tag), true = OR (under ANY).
  bool _matchAny = false;
  String _view = 'timeline'; // recall | timeline | tags -- open on content, not a blank search
  // Ids optimistically removed from the lists but not yet committed to the
  // engine: they sit in their Undo window. A commit clears the id and calls del().
  final Set<int> _pendingDelete = {};
  // One per pending delete: the Undo window measured in TIME. The snackbar's
  // closed future was the only trigger before, and it does not fire reliably.
  final Map<int, Timer> _delTimers = {};
  // The live "Deleted / UNDO" snackbar per pending id: once del() has run, UNDO
  // would fake-restore a row the engine no longer holds, so a commit dismisses it.
  final Map<int, ScaffoldFeatureController<SnackBar, SnackBarClosedReason>>
      _delSnack = {};
  bool _voice = false;
  bool _searched = false;
  // Full-text fallback is SECONDARY: shown only after a key search finds nothing
  // and the user taps "Search note text instead". Reset on any new key search,
  // clear, or view change.
  bool _textSearch = false;
  String _status = 'opening index…';
  String _query = '';
  String _dir = '';
  int _ms = 0;
  // One sync at a time: a scanned deep link arrives off the platform channel, not
  // through the barrier dialog, so it could otherwise start a second sync on the
  // shared engine handle mid-sync (data race). This gates every sync entry point.
  bool _syncBusy = false;
  // Folder auto-sync (a Syncthing / cloud folder). Path remembered per-index in
  // <dir>/syncfolder. A pass runs on open, after a save/delete, and on "Sync now":
  // no background polling. Empty = off.
  String _syncFolder = '';
  String _syncFolderSaid = '';   // the last folder-sync problem reported, to not repeat it

  // When this device last synced by ANY route, so the Sync sheet can say so.
  // Kept BESIDE the index, not inside it, so it stays this device's own answer:
  // a peer's copy of the file would report a sync this device never made.
  DateTime? _lastSync;

  // Custom-scheme deep links (ais://sync?...). The native side (MainActivity /
  // AppDelegate) pushes live links as 'onLink' and holds a cold-start link for
  // 'getInitialLink'. Absent on desktop, where the calls just throw and are ignored.
  static const _linkChannel = MethodChannel('ais/deeplink');

  // Android share-sheet intake (ACTION_SEND, text/plain). MainActivity holds a
  // share that cold-started the app for 'getInitialShared' and pushes one that
  // arrives while running as 'onShared'; either way the text prefills the Add
  // sheet. Absent on desktop and iOS, where the calls just throw and are ignored.
  static const _shareChannel = MethodChannel('ais/share');

  // iOS-only: ask the runner to set NSURLIsExcludedFromBackupKey on the index dir
  // (see ios/Runner/AppDelegate.swift). Absent elsewhere, where the call just throws.
  static const _backupChannel = MethodChannel('ais/backup');

  // Hosting shows a QR and waits up to ~2 minutes for the other device to scan
  // it -- far longer than a phone's screen timeout, so the code would go dark
  // mid-scan. Held only while the host dialog is up.
  static const _screenChannel = MethodChannel('ais/screen');

  Future<void> _keepAwake(bool on) async {
    try {
      await _screenChannel.invokeMethod<bool>('keepAwake', on);
    } catch (_) {
      // desktop, or an older bundle with no handler: the screen behaves normally
    }
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);   // to commit a pending delete on the way out
    _init();
  }

  // A delete armed inside its Undo window has not reached the engine yet. If the
  // app goes away first, the row comes back on the next launch and reads as "I
  // deleted it and it came back". Leaving the foreground is the user finishing
  // with the screen, so it settles the delete: Undo's window is the seconds they
  // are LOOKING at the snackbar, not the rest of the process's life.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden ||
        state == AppLifecycleState.detached) {
      _flushPendingDeletes();
    }
  }

  // Desktop shares the index the CLI resolves (nearest .ais/, ~/.ais/config, else
  // ~/.ais) through the engine, so no env vars. Mobile uses the app's private dir.
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

  // iOS backs Documents/ up to iCloud BY DEFAULT, so the index is uploaded unless
  // opted out with NSURLIsExcludedFromBackupKey -- a native-only flag, hence the
  // channel. Must run AFTER the directory exists and on every launch: it does not
  // survive the dir being recreated. Failure is logged, not swallowed.
  Future<void> _excludeFromICloud(String dir) async {
    if (!Platform.isIOS) return;
    try {
      final ok = await _backupChannel.invokeMethod<bool>('excludeFromBackup', dir);
      if (ok != true) debugPrint('AIS: could not exclude $dir from iCloud backup');
    } catch (e) {
      debugPrint('AIS: iCloud backup exclusion unavailable: $e');
    }
  }

  Future<void> _init() async {
    try {
      final dir = await _indexDir();
      Directory(dir).createSync(recursive: true);
      await _excludeFromICloud(dir);
      _ais = AisEngine(dir);
      _dir = dir;
      _status = 'Type tags, then Search. Tap Add to save.';
      _syncFolder = _loadSyncFolder();
      _loadLastSync();
      _loadTimeline(); // open showing recent items, not a blank search pane
      _runFolderSync(silent: true); // pull peer changes on open (opening is the user action)
    } catch (e) {
      // The likeliest cause is a shared index folder (Syncthing) a newer AIS has
      // upgraded: the engine refuses it rather than resolve deletes the old way.
      _status = 'This library was written by a newer version of AIS. '
          'Your data is safe and unchanged: update this app to open it.\n($e)';
    }
    // Speech initializes on the first mic tap (see _listen), tying the permission
    // prompt to a user gesture rather than app launch.
    if (mounted) setState(() {});
    _wireDeepLinks();
    _wireShareIntake();
  }


  // Register the live-link handler, then check for a link that cold-started us.
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

  // Register the live-share handler, then check for a share that cold-started us.
  void _wireShareIntake() {
    _shareChannel.setMethodCallHandler((call) async {
      if (call.method == 'onShared' && call.arguments is String) {
        _handleShared(call.arguments as String);
      }
    });
    _shareChannel.invokeMethod<String>('getInitialShared').then((text) {
      if (text != null && text.isNotEmpty) _handleShared(text);
    }).catchError((_) {}); // no such channel on desktop; ignore
  }

  // Text shared from another app prefills the Add sheet's value. Tags stay the
  // user's: a share's subject line is dropped on the native side, never turned
  // into tags here.
  void _handleShared(String text) {
    if (!mounted || text.isEmpty) return;
    // A share can arrive over an open sheet or dialog; clear the way first,
    // like a scanned link does.
    Navigator.of(context).popUntil((r) => r.isFirst);
    _showAdd(value: text);
  }

  // A scanned ais://sync?host=IP:PORT&token=HEX link, opened by the phone's own
  // camera and routed here by the OS. Confirm before joining: a link can come from
  // anywhere and a sync shares this device's records.
  Future<void> _handleLink(String link) async {
    if (_ais == null || !mounted) return;
    // Say so when a link is unusable. The app is already in the foreground by the
    // time this runs (the OS routed the link here), so returning in silence looks
    // exactly like the app opening and doing nothing. A camera app that mangles
    // the query, or a half-copied link, lands here.
    void bad() {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
            content: Text('That pairing link is incomplete. Ask the other device '
                'to show it again, or type the address and code in Join.')));
      }
    }

    final Uri uri;
    try {
      uri = Uri.parse(link);
    } catch (_) {
      bad();
      return;
    }
    if (uri.scheme != 'ais' || uri.host != 'sync') return;   // not ours: not our business
    final host = uri.queryParameters['host'] ?? '';
    final token = uri.queryParameters['token'] ?? '';
    if (host.isEmpty || token.isEmpty) {
      bad();
      return;
    }
    if (_syncBusy) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('A sync is already running. Finish it first.')));
      return;
    }
    // The scan can arrive while a sheet/dialog is already open, so dismiss it
    // before opening Join with the scanned host + token FILLED IN to confirm.
    Navigator.of(context).popUntil((r) => r.isFirst);
    await _syncJoin(prefillUrl: 'http://$host', prefillToken: token);
  }

  // Engine calls behind the test seams: with no libais.so the widget tests
  // inject recall/find, so the whole search path runs engine-less (keys then
  // just come back empty).
  List<Hit> _recallHits(String keys) =>
      widget.recallLookup?.call(keys, _matchAny) ??
      _ais!.recallPage(keys, orMode: _matchAny, after: 0, count: _recallPage);
  List<Hit> _findHits(String needle) =>
      widget.findLookup?.call(needle) ?? _ais?.find(needle) ?? const [];
  String _keysFor(int id) => (_ais?.keysOf(id) ?? '').trim();

  void _recall() {
    final keys = _normKeys(_q.text);
    if ((_ais == null && widget.recallLookup == null) || keys.isEmpty) return;
    final t0 = DateTime.now();
    // Page one only. The cursor/more come from the RAW page, so the Undo-window
    // filter below cannot make the page look short and stop pagination early.
    final page = _recallHits(keys);
    // Exclude ids still inside their Undo window: a re-query must not resurrect
    // a row the user just swiped away.
    final g = oneRowPerRecord(
        page.where((h) => !_pendingDelete.contains(h.id)).toList());
    final r = g.rows;
    // The VALUE half: the engine's substring find() over note values, appended
    // after the tag matches under one separator. A record the tag half already
    // lists is not repeated. The OR chip means nothing here: find takes no keys.
    final tagIds = {for (final h in page) h.id};
    final gv = oneRowPerRecord(_findHits(_q.text.trim())
        .where((h) => !_pendingDelete.contains(h.id) && !tagIds.contains(h.id))
        .toList());
    final v = gv.rows;
    final keysMap = {for (final h in [...r, ...v]) h.id: _keysFor(h.id)};
    setState(() {
      _query = keys;
      _searched = true;
      _textSearch = false; // a fresh key search leaves the text-fallback mode
      _ms = DateTime.now().difference(t0).inMilliseconds;
      _resultExtra = {...g.extra, ...gv.extra};
      _results = [...r, ...v];
      _valueOnly = {for (final h in v) h.id};
      _resultKeys = keysMap;
      _recallBefore = page.isNotEmpty ? page.last.id : 0;
      _recallMore = page.length == _recallPage;
    });
  }

  // Flip AND/OR match mode and re-run the current key search from page one.
  void _setMatchAny(bool v) {
    setState(() => _matchAny = v);
    if (_view == 'recall' && !_textSearch && _q.text.trim().isNotEmpty) _recall();
  }

  // Page on with the recall cursor. No-op for the find() text fallback (unpaged)
  // and when the last page was short.
  void _loadMoreRecall() {
    if (_ais == null || !_recallMore || _textSearch || _query.isEmpty) return;
    final page = _ais!.recallPage(_query, orMode: _matchAny, after: _recallBefore, count: _recallPage);
    final gm = oneRowPerRecord(
        page.where((h) => !_pendingDelete.contains(h.id)).toList());
    final fresh = gm.rows;
    final extra = gm.extra;
    final keysMap = {for (final h in fresh) h.id: _ais!.keysOf(h.id).trim()};
    setState(() {
      // New tag rows go BEFORE the value section, and a value row this page
      // proves is really a tag match is promoted (its value copy dropped).
      final freshIds = {for (final h in fresh) h.id};
      final tagRows = _results.where((h) => !_valueOnly.contains(h.id));
      final valRows = _results
          .where((h) => _valueOnly.contains(h.id) && !freshIds.contains(h.id));
      _valueOnly = _valueOnly.difference(freshIds);
      _results = [...tagRows, ...fresh, ...valRows];
      _resultKeys = {..._resultKeys, ...keysMap};
      _resultExtra = {..._resultExtra, ...extra};
      if (page.isNotEmpty) _recallBefore = page.last.id;
      _recallMore = page.length == _recallPage;
    });
  }

  // Live/submitted search from the persistent header field. That field shows on
  // every tab, so typing must bring the recall view forward: otherwise _recall()
  // fills _results while the body still shows the timeline.
  void _recallLive() {
    if (_view == 'recall') {
      _recall();
    } else {
      _setView('recall'); // flushes pending deletes, then _recall() (query non-empty)
    }
  }

  // The SECONDARY full-text fallback, over note VALUES via find(). Reached ONLY by
  // an explicit tap on "Search note text instead" -- never on typing, debounce,
  // submit, or view change.
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
      _valueOnly = const {}; // one flat list here, no separator
      _resultKeys = keysMap;
      _recallMore = false; // find() returns the whole set: nothing to page
    });
  }

  void _setView(String v) {
    _flushPendingDeletes(); // commit pending deletes so a re-query can't resurrect them
    setState(() {
      _view = v;
      _textSearch = false; // leaving/returning to a view drops the text fallback
      _resultKeys = const {}; // stale on any view change; _recall repopulates
      _notHereCache.clear(); // re-check file presence after a reload (a sync may have landed a blob)
    });
    if (v == 'recall') {
      if (_q.text.trim().isNotEmpty) {
        _recall();
      } else {
        // empty query: blank the pane and show the hint, like the other GUIs
        setState(() {
          _results = const [];
          _valueOnly = const {};
          _searched = false;
          _recallMore = false;
          _status = 'Type tags, then Search.';
        });
      }
    } else if (v == 'timeline') {
      _loadTimeline();
    } else {
      _loadTags();
    }
  }

  // Fresh tag-cloud load: page one from the busiest, resetting the cursor.
  void _loadTags() {
    final page = _ais?.tagsPage(afterCount: 0, afterKey: '', count: _tagsPage) ?? const [];
    setState(() {
      _tags = page;
      if (page.isNotEmpty) {
        _tagsAfterCount = page.last.count;
        _tagsAfterKey = page.last.key;
      }
      _tagsMore = page.length == _tagsPage;
    });
  }

  // Page on with the tag cursor: the next slice after the last (count, key).
  void _loadMoreTags() {
    if (_ais == null || !_tagsMore) return;
    final page = _ais!.tagsPage(
        afterCount: _tagsAfterCount, afterKey: _tagsAfterKey, count: _tagsPage);
    setState(() {
      _tags = [..._tags, ...page];
      if (page.isNotEmpty) {
        _tagsAfterCount = page.last.count;
        _tagsAfterKey = page.last.key;
      }
      _tagsMore = page.length == _tagsPage;
    });
  }

  // Existing tags starting with [prefix] (case-insensitive), busiest first, at
  // most six: what the TagSuggestRow under every tag field shows. Pages through
  // the WHOLE tag cloud (the FFI is cheap), so "rec" finds recipe even when the
  // Tags view has only its first page loaded. Widget tests inject
  // widget.tagLookup instead: they run with no engine to page.
  List<String> _tagMatches(String prefix) {
    final injected = widget.tagLookup;
    if (injected != null) return injected(prefix);
    final e = _ais;
    if (e == null || prefix.isEmpty) return const [];
    final p = prefix.toLowerCase();
    final out = <String>[];
    var afterCount = 0;
    var afterKey = '';
    while (out.length < 6) {
      final page = e.tagsPage(afterCount: afterCount, afterKey: afterKey, count: _tagsPage);
      for (final t in page) {
        if (t.key.toLowerCase().startsWith(p)) out.add(t.key);
        if (out.length >= 6) break;
      }
      if (page.length < _tagsPage) break;
      afterCount = page.last.count;
      afterKey = page.last.key;
    }
    return out;
  }

  // Fresh timeline load: page one from the newest, within [_tlFrom, _tlTo].
  void _loadTimeline() {
    final page = _ais?.timeline(before: 0, count: _tlPage, from: _tlFrom, to: _tlTo) ?? const [];
    setState(() {
      // Hide ids inside their Undo window; cursor/more come from the raw page.
      _tl = page.where((r) => !_pendingDelete.contains(r.id)).toList();
      _tlBefore = page.isNotEmpty ? page.last.id : 0;
      _tlMore = page.length == _tlPage;
    });
  }

  Future<void> _pickRange() async {
    final r = await showDateRangePicker(
      context: context,
      firstDate: DateTime(2000),
      lastDate: DateTime(2100),
      initialDateRange: (_tlFrom.isNotEmpty && _tlTo.isNotEmpty)
          ? DateTimeRange(
              start: DateTime.tryParse(_tlFrom) ?? DateTime.now(),
              end: DateTime.tryParse(_tlTo) ?? DateTime.now())
          : null,
    );
    if (r == null) return;
    String d(DateTime t) => '${t.year.toString().padLeft(4, '0')}-'
        '${t.month.toString().padLeft(2, '0')}-${t.day.toString().padLeft(2, '0')}';
    _tlFrom = d(r.start);
    _tlTo = d(r.end);
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
        // onError carries the case that matters most: a device with no OFFLINE
        // language model refuses an on-device session rather than falling back to
        // the cloud, and with no listener that arrives as silence.
        _voice = await _speech.initialize(onError: (e) {
          if (!mounted) return;
          final noModel = e.errorMsg.contains('language') ||
              e.errorMsg.contains('no_match') ||
              e.errorMsg.contains('not_available');
          setState(() => _status = noModel
              ? 'Dictation needs an offline language pack for your language. '
                  'Install it in your phone settings (AIS never sends audio away), '
                  'or type instead.'
              : 'Could not hear that. Try again, or type instead.');
        });
      } catch (_) {
        _voice = false;
      }
      if (mounted) setState(() {});
      if (!_voice) {
        if (mounted) setState(() => _status = 'Microphone unavailable or permission denied');
        return;
      }
    }
    // ON-DEVICE ONLY. The package default is onDevice: false, which streams the
    // audio to the platform's cloud recogniser -- Google's, on Android. A device
    // with no local recogniser gets no dictation and is told why.
    await _speech.listen(
      onResult: (r) {
        _q.text = r.recognizedWords;
        if (r.finalResult) _recall();
      },
      listenOptions: SpeechListenOptions(onDevice: true),
    );
  }

  bool _isUrl(String v) => v.startsWith('http://') || v.startsWith('https://');

  // Fold commas to spaces and collapse runs, so "home, wifi" and "home   wifi"
  // both give the tags home + wifi. The engine itself tokenizes on spaces.
  String _normKeys(String s) =>
      s.replaceAll(',', ' ').trim().replaceAll(RegExp(r'\s+'), ' ');

  // A blob-backed value holds only an internal "blobs/<ts>.txt" path; the
  // content lives in that file. "Edit value" on such a row edits the CONTENT
  // (docRead/setValueText), never the path.
  bool _isBlob(String v) => v.startsWith('blobs/');

  // A multi-line paste is stored out-of-line as a blob; the record holds the path.
  // Resolution lives in the C engine (ais_embed_display), so no blob file is read
  // in Dart. Cache by absolute path (blobs are immutable) to skip the FFI on
  // rebuilds; an absent blob resolves to its path (uncached), so _notHere badges it.
  final Map<String, String> _blobCache = {};
  // Open a URL value in the external browser; a failed launch surfaces a hint.
  Future<void> _openUrl(String v) async {
    try {
      final uri = Uri.parse(v);
      if (!await canLaunchUrl(uri) ||
          !await launchUrl(uri, mode: LaunchMode.externalApplication)) {
        throw 'launch failed';
      }
    } catch (_) {
      if (!mounted) return;
      // A dead end is the wrong answer: the link is the user's data and they can
      // always paste it somewhere themselves. Offer that in the failure, where
      // they are, rather than making them find Copy in a menu.
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: const Text("Couldn't open that link here"),
        action: SnackBarAction(
          label: 'COPY',
          onPressed: () {
            Clipboard.setData(ClipboardData(text: v));
            ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Link copied')));
          },
        ),
      ));
    }
  }

  // Render a record's value: a URL as a tappable link (long-press/drag still
  // selects), everything else verbatim. Callers handle encrypted/blob cases first.
  // [maxLines] bounds a list row so a huge paste cannot blow up its height.
  Widget _valueLabel(String v, ColorScheme cs, {int? maxLines}) {
    if (!_isUrl(v)) return SelectableText(_display(v), maxLines: maxLines);
    final shown = _display(v);
    // A link needs more than colour (colour-blind users) and must be reachable
    // by a screen reader / keyboard, which a bare TextSpan is not.
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

  // List rows render with plain Text, not _valueLabel: SelectableText with
  // maxLines RESERVES that many lines (a one-line row stood ~110dp tall) and
  // consumes the tap that should open the detail page. The detail page keeps
  // the selectable, tappable _valueLabel; here a URL is tinted only.
  Widget _rowLabel(String v, ColorScheme cs) => Text(
        _display(v),
        maxLines: 2,
        overflow: TextOverflow.ellipsis,
        style: _isUrl(v) ? TextStyle(color: cs.primary) : null,
      );

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

  // Hand the display text to the OS share sheet. Desktop/Linux share_plus support
  // is limited, so a failure is a no-op with a hint rather than a crash.
  Future<void> _share(String v) async {
    try {
      await SharePlus.instance.share(ShareParams(text: _display(v)));
    } catch (_) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("Sharing isn't available here")));
    }
  }

  // "Not here": the value points at a file absent on THIS device -- an AIS blob
  // not yet synced down, or a local-file reference added elsewhere. http(s) URLs
  // and inline text always resolve. Display-only.
  //
  // Cached per value: the row builders call this for every visible row on every
  // rebuild, and File.existsSync() there stats the disk per frame while scrolling
  // (jank). Presence changes only when a blob syncs down or the library switches,
  // so the cache is cleared in _setView and in _changeStore.
  final Map<String, bool> _notHereCache = {};
  bool _notHere(String v) => _notHereCache[v] ??= _notHereCompute(v);
  bool _notHereCompute(String v) {
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

  Widget _notHereBadge(ColorScheme cs) => Padding(
        padding: const EdgeInsets.only(left: 6),
        child: Tooltip(
          message: 'Not on this device. Open it on the desktop, or mount that disk.',
          child: Icon(Icons.cloud_off, size: 16, color: cs.outline),
        ),
      );

  // Sync & backup. Three clearly-separated ways in, so they can't be confused:
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
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text('Sync & backup',
                      style: Theme.of(ctx).textTheme.titleMedium),
                  const SizedBox(height: 2),
                  // No cloud copy, no trash: a second device IS the backup.
                  Text(
                      'Your notes live only on this device. Copying them to '
                      'another device, or to a file, is what backs them up.',
                      style: Theme.of(ctx).textTheme.bodySmall),
                  const SizedBox(height: 6),
                  Row(
                    children: [
                      Icon(
                          _lastSync == null
                              ? Icons.warning_amber_rounded
                              : Icons.check_circle_outline,
                          size: 16,
                          color: _lastSync == null
                              ? Theme.of(ctx).colorScheme.error
                              : Theme.of(ctx).colorScheme.primary),
                      const SizedBox(width: 6),
                      Expanded(
                        child: Text(_lastSyncLabel,
                            style: Theme.of(ctx)
                                .textTheme
                                .bodySmall
                                ?.copyWith(
                                    color: _lastSync == null
                                        ? Theme.of(ctx).colorScheme.error
                                        : null)),
                      ),
                    ],
                  ),
                ],
              ),
            ),
            _syncGroupLabel(ctx, 'A nearby device (same Wi-Fi)'),
            // The step people get wrong: this needs BOTH devices, at the same
            // time, one hosting and one joining. Said here, before either button
            // is tapped, rather than discovered by tapping the wrong one.
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 8),
              child: Text(
                  'Open AIS on both devices and keep both awake. Tap Host on one, '
                  'Join on the other, then scan or type the code it shows. Nothing '
                  'leaves your Wi-Fi.',
                  style: Theme.of(ctx).textTheme.bodySmall),
            ),
            ListTile(
              leading: const Icon(Icons.wifi_tethering),
              title: const Text('Host a sync'),
              subtitle: const Text('This device shows a code and waits. Start here.'),
              onTap: () => Navigator.pop(ctx, 'host'),
            ),
            ListTile(
              leading: const Icon(Icons.qr_code_scanner),
              title: const Text('Join / scan a nearby device'),
              subtitle: const Text('For the OTHER device: scan the code, or type it in'),
              onTap: () => Navigator.pop(ctx, 'join'),
            ),
            const Divider(height: 24),
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

  // Beside the index (<dir>_lastsync), never inside it: see _lastSync.
  void _loadLastSync() {
    try {
      final f = File('${_dir}_lastsync');
      if (f.existsSync()) {
        _lastSync = DateTime.tryParse(f.readAsStringSync().trim());
      } else {
        _lastSync = null;
      }
    } catch (_) {
      _lastSync = null;
    }
  }

  // Every converging route calls this: Host, Join, a folder pass, a file import.
  void _markSynced() {
    _lastSync = DateTime.now();
    try {
      File('${_dir}_lastsync').writeAsStringSync(_lastSync!.toIso8601String());
    } catch (_) {}
  }

  void _saveLastSync() => _markSynced();

  // "just now" / "3 hours ago" / "12 Mar 2026": vague near, exact far.
  String get _lastSyncLabel {
    final t = _lastSync;
    if (t == null) return 'Never synced on this device';
    final d = DateTime.now().difference(t);
    if (d.inMinutes < 2) return 'Last synced just now';
    if (d.inMinutes < 60) return 'Last synced ${d.inMinutes} minutes ago';
    if (d.inHours < 24) {
      return 'Last synced ${d.inHours} hour${d.inHours == 1 ? '' : 's'} ago';
    }
    if (d.inDays < 30) {
      return 'Last synced ${d.inDays} day${d.inDays == 1 ? '' : 's'} ago';
    }
    return 'Last synced ${t.day} ${_months[t.month - 1]} ${t.year}';
  }

  // Pick a shared folder and run the first pass. The folder is REMEMBERED ONLY IF
  // THAT PASS WORKS: on Android a SAF pick usually lands on shared storage this app
  // cannot opendir(), and a remembered unreadable folder is re-reported at every
  // launch.
  Future<void> _pickSyncFolder() async {
    if (_ais == null || _syncBlocks()) return;
    final messenger = ScaffoldMessenger.of(context);
    String? dir;
    try {
      dir = await getDirectoryPath();
    } catch (_) {
      dir = null;
    }
    if (!mounted) return;
    if (dir == null || dir.isEmpty) return;   // cancelled, or no picker on this platform
    _tryFolder(dir, messenger);
  }

  // Run one pass against DIR and keep it only on success, so neither the picker
  // nor the "Sync anyway" override can leave a folder set that does not work.
  // FORCE accepts a folder that is merely empty (a replaced stick, a cleared share).
  void _tryFolder(String dir, ScaffoldMessengerState messenger,
      {bool force = false}) {
    if (_ais == null) return;
    final code = _ais!.syncFolderCode(dir, force: force);
    final problem = AisEngine.syncFolderProblem(code);
    if (!mounted) return;
    if (problem == null) {
      setState(() => _syncFolder = dir);
      _saveSyncFolder(dir);
      _syncFolderSaid = '';
      _lastSync = DateTime.now();
      _saveLastSync();
      messenger.showSnackBar(SnackBar(content: Text('Syncing with $dir')));
      _setView(_view);
      return;
    }
    // Forcing hit the same refusal: an older bundled engine has no forcing entry
    // point, so the button cannot do what it offers.
    messenger.showSnackBar(SnackBar(
        content: Text(force && code == -5
            ? 'This app version cannot override that. Update the app.'
            : problem),
        action: (!force && code == -5)
            ? SnackBarAction(
                label: 'Use it anyway',
                onPressed: () => _tryFolder(dir, messenger, force: true))
            : null));
  }

  // A LAN Host/Join sync runs on a BACKGROUND isolate holding the SAME engine
  // handle; flock on the shared fd does not exclude it, so a concurrent UI-isolate
  // write would race the store. Blocks a mutating action while any sync is in
  // flight (reads stay available) and warns; reads true when blocked.
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
    final before = _ais!.countLive();
    final code = _ais!.syncFolderCode(_syncFolder);
    final problem = AisEngine.syncFolderProblem(code);
    if (problem == null) {
      // Clear on ANY success, so the same problem is reported again if it returns.
      _syncFolderSaid = '';
      _markSynced();
      if (mounted) _setView(_view);
    }
    if (!mounted) return;
    // A FAILURE is always reported, even from a background pass; only success is quiet.
    if (problem != null) {
      // Once per problem: this pass runs at open and after every save.
      if (problem != _syncFolderSaid) {
        _syncFolderSaid = problem;
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
            content: Text(problem),
            action: code == -5
                ? SnackBarAction(
                    label: 'Sync anyway',
                    onPressed: () => _forceFolderSync(_syncFolder))
                : null));
      }
    } else if (!silent) {
      ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Folder synced.${_mergeDetail(before)}')));
    }
  }

  // Use the folder as it now stands (a replaced stick, an emptied share); this
  // still creates nothing. Same cross-isolate guard as _runFolderSync: reachable
  // from a SnackBar action that outlives the message, so a LAN sync can have
  // started holding the engine handle in between.
  void _forceFolderSync(String dir) {
    if (_ais == null || _syncBlocks()) return;
    _tryFolder(dir, ScaffoldMessenger.of(context), force: true);
  }

  Widget _syncGroupLabel(BuildContext ctx, String text) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
        child: Align(
          alignment: Alignment.centerLeft,
          child: Text(text,
              style: Theme.of(ctx).textTheme.labelMedium?.copyWith(
                  color: Theme.of(ctx).colorScheme.primary)),
        ),
      );

  // Export the whole index to a single plaintext "aisb" bundle. Desktop gets a
  // native Save dialog; mobile has no browsable filesystem, so it writes a temp
  // copy and hands it to the OS share sheet. The write is fast: it runs inline.
  Future<void> _exportFile() async {
    if (_ais == null) return;
    final messenger = ScaffoldMessenger.of(context);

    if (Platform.isAndroid || Platform.isIOS) {
      // Dated, so repeated exports form a series instead of replacing each other.
      final now = DateTime.now();
      final stamp = '${now.year.toString().padLeft(4, '0')}'
          '${now.month.toString().padLeft(2, '0')}'
          '${now.day.toString().padLeft(2, '0')}';
      final tmp = await getTemporaryDirectory();
      final path = '${tmp.path}/ais-backup-$stamp.aisb';
      final rc = _ais!.exportBundle(path);
      if (!mounted) return;
      if (rc != 0) {
        messenger.showSnackBar(
            const SnackBar(content: Text('Could not write the export file.')));
        return;
      }
      try {
        final result = await SharePlus.instance.share(
            ShareParams(files: [XFile(path)], text: 'AIS backup'));
        if (!mounted) return;
        // This file sits in the CACHE directory -- the only place an app can
        // write before the user picks a destination -- and the OS may delete it
        // whenever it likes, so a dismissed share has left them nothing.
        if (result.status == ShareResultStatus.success) {
          _markSynced();
          messenger.showSnackBar(const SnackBar(
              content: Text('Backup saved. Keep it somewhere off this device.')));
        } else {
          messenger.showSnackBar(const SnackBar(
              content: Text('Not saved anywhere yet: pick a destination to keep the backup.')));
        }
      } catch (_) {
        if (mounted) {
          messenger.showSnackBar(
              const SnackBar(content: Text("Sharing isn't available here")));
        }
      }
      return;
    }

    final downloads = await getDownloadsDirectory();
    final location = await getSaveLocation(
      acceptedTypeGroups: const [
        XTypeGroup(label: 'AIS bundle', extensions: ['aisb'])
      ],
      suggestedName: 'ais-backup.aisb',
      initialDirectory: downloads?.path,
    );
    if (location == null || _ais == null) return; // cancelled
    final rc = _ais!.exportBundle(location.path);
    if (!mounted) return;
    // A file the user chose the location of IS saved, so it counts as a backup.
    if (rc == 0) _markSynced();
    messenger.showSnackBar(SnackBar(
        content: Text(rc == 0
            ? 'Backup saved to ${location.path}'
            : 'Could not write the file. Check the folder path.')));
  }

  // Import a plaintext bundle file and merge it into this index (same
  // tombstone-union LWW merge as live sync). File I/O only, no network.
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
    final before = _ais!.countLive();
    final rc = _ais!.importBundle(file.path);
    if (!mounted) return;
    final String msg;
    switch (rc) {
      case 0:
        msg = 'Merged. This index now includes the file’s records.'
            '${_mergeDetail(before)}';
        _markSynced();
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
  // Prefill (URL, token) arrives from a scanned ais:// QR (see _handleLink).
  Future<void> _syncJoin({String? prefillUrl, String? prefillToken}) async {
    final scanned = prefillUrl != null && prefillToken != null;
    final urlCtrl = TextEditingController(text: prefillUrl ?? 'http://');
    final tokCtrl = TextEditingController(text: prefillToken ?? '');
    final go = await showDialog<bool>(
      context: context,
      builder: (ctx) => _OwnedFields(
        owned: [urlCtrl, tokCtrl],
        child: AlertDialog(
        title: const Text('Join a sync'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: urlCtrl,
              autofocus: !scanned, // don't pop the keyboard when it's already filled
              decoration: const InputDecoration(
                  labelText: 'Address',
                  hintText: 'http://192.168.1.5:8766',
                  helperText: 'An address or a name, e.g. mylaptop.local'),
            ),
            TextField(
              controller: tokCtrl,
              onSubmitted: (_) => Navigator.pop(ctx, true), // Enter = Sync
              decoration: const InputDecoration(labelText: 'Token'),
            ),
            const SizedBox(height: 8),
            Text(
                scanned
                    ? 'Scanned from the other device. Tap Sync to connect.'
                    : "These come from the OTHER device: open AIS there, tap Sync, "
                        "choose Host, and it shows both. Scanning its code with your "
                        "camera fills them in for you.",
                style: Theme.of(ctx).textTheme.bodySmall),
          ],
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Sync')),
        ],
        ),
      ),
    );
    // Read the fields now; _OwnedFields frees the controllers with the route.
    final url = urlCtrl.text.trim();
    final token = tokCtrl.text.trim();
    if (go != true || _ais == null) return;
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
    final before = _ais!.countLive();   // to say what the merge actually did
    _syncBusy = true;
    final fut = _ais!.pullAsync(url, token, bidir: true);
    await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (_) => _SyncWaitDialog(
          title: 'Join a sync',
          waiting: 'Syncing...',
          note: 'You can hide this; the result is still reported.',
          done: fut),
    );
    final rc = await fut;
    _syncBusy = false;
    if (!mounted) return;
    // Every outcome is announced, hidden dialog or not: a join gives up after 10s
    // (embed_pull's LAN timeout), inside the same interaction, and silence reads as
    // success. Hidden and failed looked exactly like synced, and the records that
    // never arrived were not missed until later. Host still keeps its silence: that
    // wait is five minutes, and a snackbar that late is a surprise.
    final String msg;
    switch (rc) {
      case 0:
        msg = 'Synced. Both devices now have the same records.'
            '${_mergeDetail(before)}';
        _markSynced();
        break;
      case 1:
        // Half done: their records ARE here, so this is not a failure.
        msg = 'Got their records, but yours did not reach them. '
            'Nothing was lost. Sync again to finish.';
        _markSynced();
        break;
      case 2:
        msg = 'Synced, but one more round is needed to match exactly. '
            'Nothing was lost. Sync again.';
        _markSynced();
        break;
      case -1:
        msg = 'That address looks wrong. Use http://host:port.';
        break;
      default:
        msg = 'Could not sync. Same Wi-Fi? Check the host is waiting and the token is right.';
    }
    messenger.showSnackBar(SnackBar(content: Text(msg)));
    if (rc >= 0) _setView(_view); // a half-done sync still merged theirs
  }

  // Host: wait for another device to join; both converge (bidirectional).
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
    // 8766 is the default both ends assume, but it can be taken by a sync still
    // releasing it or by anything else. The port travels in the QR and in the
    // printed address, so stepping to the next free one still pairs.
    final port = await _freeSyncPort();
    if (!mounted) return;
    if (port == 0) {
      messenger.showSnackBar(const SnackBar(
          content: Text('No free port for syncing. Close any other sync and try again.')));
      return;
    }
    final token = _genToken();
    // The same ais:// pairing link the desktop web host encodes, so one QR format
    // feeds the one deep-link handler (see _handleLink).
    final link =
        'ais://sync?host=${Uri.encodeQueryComponent('$ip:$port')}&token=$token';
    final detail = 'http://$ip:$port\ntoken: $token\n\n'
        'desktop:  ais --sync http://$ip:$port --token $token';

    _flushPendingDeletes(); // commit any in-flight delete BEFORE the sync isolate starts
    final before = _ais!.countLive();   // to say what the merge actually did
    _syncBusy = true;
    _keepAwake(true);                   // the QR has to stay visible to be scanned
    final fut = _ais!.serveAsync(port, token, bidir: true); // blocks up to ~300s
    final hidden = await showDialog<bool>(
          context: context,
          barrierDismissible: false,
          builder: (_) => _SyncWaitDialog(
              title: 'Host a sync',
              qrData: link,
              commandLabel: 'Or type the address and token on the other device:',
              command: detail,
              intro: 'On your OTHER device: open AIS, tap Sync, choose Join, and '
                  'scan this code with its camera. Keep this screen open.',
              waiting: 'Waiting for the other device...',
              note: 'You can hide this; hosting keeps waiting in the background.',
              done: fut),
        ) ??
        false;
    final rc = await fut;
    _syncBusy = false;
    _keepAwake(false);                  // release it however the wait ended
    if (!mounted) return;
    // A hidden dialog that then timed out must not surprise with a late failure
    // snackbar ~2 min later; a success, whole or half, is still announced.
    if (hidden && rc < 0) return;
    final String msg;
    switch (rc) {
      case 0:
        msg = 'Synced. Both devices now have the same records.'
            '${_mergeDetail(before)}';
        _markSynced();
        break;
      case 1:
        msg = 'They got your records, but theirs did not come back. '
            'Nothing was lost. Sync again to finish.';
        _markSynced();
        break;
      case 2:
        // A record here outlived a delete that arrived in this round; the news
        // of that could not go out until the next one (see AIS_SYNC_AGAIN).
        msg = 'Synced, but one more round is needed to match exactly. '
            'Nothing was lost. Sync again.';
        _markSynced();
        break;
      case -3:
        msg = 'The sync port was taken just as we started. Try again.';
        break;
      default:
        msg = 'That code expired before anyone joined. Tap Sync again for a fresh one.';
    }
    messenger.showSnackBar(SnackBar(content: Text(msg)));
    if (rc >= 0) _setView(_view); // a half-done sync still merged theirs
  }

  // What a sync actually did, in records. Moving nothing is the correct outcome
  // when both devices are in step, and must be distinguishable from a failure.
  String _mergeDetail(int before) {
    if (_ais == null || before < 0) return '';
    final after = _ais!.countLive();
    if (after < 0) return '';
    if (after > before) {
      final n = after - before;
      return ' $n new record${n == 1 ? '' : 's'} arrived.';
    }
    if (after < before) {
      final n = before - after;
      return ' $n record${n == 1 ? '' : 's'} deleted elsewhere were removed here.';
    }
    return ' Nothing new: both devices were already in step.';
  }

  // A random 128-bit token as 32 hex chars (the peer must supply the same one).
  String _genToken() {
    final r = Random.secure();
    return List.generate(
        16, (_) => r.nextInt(256).toRadixString(16).padLeft(2, '0')).join();
  }

  // The first port at or after the default that we can actually bind, probed by
  // binding and closing at once: the engine's serve call reports a busy port only
  // AFTER the dialog has shown a QR advertising it. 0 if the whole range is taken.
  Future<int> _freeSyncPort() async {
    for (var p = 8766; p < 8776; p++) {
      try {
        final s = await ServerSocket.bind(InternetAddress.anyIPv4, p);
        await s.close();
        return p;
      } catch (_) {
        // taken: try the next
      }
    }
    return 0;
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

  // Clean up: rewrite the index without the records that were deleted, which is
  // how space comes back and how a document deleted before this app could
  // dispose of it finally leaves the phone. The web GUI has always offered this;
  // the phone, which has no CLI to fall back on, had no way to run it at all.
  Future<void> _cleanUp() async {
    if (_ais == null) return;
    final messenger = ScaffoldMessenger.of(context);
    if (_syncBlocks()) {
      messenger.showSnackBar(const SnackBar(
          content: Text('A sync is running. Try again in a moment.')));
      return;
    }
    final go = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Clean up'),
        content: const Text(
            'Removes what you have already deleted, for good, and frees the '
            'space it still takes. Nothing you can see is touched.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Clean up')),
        ],
      ),
    );
    if (go != true || _ais == null) return;
    // Re-check: the dialog was open long enough for a scanned link to start one.
    if (_syncBlocks()) {
      messenger.showSnackBar(const SnackBar(
          content: Text('A sync is running. Try again in a moment.')));
      return;
    }
    _flushPendingDeletes();          // commit any armed swipe first, or it is lost
    final ok = _ais!.compact() == 0;
    if (!mounted) return;
    messenger.showSnackBar(SnackBar(
        content: Text(ok ? 'Cleaned up' : "Couldn't clean up")));
    if (ok) _setView(_view);         // ids and counts moved: redraw the view
  }

  Future<void> _changeStore() async {
    // A background LAN sync still holds the CURRENT engine handle by address;
    // close()ing (freeing) it below would be a use-after-free in that isolate.
    // Refuse while any sync is in flight, like every other mutating action.
    if (_syncBlocks()) return;
    final ctrl = TextEditingController(text: _dir);
    final picked = await showDialog<String>(
      context: context,
      builder: (ctx) => _OwnedFields(
        owned: [ctrl],
        child: AlertDialog(
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
      ),
    );
    if (picked == null || picked.isEmpty || picked == _dir) return;
    final chosen = picked; // non-null from here (a setState closure below can't promote picked)
    // A sync can start while the dialog is open (a scanned deep link arrives off
    // the platform channel, not through the barrier), so re-check before freeing
    // the old handle: the swap would race the sync isolate.
    if (_syncBlocks()) return;
    // Commit any in-flight swipe-delete against the CURRENT library BEFORE swapping:
    // its deferred snackbar-close would otherwise fire del(id) on the NEW library and
    // delete an unrelated same-numbered record there.
    _flushPendingDeletes();
    try {
      // Open the new index FIRST, and only then close the old one: a bad path would
      // otherwise throw after close(), leaving _ais on a closed handle.
      Directory(chosen).createSync(recursive: true);
      final next = AisEngine(chosen);
      _ais?.close();
      _ais = next;
      // Desktop: remember the choice the same way `ais --default` does, so the
      // next launch (and the CLI) opens it too. Mobile's index is fixed.
      if (!Platform.isAndroid && !Platform.isIOS) AisIndex.setDefault(chosen);
      setState(() {
        _dir = chosen;
        _syncFolder = _loadSyncFolder(); // sync-folder is per-index
        // Blob content and file-presence are keyed per library.
        _blobCache.clear();
        _notHereCache.clear();
        _results = const [];
        _valueOnly = const {};
        _resultKeys = const {};
        _tl = const [];
        _tlBefore = 0;
        _tlMore = false;
        _tlFrom = '';
        _tlTo = '';
        _recallBefore = 0;
        _recallMore = false;
        _tags = const [];
        _tagsAfterCount = 0;
        _tagsAfterKey = '';
        _tagsMore = false;
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
    if (picked == null) return;
    themeModeNotifier.value = picked;
    await saveThemeMode(picked);
  }

  // The on-disk format version, from the index's own `version` file (c/store.c
  // writes it). Absent means a legacy index, which the engine treats as 0. Null
  // when no index is open or the file is unreadable.
  int? _indexFormatVersion() {
    if (_dir.isEmpty) return null;
    try {
      final f = File('$_dir/version');
      if (!f.existsSync()) return 0;
      return int.tryParse(f.readAsStringSync().trim());
    } catch (_) {
      return null;
    }
  }

  // App version, the ENGINE version this bundle actually links, and the index
  // format version -- the three numbers a bug report needs, on one copyable line.
  // A Flutter bundle can ship a stale libais; "engine: unknown" means no
  // ais_version() in the loaded library.
  void _showAbout() {
    final messenger = ScaffoldMessenger.of(context);
    final engine = AisIndex.engineVersion() ?? 'unknown';
    final fmt = _indexFormatVersion();
    final line = 'AIS $appVersionLabel · engine: $engine'
        '${fmt == null ? '' : ' · index format: v$fmt'}';
    showAboutDialog(
      context: context,
      applicationName: 'AIS',
      applicationVersion: appVersionLabel,
      children: [
        Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Expanded(child: SelectableText(line)),
          IconButton(
            icon: const Icon(Icons.copy),
            tooltip: 'Copy version details',
            onPressed: () {
              Clipboard.setData(ClipboardData(text: line));
              messenger.showSnackBar(
                  const SnackBar(content: Text('Version details copied')));
            },
          ),
        ]),
        const SizedBox(height: 8),
        SelectableText(_dir.isEmpty ? 'Library: (default)' : 'Library: $_dir'),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    // Ctrl+Shift+S opens Sync & backup from anywhere: a desktop user should not
    // have to hunt an overflow menu for it, and the headless UI test (uitest/run.sh)
    // needs an entry point that does not move when the layout does.
    return CallbackShortcuts(
      bindings: <ShortcutActivator, VoidCallback>{
        const SingleActivator(LogicalKeyboardKey.keyS,
            control: true, shift: true): () {
          if (_ais != null) _syncSheet();
        },
      },
      child: Focus(autofocus: true, child: _buildScaffold(cs)),
    );
  }

  Widget _buildScaffold(ColorScheme cs) {
    return Scaffold(
      body: SafeArea(
        child: Column(children: [
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
                      // A SEARCH-result count: off this view it would linger as a
                      // wrong number over the Recent/Tags lists.
                      if (_view == 'recall' && _searched)
                        Text(
                          '${_results.length} result${_results.length == 1 ? '' : 's'} · $_ms ms',
                          style: Theme.of(context)
                              .textTheme
                              .bodySmall
                              ?.copyWith(color: cs.onSurfaceVariant),
                        ),
                      // Config home for store/sync/theme/about. 48dp target.
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
                            case 'cleanup':
                              await _cleanUp();
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
                            // Named "backup" as well as "sync": the badge is the
                            // only place the app admits it never happened.
                            child: ListTile(
                              contentPadding: EdgeInsets.zero,
                              leading: Icon(_lastSync == null
                                  ? Icons.sync_problem
                                  : Icons.sync),
                              title: const Text('Sync & backup'),
                              subtitle: _lastSync == null
                                  ? const Text('Not backed up yet')
                                  : null,
                            ),
                          ),
                          PopupMenuItem(
                            value: 'cleanup',
                            enabled: _ais != null,
                            child: const ListTile(
                              contentPadding: EdgeInsets.zero,
                              leading: Icon(Icons.cleaning_services_outlined),
                              title: Text('Clean up'),
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
                            focusNode: _qFocus,
                            // The app opens on the timeline, not search; autofocus
                            // here would pop the soft keyboard on every launch on
                            // mobile. Keep it only on desktop (no soft keyboard).
                            autofocus: !Platform.isAndroid && !Platform.isIOS,
                            textInputAction: TextInputAction.search,
                            onSubmitted: (_) => _recallLive(),
                            onChanged: (v) {
                              // Debounced live filter: run on the typing pause.
                              _debounce?.cancel();
                              if (v.trim().isEmpty) {
                                // cleared: fall back to the hint, don't search
                                setState(() {
                                  _results = const [];
                                  _valueOnly = const {};
                                  _resultKeys = const {};
                                  _searched = false;
                                  _textSearch = false; // cleared query drops the fallback too
                                  _query = '';         // and its paging cursor state
                                  _recallMore = false;
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
                      ],
                    ),
                    // The completion row every tag field gets. Completing re-runs
                    // the search at once: a programmatic controller change never
                    // fires onChanged, so the debounce path above would sit idle.
                    TagSuggestRow(
                      controller: _q,
                      focusNode: _qFocus,
                      lookup: _tagMatches,
                      onCompleted: () {
                        _debounce?.cancel();
                        _recallLive();
                      },
                    ),
                    const SizedBox(height: 8),
                    // "Match any tag" (OR) as a chip. The library path moved
                    // off the home screen: it stays in About and in the Change
                    // Library dialog, where it is actionable.
                    Row(children: [
                      FilterChip(
                        label: const Text('Match any tag'),
                        selected: _matchAny,
                        onSelected: _setMatchAny,
                        visualDensity: VisualDensity.compact,
                      ),
                    ]),
                  ],
                ),
              ),
            ),
          ),

          Expanded(child: _body(cs)),
        ]),
      ),
      floatingActionButton: _emptyStateOffersAdd
          ? null
          : FloatingActionButton.extended(
              onPressed: _ais == null ? null : _showAdd,
              icon: const Icon(Icons.add),
              label: const Text('Add'),
            ),
      // The label is "Search"; the internal view key stays 'recall'. Scaffold
      // resizes the body above the soft keyboard but not this slot, so pad it up by
      // the keyboard height (animated); the FAB anchors above the bar and rides up.
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

  void _retryOpen() {
    setState(() => _status = 'opening index…');
    _init();
  }

  // Whether the engine can answer for an empty list yet: a spinner while it is
  // still OPENING, the real error + Retry if the open FAILED, null once it is live.
  // Keeps a slow or failed open from reading as "nothing saved yet".
  Widget? _engineGate(ColorScheme cs) {
    if (_ais != null) return null;
    if (_status.startsWith('cannot open') || _status.startsWith('cannot resolve')) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.error_outline, size: 48, color: cs.error),
              const SizedBox(height: 16),
              Text(_status,
                  textAlign: TextAlign.center,
                  style: TextStyle(color: cs.onSurfaceVariant)),
              const SizedBox(height: 20),
              FilledButton.icon(
                onPressed: _retryOpen,
                icon: const Icon(Icons.refresh),
                label: const Text('Retry'),
              ),
            ],
          ),
        ),
      );
    }
    return const Center(
      child: Padding(
        padding: EdgeInsets.all(24),
        child: CircularProgressIndicator(),
      ),
    );
  }

  // Does the body currently show an empty state that carries its own "Add
  // something" button? Then the FAB is a SECOND copy of the same action on the
  // same screen. One Add ever: the two web front ends hide their fab on exactly
  // this condition (c/serve.c, app/app.css), and GUI.md's rule is that a change
  // to one surface is a change to all three.
  bool get _emptyStateOffersAdd {
    if (_ais == null) return false;      // the gate or a status message is showing
    switch (_view) {
      case 'timeline':
        return _tl.isEmpty;
      case 'tags':
        return false;                    // "No tags yet." offers nothing to press
      default:
        return _results.isEmpty && !_textSearch && !_searched;
    }
  }

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

  // Commit a still-pending delete for real. Idempotent: only fires if the id is
  // still pending (not undone, not already committed). Input-driven, so it never
  // depends on the snackbar's animated close alone.
  void _commitDelete(int id) {
    _delTimers.remove(id)?.cancel();
    if (_pendingDelete.remove(id) && _ais != null) {
      // del() returns false when the engine kept the record (unknown id, or a
      // write error). The row was already hidden, so say it failed and re-sync.
      if (!_ais!.del(id) && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text("Couldn't delete that item")));
        _setView(_view);
      }
    }
    // A committed delete must not keep offering UNDO: it would only fake-restore
    // a record the engine no longer has. Dismiss its snackbar if it is still up.
    _delSnack.remove(id)?.close();
  }

  // Called before a new delete and before any view re-query, so a "deleted" row
  // cannot resurrect on refresh.
  void _flushPendingDeletes() {
    for (final id in _pendingDelete.toList()) {
      _commitDelete(id);
    }
  }

  // The standard delete: pull the row from the visible list NOW and offer Undo in
  // a SnackBar; the engine is touched only if that window closes without an Undo
  // tap. Swipe and the ⋮ "Delete" menu both route here.
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
      // re-query dropped it before Detail -> Delete ran). With no snapshot there
      // is no Undo, so honor the delete outright.
      final ok = _ais?.del(id) ?? false;
      messenger.showSnackBar(SnackBar(
          content: Text(ok ? 'Deleted' : "Couldn't delete that item")));
      return;
    }
    _pendingDelete.add(id);
    // Slightly longer than the snackbar, so UNDO keeps its full window and the
    // commit still happens if the snackbar is dismissed early or never closes.
    _delTimers[id] = Timer(const Duration(milliseconds: 4500), () => _commitDelete(id));
    setState(() {
      // remove EVERY row of that id: a record can appear more than once across pages
      if (recallHit != null) _results = _results.where((h) => h.id != id).toList();
      if (tlRow != null) _tl = [..._tl]..removeAt(tlIdx);
    });
    var undone = false;
    final ctl = messenger.showSnackBar(SnackBar(
      content: const Text('Deleted'),
      duration: const Duration(seconds: 4),
      action: SnackBarAction(
        label: 'UNDO',
        onPressed: () {
          // Guard against an already-committed delete (a later delete or a view
          // change flushed it): UNDO must never fake-restore a gone record.
          if (!_pendingDelete.contains(id)) return;
          undone = true;
          _delTimers.remove(id)?.cancel();
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
      // Closed without an Undo tap: commit (a no-op if already flushed).
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

  Widget _deleteBg(ColorScheme cs) => Container(
        color: cs.error,
        alignment: Alignment.centerRight,
        padding: const EdgeInsets.only(right: 24),
        child: Icon(Icons.delete, color: cs.onError),
      );

  // The empty / first-run state; its button opens the same Add sheet as the FAB.
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

  // Edit a record's tags as chips. Apply sends the minimal +tag/-tag delta.
  Future<void> _editKeys(Hit hit) async {
    if (_ais == null) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Library is still opening. Try again in a moment.')));
      return;
    }
    if (_syncBlocks()) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('A sync is running. Try again in a moment.')));
      return; // don't write while a sync holds the handle
    }
    final original = _ais!.keysOf(hit.id).split(RegExp(r'\s+'))
        .where((t) => t.isNotEmpty).toList();
    // The dialog owns its fields and pops with the edited list (Apply commits
    // any token still in the field first), or null on Cancel.
    final tags = await showDialog<List<String>>(
      context: context,
      builder: (ctx) => EditTagsDialog(initial: original, suggest: _tagMatches),
    );
    if (tags == null || _ais == null) return;
    final delta = <String>[
      for (final t in original) if (!tags.contains(t)) '-$t',
      for (final t in tags) if (!original.contains(t)) t,
    ].join(' ');
    if (delta.isEmpty) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(const SnackBar(content: Text('No changes')));
      }
      return;
    }
    // A NUL or an over-long tag would be silently dropped by the engine.
    final content = contentError(value: '', keys: tags.join(' '));
    if (content != null) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(content)));
      }
      return;
    }
    // update() is false when the engine rejected the change (unknown or deleted).
    final updated = _ais!.update(hit.id, delta);
    if (mounted) {
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text(tagsUpdateMessage(updated))));
    }
    if (updated) {
      _setView(_view); // refresh whichever view is showing (matches _editValue),
      // not a blind _recall() that no-ops off the recall tab and leaves stale tags
    }
  }

  // Fix a record's value in place: the engine keeps its id and timeline slot.
  // A document (blobs/) row edits its full text (docRead: the list shows only a
  // bounded preview) and every save goes through the same doc-aware engine
  // entry as Add, so an edit may cross the one-line/document boundary either
  // way. Offered for plain and document rows; encrypted/away rows omit the item.
  // Returns the record's new STORED value when it changed (for a document, its
  // fresh blobs/ path), else null (cancel/no-op/failure).
  Future<String?> _editValue(int id, String oldValue) async {
    final messenger = ScaffoldMessenger.of(context);
    if (_ais == null) {
      messenger.showSnackBar(const SnackBar(
          content: Text('Library is still opening. Try again in a moment.')));
      return null;
    }
    if (_syncBlocks()) {
      messenger.showSnackBar(const SnackBar(
          content: Text('A sync is running. Try again in a moment.')));
      return null; // don't write while a sync holds the handle
    }
    var orig = oldValue;
    if (_isBlob(oldValue)) {
      final t = _ais!.docRead(oldValue);
      if (t == null) {
        // the row gates on _notHere, so this is a race (blob just left)
        messenger.showSnackBar(
            const SnackBar(content: Text('That note is not on this device')));
        return null;
      }
      orig = t;
    }
    final ctrl = TextEditingController(text: orig);
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => _OwnedFields(
        owned: [ctrl],
        child: AlertDialog(
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
      ),
    );
    final newValue = ctrl.text; // _OwnedFields frees ctrl with the route
    if (ok != true || _ais == null) return null;
    // Empty value: say so instead of silently closing the dialog. An unchanged
    // value is a genuine no-op and stays quiet. Don't trim into the stored value.
    if (newValue.trim().isEmpty) {
      messenger.showSnackBar(const SnackBar(content: Text("Value can't be empty")));
      return null;
    }
    if (newValue == orig) return null;
    // A NUL would be silently truncated by the engine. The line-length cap
    // applies only to what will be stored INLINE: multi-line (or over-long)
    // text goes out-of-line, where no such limit exists.
    if (newValue.contains('\u0000')) {
      messenger.showSnackBar(const SnackBar(
          content: Text('Remove the special (null) character before saving.')));
      return null;
    }
    if (!newValue.trimRight().contains('\n')) {
      final content = contentError(value: newValue, keys: '');
      if (content != null) {
        messenger.showSnackBar(SnackBar(content: Text(content)));
        return null;
      }
    }
    // oldValue stays the STORED string (a blobs/ path for a document): that is
    // the line the engine matches and replaces. newStored is what the record
    // holds now (the trimmed line, or a fresh blobs/ path).
    final (rc, newStored) = _ais!.setValueText(id, oldValue, newValue);
    if (!mounted) return null;
    final done = rc == 0;
    messenger.showSnackBar(SnackBar(
        content: Text(done
            ? 'Value updated'
            : rc == -2
                ? 'A note already holds that text: this one as another link, or another note'
                : rc == -3
                    ? 'A deleted note still holds that text: run Clean up first'
                    : "Couldn't update the value")));
    if (done) _setView(_view); // refresh whichever view is showing
    return done ? newStored : null;
  }

  // Reveal an encrypted ("aisc:") hit. Encrypted DOCUMENTS need the CLI.
  Future<void> _revealHit(Hit hit) async {
    final ctrl = TextEditingController();
    final pass = await showDialog<String>(
      context: context,
      builder: (ctx) => _OwnedFields(
        owned: [ctrl],
        child: AlertDialog(
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

  // A full-screen detail/edit page for one record, opened by TAPPING a row. Reuses
  // the row handlers and refreshes its own value/tags after an in-place edit. `ts`
  // is the save time when known: timeline rows carry it, recall hits don't.
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
                        final nv = await _editValue(id, curValue);
                        if (nv != null) setLocal(() => curValue = nv);
                      } else if (a == 'tags') {
                        await _editKeys(Hit(id, curValue));
                        setLocal(() => keys = _ais?.keysOf(id).trim() ?? '');
                      } else if (a == 'delete') {
                        Navigator.of(ctx).pop(); // back to the list first…
                        _deferDelete(id); // …then remove it with an Undo snackbar
                      }
                    },
                    itemBuilder: (_) => [
                      // plain and document rows edit as text; encrypted/away rows omit this
                      if (!isSecret && !away)
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
                  // Encrypted and away rows keep their treatment from the list.
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
      // Key search found nothing: the only place the full-text fallback is offered.
      if (_searched && _query.isNotEmpty) return _noTagMatch(cs);
      if (_searched) return _centerMsg('No results for "$_query"', cs);
      // First-run: an error keeps its plain message, a healthy engine the empty state.
      if (_ais == null) return _centerMsg(_status, cs);
      assert(_emptyStateOffersAdd);   // the FAB stands down on exactly this branch
      return _emptyState(cs,
          icon: Icons.note_add_outlined,
          line: 'Save a link, a note, or a fact, then find it later by its tags.');
    }
    // find() results reuse the recall row builder; the header above the list is the
    // only thing marking them as TEXT matches rather than tag matches.
    // A key search lays out [tag rows][footer][separator][value rows]: the value
    // section is always the tail of _results (see _recall/_loadMoreRecall), so
    // its start is the first row whose id sits in _valueOnly.
    final vFrom = _results.indexWhere((h) => _valueOnly.contains(h.id));
    final tagCount = vFrom >= 0 ? vFrom : _results.length;
    final head = tagCount + (_recallMore ? 1 : 0);
    final list = NotificationListener<ScrollNotification>(
      onNotification: (n) {
        if (_recallMore && _nearBottom(n)) _loadMoreRecall();
        return false;
      },
      child: ListView.separated(
      padding: const EdgeInsets.only(bottom: 88),
      itemCount: head + (vFrom >= 0 ? 1 + _results.length - tagCount : 0),
      separatorBuilder: (_, __) => const Divider(height: 1, indent: 16, endIndent: 16),
      itemBuilder: (_, i) {
        if (i < tagCount) return _resultRow(_results[i], cs);
        if (_recallMore && i == tagCount) return _loadingFooter();
        if (i == head) return _valueMatchSeparator(cs);
        return _resultRow(_results[i - head - 1 + tagCount], cs);
      },
    ),
    );
    if (_textSearch) {
      return Column(children: [_textMatchHeader(cs), Expanded(child: list)]);
    }
    return list;
  }

  // ONE row builder for every recall/find hit, whichever section it sits in:
  // a second copy is how the sections would drift apart.
  Widget _resultRow(Hit hit, ColorScheme cs) {
    final v = hit.value;
    final isSecret = v.startsWith('aisc:');
    final away = _notHere(v);
    return Dismissible(
      key: ValueKey(hit.id),
      direction: DismissDirection.endToStart,
      background: _deleteBg(cs),
      onDismissed: (_) => _deferDelete(hit.id),
      child: ListTile(
      // Primary path: tap the row for its detail/edit page.
      onTap: () => _openDetail(hit.id, v),
      visualDensity: const VisualDensity(vertical: -1),
      minVerticalPadding: 10,
      contentPadding: const EdgeInsets.symmetric(horizontal: 16),
      title: Row(mainAxisSize: MainAxisSize.min, children: [
        Flexible(
          child: isSecret
              ? Row(mainAxisSize: MainAxisSize.min, children: [
                  Icon(Icons.lock_outline, size: 16, color: cs.outline),
                  const SizedBox(width: 6),
                  Text('encrypted', style: TextStyle(color: cs.onSurfaceVariant)),
                ])
              : _rowLabel(v, cs),
        ),
        if (away) _notHereBadge(cs),
      ]),
      // The record's tags (fetched once when results loaded), plus a note when
      // it holds links this row does not show: deleting removes the WHOLE record.
      subtitle: Text(
        ((_resultKeys[hit.id]?.isNotEmpty ?? false)
                ? _resultKeys[hit.id]!
                : '(no tags)') +
            ((_resultExtra[hit.id] ?? 0) > 0
                ? '  ·  +${_resultExtra[hit.id]} more link'
                    '${_resultExtra[hit.id] == 1 ? '' : 's'}'
                : ''),
        style: Theme.of(context)
            .textTheme
            .bodySmall
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
              // plain and document rows edit as text; encrypted/away rows omit this
              if (!isSecret && !away)
                const PopupMenuItem(value: 'value', child: Text('Edit value')),
              const PopupMenuItem(value: 'edit', child: Text('Edit tags')),
              const PopupMenuItem(value: 'delete', child: Text('Delete')),
            ],
          ),
        ],
      ),
      ),
    );
  }

  // The trailing row of a paged list while the next keyset page loads. Kept tiny
  // so it reads as "more coming", not as a blocking state.
  Widget _loadingFooter() => const Padding(
        padding: EdgeInsets.symmetric(vertical: 16),
        child: Center(
          child: SizedBox(
            width: 20,
            height: 20,
            child: CircularProgressIndicator(strokeWidth: 2),
          ),
        ),
      );

  // The empty key-search state: no TAG matched, plus the full-text fallback as a
  // quiet TextButton rather than a primary action.
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

  // The one separator inside a key search's list: every row below it matched
  // the query in its VALUE text, not by tag.
  Widget _valueMatchSeparator(ColorScheme cs) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
        child: Row(children: [
          Icon(Icons.notes, size: 14, color: cs.onSurfaceVariant),
          const SizedBox(width: 6),
          Expanded(
            child: Text('matched in the value',
                style: Theme.of(context)
                    .textTheme
                    .bodySmall
                    ?.copyWith(color: cs.onSurfaceVariant)),
          ),
        ]),
      );

  // A quiet marker above find() results: note-TEXT matches, not tag matches.
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

  // Infinite scroll: true within 600px of the bottom, so the next keyset page
  // should be fetched. One threshold for every list (recall / timeline / tags).
  bool _nearBottom(ScrollNotification n) =>
      n.metrics.axis == Axis.vertical &&
      n.metrics.pixels >= n.metrics.maxScrollExtent - 600;

  // Page on with the keyset cursor: the next page older than the last id we hold.
  void _loadMoreTimeline() {
    if (_ais == null) return;
    final page = _ais!.timeline(before: _tlBefore, count: _tlPage, from: _tlFrom, to: _tlTo);
    setState(() {
      _tl = [..._tl, ...page.where((r) => !_pendingDelete.contains(r.id))];
      if (page.isNotEmpty) _tlBefore = page.last.id;
      _tlMore = page.length == _tlPage;
    });
  }

  Widget _rangeBar(ColorScheme cs) {
    final on = _tlFrom.isNotEmpty || _tlTo.isNotEmpty;
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 0),
      child: Row(children: [
        InputChip(
          avatar: Icon(Icons.date_range, size: 18, color: cs.onSurfaceVariant),
          label: Text(on ? '$_tlFrom \u2013 $_tlTo' : 'Date range'),
          visualDensity: VisualDensity.compact,
          onPressed: _pickRange,
          onDeleted: on ? _clearRange : null,
        ),
      ]),
    );
  }

  // Timeline: dateless rows surface first, then newest; grouped by day.
  Widget _timelineBody(ColorScheme cs) {
    // Opening or open-failed must not read as an empty timeline.
    final gate = _engineGate(cs);
    if (gate != null) return gate;
    if (_tl.isEmpty) {
      assert(_emptyStateOffersAdd);   // the FAB stands down on exactly this branch
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
        // Tap opens the detail/edit page; carry the ts so it can show the save time.
        onTap: () => _openDetail(r.id, r.value, ts: r.ts),
        visualDensity: const VisualDensity(vertical: -1),
        minVerticalPadding: 10,
        contentPadding: const EdgeInsets.symmetric(horizontal: 16),
        title: Row(mainAxisSize: MainAxisSize.min, children: [
          Flexible(
            child: r.value.startsWith('aisc:')
                ? Row(mainAxisSize: MainAxisSize.min, children: [
                    Icon(Icons.lock_outline, size: 16, color: cs.outline),
                    const SizedBox(width: 6),
                    Text('encrypted', style: TextStyle(color: cs.outline)),
                  ])
                : _rowLabel(r.value, cs),
          ),
          if (_notHere(r.value)) _notHereBadge(cs),
        ]),
        subtitle: Text('$time${r.keys.isEmpty ? '(no tags)' : r.keys}',
            style: Theme.of(context)
                .textTheme
                .bodySmall
                ?.copyWith(color: cs.onSurfaceVariant)),
        // The recall rows' overflow, so a keyless add stays actionable here too.
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
            // plain and document rows edit as text; encrypted/away rows omit this
            if (!r.value.startsWith('aisc:') && !_notHere(r.value))
              const PopupMenuItem(value: 'value', child: Text('Edit value')),
            const PopupMenuItem(value: 'edit', child: Text('Edit tags')),
            const PopupMenuItem(value: 'delete', child: Text('Delete')),
          ],
        ),
      ),
      ));
    }
    if (_tlMore) {
      // Infinite scroll pages this in; the button stays as an explicit fallback.
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
        // builder so only the on-screen rows of a large loaded range mount.
        child: NotificationListener<ScrollNotification>(
          onNotification: (n) {
            if (_tlMore && _nearBottom(n)) _loadMoreTimeline();
            return false;
          },
          child: ListView.builder(
            padding: const EdgeInsets.only(bottom: 88),
            itemCount: items.length,
            itemBuilder: (_, i) => items[i],
          ),
        ),
      ),
    ]);
  }

  // Tags: every key with its count, busiest first; tap a key to recall it.
  Widget _tagsBody(ColorScheme cs) {
    if (_tags.isEmpty) return _centerMsg('No tags yet.', cs);
    return NotificationListener<ScrollNotification>(
      onNotification: (n) {
        if (_tagsMore && _nearBottom(n)) _loadMoreTags();
        return false;
      },
      child: ListView.separated(
      padding: const EdgeInsets.only(bottom: 88),
      itemCount: _tags.length + (_tagsMore ? 1 : 0),
      separatorBuilder: (_, __) => const Divider(height: 1, indent: 16, endIndent: 16),
      itemBuilder: (_, i) {
        if (i >= _tags.length) return _loadingFooter();
        final t = _tags[i];
        return ListTile(
          visualDensity: const VisualDensity(vertical: -1),
          leading: Icon(Icons.label_outline, color: cs.onSurfaceVariant),
          title: Text(t.key),
          trailing: Text('${t.count}',
              style: Theme.of(context)
                  .textTheme
                  .labelLarge
                  ?.copyWith(color: cs.onSurfaceVariant)),
          onTap: () {
            _q.text = t.key;
            _setView('recall');
          },
        );
      },
    ),
    );
  }

  // The Add form. Keys are optional and prefilled from the search box; [value]
  // prefills the note itself (the Android share sheet lands here).
  void _showAdd({String value = ''}) {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      showDragHandle: true,
      builder: (ctx) => AddSheet(
        initialValue: value,
        initialKeys: _q.text.trim(),
        suggest: _tagMatches,
        onSave: _addSave,
      ),
    );
    // saved, cancelled, or swipe-dismissed: AddSheet's own dispose frees the
    // fields with the route either way.
  }

  // Validate-and-store for the Add sheet: the inline error to show, or null
  // when the record landed (the sheet closes itself; the view change and the
  // snackbar happen here). Kept on the page State so AddSheet stays pumpable
  // in a widget test with no engine.
  Future<String?> _addSave(
      {required String value,
      required String keys,
      required bool encrypt,
      required String passphrase,
      required String repeat}) async {
    // Store the value verbatim (no trim), like the in-place edit path;
    // addSaveError treats whitespace-only as empty.
    final k = _normKeys(keys);
    final err = addSaveError(
        value: value,
        engineReady: _ais != null,
        syncing: _syncBlocks(),
        encrypt: encrypt,
        passphrase: passphrase,
        passphraseRepeat: repeat,
        keys: k);
    if (err != null) return err;
    // addSaveError stays quiet when the engine is not ready (the FAB is
    // disabled then), but the share intake opens this sheet without the FAB.
    if (_ais == null) return 'Library is still opening. Try again in a moment.';
    final messenger = ScaffoldMessenger.of(context);
    // Commit any armed swipe-delete BEFORE a background encrypt: a UI-isolate
    // del() must not run on the shared handle while the off-isolate ais_put
    // does (a cross-isolate write race). The sheet is modal, so no new delete
    // can be armed meanwhile.
    _flushPendingDeletes();
    // The one save that adds no live record is a merge: same text, same record
    // (value is identity), restamped to today. Count around the save to say so
    // afterwards; an encrypted save always mints a fresh record (its IV).
    final before = encrypt ? -1 : _ais!.countLive();
    int id = -1;
    try {
      if (encrypt) {
        id = await _ais!.storeEncryptedAsync(k, value, passphrase);
      } else {
        id = _ais!.store(k, value);
      }
    } catch (e) {
      return 'Could not save: $e';
    }
    // The engine returns -1 when nothing was stored (bad args, blob write
    // failure, crypto not built).
    if (!saveSucceeded(id)) return saveOutcomeMessage(id, k);
    if (!mounted) return null;
    // Show the new record at the top of Recent. Saving is not a query: don't
    // drop into a search for it.
    final merged = before >= 0 && _ais!.countLive() == before;
    _setView('timeline');
    _runFolderSync(silent: true); // push the new record to peers
    messenger.showSnackBar(
        SnackBar(content: Text(saveOutcomeMessage(id, k, merged: merged))));
    return null;
  }

  @override
  void dispose() {
    _flushPendingDeletes();          // an armed delete must not die with the page
    WidgetsBinding.instance.removeObserver(this);
    _qFocus.dispose();
    _debounce?.cancel();
    _speech.cancel(); // stop any active recognizer session
    // A background LAN sync may still hold this handle by address; close()ing
    // (freeing) it now would be a use-after-free in that isolate. This is the
    // top-level page, so a dispose means the app is going away: let the OS reclaim it.
    if (!_syncBusy) _ais?.close();
    super.dispose();
  }
}

// One row of completion chips under a tag field: THE tag-autocomplete widget,
// shared by the Add sheet, the Edit-tags dialog and the header search field
// (three hand-rolled copies is how rows drift apart here). [lookup] gives the
// existing tags for the token being typed -- the trailing token, split on
// spaces/commas like _normKeys -- and an empty token shows nothing. A tap
// completes that token plus a trailing space and hands focus back to the
// field; [onPick] replaces that default where completion means something else
// (Edit-tags commits a chip), [onCompleted] runs after it (search re-queries:
// a programmatic controller change fires no onChanged).
class TagSuggestRow extends StatelessWidget {
  final TextEditingController controller;
  final FocusNode? focusNode;
  final List<String> Function(String prefix) lookup;
  final void Function(String tag)? onPick;
  final VoidCallback? onCompleted;
  static const int _max = 6;
  const TagSuggestRow(
      {super.key,
      required this.controller,
      required this.lookup,
      this.focusNode,
      this.onPick,
      this.onCompleted});

  // The trailing token, per _normKeys rules (spaces and commas separate tags).
  static String _token(String text) {
    final i = text.lastIndexOf(RegExp(r'[,\s]'));
    return i < 0 ? text : text.substring(i + 1);
  }

  void _apply(String tag) {
    final text = controller.text;
    final head = text.substring(0, text.length - _token(text).length);
    controller.value = TextEditingValue(
      text: '$head$tag ',
      selection: TextSelection.collapsed(offset: head.length + tag.length + 1),
    );
    onCompleted?.call();
  }

  @override
  Widget build(BuildContext context) => ValueListenableBuilder<TextEditingValue>(
        valueListenable: controller,
        builder: (context, v, _) {
          final tok = _token(v.text);
          final tags = tok.isEmpty ? const <String>[] : lookup(tok).take(_max).toList();
          if (tags.isEmpty) return const SizedBox.shrink();
          return Padding(
            padding: const EdgeInsets.only(top: 6),
            // One row, never a wrap: scrolls sideways instead of growing down
            // and pushing the field it belongs to off the keyboard.
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(children: [
                for (final t in tags)
                  Padding(
                    padding: const EdgeInsets.only(right: 6),
                    child: ActionChip(
                      label: Text(t),
                      visualDensity: VisualDensity.compact,
                      onPressed: () {
                        if (onPick != null) {
                          onPick!(t);
                        } else {
                          _apply(t);
                        }
                        focusNode?.requestFocus();
                      },
                    ),
                  ),
              ]),
            ),
          );
        },
      );
}

// The Add form, extracted from _showAdd so a widget test can pump it with a
// fake [onSave] and [suggest] (the real ones need the engine, which
// `flutter test` never has). Owns its fields: State.dispose frees them at
// route teardown, the timing _OwnedFields exists to guarantee.
class AddSheet extends StatefulWidget {
  final String initialValue;
  final String initialKeys;
  // Existing tags for the token being typed (busiest first, already capped).
  final List<String> Function(String prefix) suggest;
  // Validate-and-store. Returns the inline error to show, or null when the
  // record landed and the sheet may close. Gets the raw field texts, the
  // confirmation [repeat] included: the match check is validation, and
  // validation lives behind this seam (add_validation.dart).
  final Future<String?> Function(
      {required String value,
      required String keys,
      required bool encrypt,
      required String passphrase,
      required String repeat}) onSave;
  const AddSheet(
      {super.key,
      this.initialValue = '',
      this.initialKeys = '',
      required this.suggest,
      required this.onSave});
  @override
  State<AddSheet> createState() => _AddSheetState();
}

class _AddSheetState extends State<AddSheet> {
  late final _valCtrl = TextEditingController(text: widget.initialValue);
  late final _keysCtrl = TextEditingController(text: widget.initialKeys);
  final _ppCtrl = TextEditingController();
  final _pp2Ctrl = TextEditingController();
  final _keysFocus = FocusNode();
  bool _encrypt = false; // off by default
  bool _saving = false;  // true while the off-isolate encrypt runs
  bool _ppShow = false;  // reveal toggle for the sealing passphrase (both fields)
  String? _error;        // in-sheet feedback so a save never fails silently

  @override
  void dispose() {
    _valCtrl.dispose();
    _keysCtrl.dispose();
    _ppCtrl.dispose();
    _pp2Ctrl.dispose();
    _keysFocus.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    if (_encrypt) setState(() => _saving = true);
    final err = await widget.onSave(
        value: _valCtrl.text,
        keys: _keysCtrl.text,
        encrypt: _encrypt,
        passphrase: _ppCtrl.text,
        repeat: _pp2Ctrl.text);
    // The sheet is swipe-dismissible during "Encrypting…", and setState on a
    // disposed element throws.
    if (!mounted) return;
    if (err != null) {
      setState(() {
        _saving = false;
        _error = err;
      });
      return;
    }
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) => Padding(
        padding: EdgeInsets.only(
            bottom: MediaQuery.of(context).viewInsets.bottom + 16,
            left: 16, right: 16, top: 4),
        // Scrolls: with the keyboard up and Encrypt on, the fixed Column
        // overflowed the sheet.
        child: SingleChildScrollView(
          child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text('Add to your memory',
                style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 14),
            // The thing being saved comes first, then how to find it again. The
            // other order asked for the label before the thing it labels, which
            // is not how anyone describes what they are doing.
            TextField(
              controller: _valCtrl,
              autofocus: true,
              minLines: 1,
              maxLines: 3,
              decoration: const InputDecoration(
                labelText: 'What to remember',
                hintText: 'a link, a note, a phone number…',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _keysCtrl,
              focusNode: _keysFocus,
              decoration: const InputDecoration(
                labelText: 'Tags (space-separated, optional)',
                hintText: 'e.g. venice italy hotel',
                border: OutlineInputBorder(),
              ),
            ),
            TagSuggestRow(
              controller: _keysCtrl,
              focusNode: _keysFocus,
              lookup: widget.suggest,
            ),
            const SizedBox(height: 4),
            Row(children: [
              Switch(
                value: _encrypt,
                onChanged: (b) => setState(() => _encrypt = b),
              ),
              const Text('Encrypt'),
            ]),
            if (_encrypt) ...[
              TextField(
                controller: _ppCtrl,
                obscureText: !_ppShow,
                decoration: InputDecoration(
                  labelText: 'Passphrase',
                  border: const OutlineInputBorder(),
                  suffixIcon: IconButton(
                    icon: Icon(_ppShow ? Icons.visibility_off : Icons.visibility),
                    tooltip: _ppShow ? 'Hide' : 'Show',
                    onPressed: () => setState(() => _ppShow = !_ppShow),
                  ),
                ),
              ),
              const SizedBox(height: 8),
              TextField(
                controller: _pp2Ctrl,
                obscureText: !_ppShow,
                decoration: const InputDecoration(
                  labelText: 'Repeat passphrase',
                  border: OutlineInputBorder(),
                ),
              ),
              Padding(
                padding: const EdgeInsets.only(top: 6, bottom: 4),
                child: Text('A lost passphrase cannot be recovered.',
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: Theme.of(context).colorScheme.onSurfaceVariant)),
              ),
            ],
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(top: 2, bottom: 6),
                child: Text(_error!,
                    style: TextStyle(color: Theme.of(context).colorScheme.error)),
              ),
            const SizedBox(height: 16),
            FilledButton.icon(
              icon: _saving
                  ? const SizedBox(
                      width: 16, height: 16,
                      child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.check),
              label: Text(_saving ? 'Encrypting…' : 'Save'),
              onPressed: _saving ? null : _save,
            ),
          ],
        ),
        ),
      );
}

// The Edit-tags dialog, extracted from _editKeys so a widget test can pump it
// with a fake [suggest]. Pops with the edited tag list on Apply (any token
// still in the field is committed first), or null on Cancel. Owns and frees
// its own field and focus node.
class EditTagsDialog extends StatefulWidget {
  final List<String> initial;
  final List<String> Function(String prefix) suggest;
  const EditTagsDialog({super.key, required this.initial, required this.suggest});
  @override
  State<EditTagsDialog> createState() => _EditTagsDialogState();
}

class _EditTagsDialogState extends State<EditTagsDialog> {
  late final List<String> _tags = [...widget.initial];
  final _ctrl = TextEditingController();
  final _focus = FocusNode();

  @override
  void dispose() {
    _ctrl.dispose();
    _focus.dispose();
    super.dispose();
  }

  // Split on spaces/commas so a pasted or submitted multi-word string becomes
  // one chip PER tag, matching how the engine tokenizes on update.
  void _addToken(String raw) {
    final parts =
        raw.split(RegExp(r'[,\s]+')).where((p) => p.isNotEmpty).toList();
    final fresh = parts.where((p) => !_tags.contains(p)).toList();
    if (fresh.isEmpty) return;
    setState(() => _tags.addAll(fresh));
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
        title: const Text('Edit tags'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (_tags.isNotEmpty)
              Wrap(
                spacing: 6,
                runSpacing: 4,
                children: [
                  for (final t in _tags)
                    InputChip(
                      label: Text(t),
                      visualDensity: VisualDensity.compact,
                      onDeleted: () => setState(() => _tags.remove(t)),
                    ),
                ],
              ),
            const SizedBox(height: 8),
            TextField(
              controller: _ctrl,
              focusNode: _focus,
              autofocus: true,
              decoration: const InputDecoration(hintText: 'Add a tag'),
              onChanged: (v) {
                // a space or comma commits the token in progress
                if (v.endsWith(' ') || v.endsWith(',')) {
                  _addToken(v.substring(0, v.length - 1));
                  _ctrl.clear();
                }
              },
              onSubmitted: (v) {
                _addToken(v);
                _ctrl.clear();
                _focus.requestFocus();
              },
            ),
            // A suggestion tap commits straight to a chip: the field's own
            // "space commits" rule above never sees a programmatic completion.
            // Tags already chipped are not re-offered.
            TagSuggestRow(
              controller: _ctrl,
              focusNode: _focus,
              lookup: (p) =>
                  widget.suggest(p).where((t) => !_tags.contains(t)).toList(),
              onPick: (t) {
                _addToken(t);
                _ctrl.clear();
              },
            ),
          ],
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context, null),
              child: const Text('Cancel')),
          FilledButton(
              onPressed: () {
                _addToken(_ctrl.text); // capture any token still in the field
                Navigator.pop(context, List<String>.of(_tags));
              },
              child: const Text('Apply')),
        ],
      );
}

// A non-dismissible barrier dialog shown while a sync FFI call blocks: it keeps
// the UI off the shared engine handle during the sync and closes itself when the
// future completes (peer done, or timeout). Used by both receive and send.
// Owns a dialog's or sheet's TextEditingControllers/FocusNodes and frees them in
// its own dispose(). Mounted INSIDE the route, so the framework runs that dispose
// at route teardown, after the exit animation, and unmounts deepest-first -- the
// fields have already detached. Freeing at pop instead (a .whenComplete, or the
// line after `await showDialog`) runs while the closing route's fields still
// listen, and every Save and Cancel dies on the '_dependents.isEmpty' red screen.
// Reading `ctrl.text` right after the await stays safe: the pop resolves it
// before the animation, the teardown that frees the field comes after.
class _OwnedFields extends StatefulWidget {
  final List<ChangeNotifier> owned;
  final Widget child;
  const _OwnedFields({required this.owned, required this.child});
  @override
  State<_OwnedFields> createState() => _OwnedFieldsState();
}

class _OwnedFieldsState extends State<_OwnedFields> {
  @override
  void dispose() {
    for (final n in widget.owned) {
      n.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => widget.child;
}

class _SyncWaitDialog extends StatefulWidget {
  final String title;
  final String? qrData;       // ais:// pairing link; shown as a QR on host, null hides it
  final String? commandLabel; // line shown above the command (host); null hides it
  final String? command;      // shown on host; null on join
  final String? intro;        // what to do next, and on WHICH device
  final String waiting;
  final String? note;         // honest caption: hiding does NOT stop the sync
  final Future<int> done;
  const _SyncWaitDialog(
      {required this.title,
      this.qrData,
      this.commandLabel,
      this.command,
      this.intro,
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
    // Auto-close on finish: pop(false) means "completed on its own", pop(true) is
    // the user hiding it.
    widget.done.whenComplete(() {
      if (mounted) Navigator.of(context).pop(false);
    });
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
        title: Text(widget.title),
        // Bound the width: QrImageView is a CustomPaint with no intrinsic size,
        // so an unbounded-width AlertDialog content collapses it and the whole
        // card never paints.
        content: SizedBox(
          width: 260,
          child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (widget.intro != null) ...[
              Text(widget.intro!, style: Theme.of(context).textTheme.bodySmall),
              const SizedBox(height: 12),
            ],
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
              Text(
                  "If its camera won't open the code, copy the address and token "
                  "below and type them into Join there instead.",
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
              // Selecting monospace text is fiddly on a phone, and the fallback
              // when a camera refuses the ais:// scheme is retyping 32 hex chars.
              Align(
                alignment: Alignment.centerLeft,
                child: TextButton.icon(
                  icon: const Icon(Icons.copy, size: 16),
                  label: const Text('Copy'),
                  onPressed: () {
                    Clipboard.setData(ClipboardData(text: widget.command!));
                    ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
                        content: Text('Address and token copied')));
                  },
                ),
              ),
              const SizedBox(height: 8),
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
          // "Hide", not "Cancel": an in-flight network sync cannot be safely
          // aborted mid-merge, so this only dismisses the dialog; the sync keeps
          // running and the result arrives as a snackbar.
          TextButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('Hide')),
        ],
      );
}
