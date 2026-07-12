// Fairness harness for the file-sync A/B task. Runs against EITHER arm's build.
// dlopens libais.so directly (no app bindings) — copy into <arm>/app/flutter/tool
// and run from that app dir with LD_LIBRARY_PATH pointing at the arm's ais_engine.
// Export from index A, import into empty B under the right secret (records must
// appear), then import under a WRONG secret (must be rejected, records absent).
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

typedef _OpenN = Pointer<Void> Function(Pointer<Utf8>);
typedef _StoreN = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _BundleN = Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _BundleD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _RecallN = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32);
typedef _RecallD = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int);
typedef _FreeN = Void Function(Pointer<Utf8>);
typedef _FreeD = void Function(Pointer<Utf8>);

void main() {
  final lib = DynamicLibrary.open('libais.so');
  final open = lib.lookupFunction<_OpenN, _OpenN>('ais_embed_open');
  final store = lib.lookupFunction<_StoreN, _StoreD>('ais_embed_store');
  final export_ =
      lib.lookupFunction<_BundleN, _BundleD>('ais_embed_export_bundle');
  final import_ =
      lib.lookupFunction<_BundleN, _BundleD>('ais_embed_import_bundle');
  final recall = lib.lookupFunction<_RecallN, _RecallD>('ais_embed_recall');
  final free = lib.lookupFunction<_FreeN, _FreeD>('ais_embed_free');

  Pointer<Void> openIdx(String p) => open(p.toNativeUtf8());
  void put(Pointer<Void> h, String k, String v) =>
      store(h, k.toNativeUtf8(), v.toNativeUtf8());
  String rec(Pointer<Void> h, String k) {
    final p = recall(h, k.toNativeUtf8(), 0);
    if (p == nullptr) return '';
    final s = p.toDartString();
    free(p);
    return s;
  }

  final a = Directory.systemTemp.createTempSync('bundle_A_');
  final b = Directory.systemTemp.createTempSync('bundle_B_');
  final c = Directory.systemTemp.createTempSync('bundle_C_');
  final bundle = '${Directory.systemTemp.path}/verify_bundle.bin';
  const secret = 'correct horse';
  const wrong = 'battery staple';

  var pass = true;
  void check(String label, bool cond, String got) {
    print('${cond ? "ok  " : "FAIL"}  $label  ->  $got');
    if (!cond) pass = false;
  }

  final ha = openIdx(a.path);
  put(ha, 'venice italy', 'https://example.org/venice');
  put(ha, 'recipe pasta', 'boil then toss');

  final e = export_(ha, bundle.toNativeUtf8(), secret.toNativeUtf8());
  check('export_bundle returns 0', e == 0, '$e');
  check('bundle file exists and is non-empty',
      File(bundle).existsSync() && File(bundle).lengthSync() > 0,
      File(bundle).existsSync() ? '${File(bundle).lengthSync()} bytes' : 'missing');

  final hb = openIdx(b.path);
  final i = import_(hb, bundle.toNativeUtf8(), secret.toNativeUtf8());
  check('import_bundle (right secret) returns 0', i == 0, '$i');
  check('imported record found under "venice"',
      rec(hb, 'venice').contains('example.org/venice'), rec(hb, 'venice'));
  check('imported record found under "pasta"',
      rec(hb, 'pasta').contains('boil then toss'), rec(hb, 'pasta'));

  final hc = openIdx(c.path);
  final w = import_(hc, bundle.toNativeUtf8(), wrong.toNativeUtf8());
  check('import_bundle (WRONG secret) returns non-zero', w != 0, '$w');
  check('wrong-secret import left B-record absent',
      !rec(hc, 'venice').contains('example.org'), rec(hc, 'venice').isEmpty ? '<empty>' : rec(hc, 'venice'));

  a.deleteSync(recursive: true);
  b.deleteSync(recursive: true);
  c.deleteSync(recursive: true);
  if (File(bundle).existsSync()) File(bundle).deleteSync();
  print(pass ? '\nALL PASS' : '\nSOME FAILED');
  exit(pass ? 0 : 1);
}
