// Live verification of the tag-editor data path: ais_embed_keys (new native
// symbol) + the exact +tag/-tag delta the chip editor computes. Hermetic: a
// temp index, real libais.so. Run from app/flutter with LD_LIBRARY_PATH set.
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

typedef _OpenC = Pointer<Void> Function(Pointer<Utf8>);
typedef _StoreC = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _KeysC = Pointer<Utf8> Function(Pointer<Void>, Int64);
typedef _KeysD = Pointer<Utf8> Function(Pointer<Void>, int);
typedef _UpdateC = Int32 Function(Pointer<Void>, Int64, Pointer<Utf8>);
typedef _UpdateD = int Function(Pointer<Void>, int, Pointer<Utf8>);
typedef _RecallC = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32);
typedef _RecallD = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int);
typedef _FreeC = Void Function(Pointer<Utf8>);
typedef _FreeD = void Function(Pointer<Utf8>);

void main() {
  final lib = DynamicLibrary.open('libais.so');
  final open = lib.lookupFunction<_OpenC, _OpenC>('ais_embed_open');
  final store = lib.lookupFunction<_StoreC, _StoreD>('ais_embed_store');
  final keys = lib.lookupFunction<_KeysC, _KeysD>('ais_embed_keys');
  final update = lib.lookupFunction<_UpdateC, _UpdateD>('ais_embed_update');
  final recall = lib.lookupFunction<_RecallC, _RecallD>('ais_embed_recall');
  final free = lib.lookupFunction<_FreeC, _FreeD>('ais_embed_free');

  final dir = Directory.systemTemp.createTempSync('ais_verify_');
  final dp = dir.path.toNativeUtf8();
  final h = open(dp);
  if (h == nullptr) {
    print('FAIL: ais_embed_open returned null for ${dir.path}');
    exit(1);
  }

  String keysOf(int id) {
    final p = keys(h, id);
    if (p == nullptr) return '<null>';
    final s = p.toDartString();
    free(p);
    return s;
  }

  String recallOf(String k) {
    final kp = k.toNativeUtf8();
    final p = recall(h, kp, 0);
    calloc.free(kp);
    if (p == nullptr) return '<null>';
    final s = p.toDartString();
    free(p);
    return s;
  }

  final kp = 'alpha beta gamma'.toNativeUtf8();
  final vp = 'the value to remember'.toNativeUtf8();
  final id = store(h, kp, vp);
  calloc.free(kp);
  calloc.free(vp);
  print('stored id=$id');

  var pass = true;
  void check(String label, bool cond, String got) {
    print('${cond ? "ok  " : "FAIL"}  $label  ->  $got');
    if (!cond) pass = false;
  }

  // 1. new symbol returns the current keys
  final k0 = keysOf(id);
  final set0 = k0.split(RegExp(r'\s+')).where((t) => t.isNotEmpty).toSet();
  check('keysOf after store == {alpha,beta,gamma}',
      set0.containsAll({'alpha', 'beta', 'gamma'}) && set0.length == 3, k0);

  // 2. exact delta the UI computes: remove beta, add delta -> "-beta delta"
  final original = ['alpha', 'beta', 'gamma'];
  final finalTags = ['alpha', 'gamma', 'delta'];
  final delta = <String>[
    for (final t in original) if (!finalTags.contains(t)) '-$t',
    for (final t in finalTags) if (!original.contains(t)) t,
  ].join(' ');
  check('computed delta == "-beta delta"', delta == '-beta delta', delta);
  final dpp = delta.toNativeUtf8();
  update(h, id, dpp);
  calloc.free(dpp);

  // 3. keys reflect the edit: alpha,gamma,delta present; beta gone
  final k1 = keysOf(id);
  final set1 = k1.split(RegExp(r'\s+')).where((t) => t.isNotEmpty).toSet();
  check('keysOf after update == {alpha,gamma,delta}',
      set1.containsAll({'alpha', 'gamma', 'delta'}) &&
          !set1.contains('beta') &&
          set1.length == 3,
      k1);

  // 4. recall proves the store agrees: found under delta, not under beta
  check('recall("delta") finds the record',
      recallOf('delta').contains('the value to remember'), recallOf('delta'));
  check('recall("beta") no longer finds it (detached)',
      !recallOf('beta').contains('the value to remember'),
      recallOf('beta').isEmpty ? '<empty>' : recallOf('beta'));

  dir.deleteSync(recursive: true);
  print(pass ? '\nALL PASS' : '\nSOME FAILED');
  exit(pass ? 0 : 1);
}
