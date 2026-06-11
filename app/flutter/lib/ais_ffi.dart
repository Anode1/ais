// ais_ffi.dart -- Dart FFI binding to the AIS C engine (see ../../../c/embed.h).
// The C does the work; this only marshals strings across the boundary. Same
// engine, same `id|value` contract as the CLI and `ais serve`.
import 'dart:ffi';
import 'dart:io' show Platform;
import 'package:ffi/ffi.dart';

// --- C signatures (native) and their Dart shapes -------------------------
typedef _OpenC = Pointer<Void> Function(Pointer<Utf8>);
typedef _OpenD = Pointer<Void> Function(Pointer<Utf8>);
typedef _RecallC = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32);
typedef _RecallD = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int);
typedef _StoreC = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _FreeC = Void Function(Pointer<Utf8>);
typedef _FreeD = void Function(Pointer<Utf8>);
typedef _CloseC = Void Function(Pointer<Void>);
typedef _CloseD = void Function(Pointer<Void>);
typedef _TimelineC = Pointer<Utf8> Function(Pointer<Void>, Int32);
typedef _TimelineD = Pointer<Utf8> Function(Pointer<Void>, int);
typedef _TagsC = Pointer<Utf8> Function(Pointer<Void>);
typedef _TagsD = Pointer<Utf8> Function(Pointer<Void>);

/// One timeline row: a record's save time (empty if undated), its keys, value.
class TlRow {
  final String ts, keys, value;
  const TlRow(this.ts, this.keys, this.value);
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

/// A handle to one on-disk index. Open once, recall/store many times, close.
class AisEngine {
  final DynamicLibrary _lib = _load();
  late final Pointer<Void> _h;
  late final _OpenD _open = _lib.lookupFunction<_OpenC, _OpenD>('ais_embed_open');
  late final _RecallD _recall = _lib.lookupFunction<_RecallC, _RecallD>('ais_embed_recall');
  late final _StoreD _store = _lib.lookupFunction<_StoreC, _StoreD>('ais_embed_store');
  late final _FreeD _free = _lib.lookupFunction<_FreeC, _FreeD>('ais_embed_free');
  late final _CloseD _close = _lib.lookupFunction<_CloseC, _CloseD>('ais_embed_close');
  late final _TimelineD _timelineFn =
      _lib.lookupFunction<_TimelineC, _TimelineD>('ais_embed_timeline');
  late final _TagsD _tagsFn = _lib.lookupFunction<_TagsC, _TagsD>('ais_embed_tags');

  AisEngine(String indexDir) {
    final dir = indexDir.toNativeUtf8();
    _h = _open(dir);
    calloc.free(dir);
    if (_h == nullptr) throw Exception('AIS: cannot open index at $indexDir');
  }

  /// Recall the values filed under [keys]. orMode false = AND, true = OR.
  List<String> recall(String keys, {bool orMode = false}) {
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
          final i = l.indexOf('|'); // strip the "id|" prefix
          return i >= 0 ? l.substring(i + 1) : l;
        })
        .toList();
  }

  /// The most-recent records, dateless first then newest. limit <= 0 = default.
  List<TlRow> timeline({int limit = 0}) {
    final p = _timelineFn(_h, limit);
    if (p == nullptr) return const [];
    final text = p.toDartString();
    _free(p);
    return text.split('\n').where((l) => l.isNotEmpty).map((l) {
      final a = l.indexOf('|'),
          b = l.indexOf('|', a + 1),
          c = l.indexOf('|', b + 1); // value may contain '|', so take the rest
      return TlRow(l.substring(a + 1, b), l.substring(b + 1, c), l.substring(c + 1));
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

  void close() => _close(_h);
}
