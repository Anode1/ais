// Round-trip for folder auto-sync via the SAME FFI symbol the app calls
// (ais_embed_sync_folder). dlopens libais.so directly. Two indexes A and B sync
// through a shared folder and must converge. Run from app/flutter with
// LD_LIBRARY_PATH pointing at the built ais_engine dir.
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

typedef _OpenN = Pointer<Void> Function(Pointer<Utf8>);
typedef _StoreN = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _FldN = Int32 Function(Pointer<Void>, Pointer<Utf8>);
typedef _FldD = int Function(Pointer<Void>, Pointer<Utf8>);
typedef _RecallN = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32);
typedef _RecallD = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int);
typedef _FreeN = Void Function(Pointer<Utf8>);
typedef _FreeD = void Function(Pointer<Utf8>);

void main() {
  final lib = DynamicLibrary.open('libais.so');
  final open = lib.lookupFunction<_OpenN, _OpenN>('ais_embed_open');
  final store = lib.lookupFunction<_StoreN, _StoreD>('ais_embed_store');
  final sync = lib.lookupFunction<_FldN, _FldD>('ais_embed_sync_folder');
  final recall = lib.lookupFunction<_RecallN, _RecallD>('ais_embed_recall');
  final free = lib.lookupFunction<_FreeN, _FreeD>('ais_embed_free');

  Pointer<Void> openIdx(String p) => open(p.toNativeUtf8());
  void put(Pointer<Void> h, String k, String v) =>
      store(h, k.toNativeUtf8(), v.toNativeUtf8());
  int syncFld(Pointer<Void> h, String f) => sync(h, f.toNativeUtf8());
  String rec(Pointer<Void> h, String k) {
    final p = recall(h, k.toNativeUtf8(), 0);
    if (p == nullptr) return '';
    final s = p.toDartString();
    free(p);
    return s;
  }

  final a = Directory.systemTemp.createTempSync('sfA_');
  final b = Directory.systemTemp.createTempSync('sfB_');
  final fld = Directory.systemTemp.createTempSync('sfShared_');

  var pass = true;
  void check(String label, bool cond, String got) {
    print('${cond ? "ok  " : "FAIL"}  $label  ->  $got');
    if (!cond) pass = false;
  }

  final ha = openIdx(a.path);
  final hb = openIdx(b.path);
  put(ha, 'alpha', 'note from A');
  put(hb, 'beta', 'note from B');

  check('A sync pass returns 0', syncFld(ha, fld.path) == 0, 'ok');
  check('B sync pass returns 0', syncFld(hb, fld.path) == 0, 'ok');
  check('A second pass returns 0', syncFld(ha, fld.path) == 0, 'ok');

  check('A now has B\'s record', rec(ha, 'beta').contains('note from B'), rec(ha, 'beta'));
  check('B now has A\'s record', rec(hb, 'alpha').contains('note from A'), rec(hb, 'alpha'));

  a.deleteSync(recursive: true);
  b.deleteSync(recursive: true);
  fld.deleteSync(recursive: true);
  print(pass ? '\nALL PASS' : '\nSOME FAILED');
  exit(pass ? 0 : 1);
}
