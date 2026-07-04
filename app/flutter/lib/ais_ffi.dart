// ais_ffi.dart -- Dart FFI binding to the AIS C engine (see ../../../c/embed.h).
// The C does the work; this only marshals strings across the boundary. Same
// engine, same `id|value` contract as the CLI and `ais serve`.
import 'dart:ffi';
import 'dart:io' show Platform;
import 'dart:isolate';
import 'package:ffi/ffi.dart';

// --- C signatures (native) and their Dart shapes -------------------------
typedef _OpenC = Pointer<Void> Function(Pointer<Utf8>);
typedef _OpenD = Pointer<Void> Function(Pointer<Utf8>);
typedef _RecallC = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32);
typedef _RecallD = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int);
typedef _StoreC = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreEncC = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreEncD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _RevealC = Pointer<Utf8> Function(Pointer<Utf8>, Pointer<Utf8>);
typedef _RevealD = Pointer<Utf8> Function(Pointer<Utf8>, Pointer<Utf8>);
typedef _DisplayC = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>);
typedef _DisplayD = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>);
typedef _DelC = Int32 Function(Pointer<Void>, Int64);
typedef _DelD = int Function(Pointer<Void>, int);
typedef _UpdateC = Int32 Function(Pointer<Void>, Int64, Pointer<Utf8>);
typedef _UpdateD = int Function(Pointer<Void>, int, Pointer<Utf8>);
typedef _PullC = Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _PullD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _ServeC = Int32 Function(Pointer<Void>, Int32, Pointer<Utf8>);
typedef _ServeD = int Function(Pointer<Void>, int, Pointer<Utf8>);
typedef _FreeC = Void Function(Pointer<Utf8>);
typedef _FreeD = void Function(Pointer<Utf8>);
typedef _CloseC = Void Function(Pointer<Void>);
typedef _CloseD = void Function(Pointer<Void>);
typedef _TimelineC = Pointer<Utf8> Function(Pointer<Void>, Int64, Int32, Pointer<Utf8>, Pointer<Utf8>);
typedef _TimelineD = Pointer<Utf8> Function(Pointer<Void>, int, int, Pointer<Utf8>, Pointer<Utf8>);
typedef _TagsC = Pointer<Utf8> Function(Pointer<Void>);
typedef _TagsD = Pointer<Utf8> Function(Pointer<Void>);
typedef _DefaultSetC = Int32 Function(Pointer<Utf8>);
typedef _DefaultSetD = int Function(Pointer<Utf8>);
typedef _LocateC = Int32 Function(Pointer<Utf8>, IntPtr);     // (out, outsz)
typedef _LocateD = int Function(Pointer<Utf8>, int);

/// One recall hit: the record id (the handle for delete/update) and its value.
class Hit {
  final int id;
  final String value;
  const Hit(this.id, this.value);
}

/// One timeline row: the record id (the paging cursor and delete/update handle),
/// its save time (empty if undated), its keys, value.
class TlRow {
  final int id;
  final String ts, keys, value;
  const TlRow(this.id, this.ts, this.keys, this.value);
}

/// One tag: the key and how many records are filed under it.
class TagRow {
  final String key;
  final int count;
  const TagRow(this.key, this.count);
}

DynamicLibrary _load() {
  if (Platform.isAndroid || Platform.isLinux) return DynamicLibrary.open('libais.so');
  if (Platform.isWindows) return DynamicLibrary.open('ais.dll');
  // iOS / macOS: the C is statically linked into the app binary.
  if (Platform.isIOS || Platform.isMacOS) return DynamicLibrary.process();
  throw UnsupportedError('AIS: unsupported platform');
}

/// Index resolution and saved-default, independent of an open handle -- so the
/// app can find the same index the CLI would (and persist a new choice) before
/// it opens anything. No env vars: the C resolves home via the OS.
class AisIndex {
  static final DynamicLibrary _lib = _load();
  static final _LocateD _locate =
      _lib.lookupFunction<_LocateC, _LocateD>('ais_embed_locate');
  static final _DefaultSetD _defaultSet =
      _lib.lookupFunction<_DefaultSetC, _DefaultSetD>('ais_embed_default_set');

  /// The index a bare run would use (nearest .ais/, ~/.ais/config, else ~/.ais),
  /// or null if it cannot be resolved.
  static String? locate() {
    final buf = calloc<Uint8>(4096);
    try {
      final rc = _locate(buf.cast<Utf8>(), 4096);
      return rc == 0 ? buf.cast<Utf8>().toDartString() : null;
    } finally {
      calloc.free(buf);
    }
  }

  /// Persist [dir] as the saved default index (~/.ais/config). True on success.
  static bool setDefault(String dir) {
    final d = dir.toNativeUtf8();
    try {
      return _defaultSet(d) == 0;
    } finally {
      calloc.free(d);
    }
  }
}

/// A handle to one on-disk index. Open once, recall/store many times, close.
class AisEngine {
  final DynamicLibrary _lib = _load();
  late final Pointer<Void> _h;
  late final _OpenD _open = _lib.lookupFunction<_OpenC, _OpenD>('ais_embed_open');
  late final _RecallD _recall = _lib.lookupFunction<_RecallC, _RecallD>('ais_embed_recall');
  late final _StoreD _store = _lib.lookupFunction<_StoreC, _StoreD>('ais_embed_store');
  late final _StoreEncD _storeEnc =
      _lib.lookupFunction<_StoreEncC, _StoreEncD>('ais_embed_store_encrypted');
  late final _RevealD _reveal =
      _lib.lookupFunction<_RevealC, _RevealD>('ais_embed_reveal');
  late final _DisplayD _displayFn =
      _lib.lookupFunction<_DisplayC, _DisplayD>('ais_embed_display');
  late final _FreeD _free = _lib.lookupFunction<_FreeC, _FreeD>('ais_embed_free');
  late final _CloseD _close = _lib.lookupFunction<_CloseC, _CloseD>('ais_embed_close');
  late final _TimelineD _timelineFn =
      _lib.lookupFunction<_TimelineC, _TimelineD>('ais_embed_timeline');
  late final _TagsD _tagsFn = _lib.lookupFunction<_TagsC, _TagsD>('ais_embed_tags');
  late final _DelD _del = _lib.lookupFunction<_DelC, _DelD>('ais_embed_del');
  late final _UpdateD _update = _lib.lookupFunction<_UpdateC, _UpdateD>('ais_embed_update');

  AisEngine(String indexDir) {
    final dir = indexDir.toNativeUtf8();
    _h = _open(dir);
    calloc.free(dir);
    if (_h == nullptr) throw Exception('AIS: cannot open index at $indexDir');
  }

  /// Recall the records filed under [keys]. orMode false = AND, true = OR.
  /// Each hit keeps its id (the handle for [del]/[update]) and value.
  List<Hit> recall(String keys, {bool orMode = false}) {
    final k = keys.toNativeUtf8();
    final p = _recall(_h, k, orMode ? 1 : 0);
    calloc.free(k);
    if (p == nullptr) return const [];
    final text = p.toDartString();
    _free(p);
    return text
        .split('\n')
        .where((l) => l.isNotEmpty)
        .map((l) {
          final i = l.indexOf('|'); // split the "id|value" line (value may hold '|')
          if (i < 0) return Hit(0, l);
          return Hit(int.tryParse(l.substring(0, i)) ?? 0, l.substring(i + 1));
        })
        .toList();
  }

  /// A timeline page: the [count] records with id < [before], newest id first.
  /// before <= 0 starts from the newest; pass the last row's id to page on. A
  /// page shorter than [count] means there are no more. [from]/[to] are
  /// "YYYY-MM-DD" bounds (inclusive; '' = open); a bounded range drops dateless
  /// records. The range is held constant while paging on [before].
  List<TlRow> timeline({int before = 0, int count = 0, String from = '', String to = ''}) {
    final f = from.toNativeUtf8();
    final t = to.toNativeUtf8();
    final p = _timelineFn(_h, before, count, f, t);
    calloc.free(f);
    calloc.free(t);
    if (p == nullptr) return const [];
    final text = p.toDartString();
    _free(p);
    return text.split('\n').where((l) => l.isNotEmpty).map((l) {
      final a = l.indexOf('|'),
          b = l.indexOf('|', a + 1),
          c = l.indexOf('|', b + 1); // value may contain '|', so take the rest
      return TlRow(int.tryParse(l.substring(0, a)) ?? 0,
          l.substring(a + 1, b), l.substring(b + 1, c), l.substring(c + 1));
    }).toList();
  }

  /// Every distinct key with its record count, busiest first.
  List<TagRow> tags() {
    final p = _tagsFn(_h);
    if (p == nullptr) return const [];
    final text = p.toDartString();
    _free(p);
    return text.split('\n').where((l) => l.isNotEmpty).map((l) {
      final i = l.indexOf('|');
      return TagRow(l.substring(i + 1), int.tryParse(l.substring(0, i)) ?? 0);
    }).toList();
  }

  /// Store [value] under [keys]; returns the record id, or -1 on error.
  int store(String keys, String value) {
    final k = keys.toNativeUtf8();
    final v = value.toNativeUtf8();
    final id = _store(_h, k, v);
    calloc.free(k);
    calloc.free(v);
    return id;
  }

  /// Store [value] under [keys], ENCRYPTED under [passphrase] (the "aisc:"
  /// marker). Returns the record id, or -1 (error, or crypto not built).
  int storeEncrypted(String keys, String value, String passphrase) {
    final k = keys.toNativeUtf8();
    final v = value.toNativeUtf8();
    final p = passphrase.toNativeUtf8();
    final id = _storeEnc(_h, k, v, p);
    calloc.free(k);
    calloc.free(v);
    calloc.free(p);
    return id;
  }

  /// Decrypt a marked ("aisc:") [value] under [passphrase]; returns the
  /// cleartext, or null (wrong passphrase, or not an inline secret).
  String? reveal(String value, String passphrase) {
    final v = value.toNativeUtf8();
    final p = passphrase.toNativeUtf8();
    final r = _reveal(v, p);
    calloc.free(v);
    calloc.free(p);
    if (r == nullptr) return null;
    final s = r.toDartString();
    _free(r);
    return s;
  }

  /// Resolve [value] to the text to SHOW: a document blob's content (bounded),
  /// else [value] verbatim. Blob resolution lives in the C engine (shared with
  /// the CLI and `ais serve`), so the app never reads blob files itself.
  String display(String value) {
    final v = value.toNativeUtf8();
    final p = _displayFn(_h, v);
    calloc.free(v);
    if (p == nullptr) return value;
    final s = p.toDartString();
    _free(p);
    return s;
  }

  /// [storeEncrypted] off the UI isolate: the Argon2 KDF is ~1s, so run it in a
  /// background isolate to keep the UI responsive. The engine handle is shared
  /// across isolates (same process), so it is passed by address.
  Future<int> storeEncryptedAsync(String keys, String value, String passphrase) {
    final addr = _h.address;
    return Isolate.run(() {
      final lib = _load();
      final fn =
          lib.lookupFunction<_StoreEncC, _StoreEncD>('ais_embed_store_encrypted');
      final k = keys.toNativeUtf8();
      final v = value.toNativeUtf8();
      final p = passphrase.toNativeUtf8();
      final id = fn(Pointer<Void>.fromAddress(addr), k, v, p);
      calloc.free(k);
      calloc.free(v);
      calloc.free(p);
      return id;
    });
  }

  /// [reveal] off the UI isolate (the decrypt KDF is ~1s). No handle needed.
  Future<String?> revealAsync(String value, String passphrase) {
    return Isolate.run(() {
      final lib = _load();
      final fn = lib.lookupFunction<_RevealC, _RevealD>('ais_embed_reveal');
      final freeFn = lib.lookupFunction<_FreeC, _FreeD>('ais_embed_free');
      final v = value.toNativeUtf8();
      final p = passphrase.toNativeUtf8();
      final r = fn(v, p);
      calloc.free(v);
      calloc.free(p);
      if (r == nullptr) return null;
      final s = r.toDartString();
      freeFn(r);
      return s;
    });
  }

  /// Pull + merge a peer's `ais --export --serve` over the LAN (sync: Receive).
  /// Runs off the UI isolate (it blocks on the network). Returns 0 = merged,
  /// -1 = bad URL/args, -2 = could not connect / wrong token / timeout.
  Future<int> pullAsync(String url, String token, {bool bidir = false}) {
    final addr = _h.address;
    final name = bidir ? 'ais_embed_sync_pull' : 'ais_embed_pull';
    return Isolate.run(() {
      final lib = _load();
      final fn = lib.lookupFunction<_PullC, _PullD>(name);
      final u = url.toNativeUtf8();
      final t = token.toNativeUtf8();
      final rc = fn(Pointer<Void>.fromAddress(addr), u, t);
      calloc.free(u);
      calloc.free(t);
      return rc;
    });
  }

  /// Serve this index to one LAN peer that pulls with `ais --import` (sync:
  /// Send). Blocks up to ~120s for one peer, so run it off the UI isolate.
  /// Returns 0 = a peer pulled and merged, -1 = bad args, -2 = no peer
  /// completed (timeout / wrong token / error).
  Future<int> serveAsync(int port, String token, {bool bidir = false}) {
    final addr = _h.address;
    final name = bidir ? 'ais_embed_sync_serve' : 'ais_embed_serve';
    return Isolate.run(() {
      final lib = _load();
      final fn = lib.lookupFunction<_ServeC, _ServeD>(name);
      final t = token.toNativeUtf8();
      final rc = fn(Pointer<Void>.fromAddress(addr), port, t);
      calloc.free(t);
      return rc;
    });
  }

  /// Delete record [id]. True on success.
  bool del(int id) => _del(_h, id) == 0;

  /// Edit record [id]'s keys: a bare token attaches, a `-key` token detaches.
  /// True on success (false if id unknown/deleted).
  bool update(int id, String keys) {
    final k = keys.toNativeUtf8();
    final rc = _update(_h, id, k);
    calloc.free(k);
    return rc == 0;
  }

  void close() => _close(_h);
}
