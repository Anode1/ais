// Round-trip harness for the plaintext file bundle. dlopens libais.so directly.
// Run from app/flutter with LD_LIBRARY_PATH pointing at the built ais_engine dir.
// Export index A to a file, import into empty B, assert A's records appear in B.
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

typedef _OpenN = Pointer<Void> Function(Pointer<Utf8>);
typedef _StoreN = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _BundleN = Int32 Function(Pointer<Void>, Pointer<Utf8>);
typedef _BundleD = int Function(Pointer<Void>, Pointer<Utf8>);
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
  final bundle = '${Directory.systemTemp.path}/verify_bundle.bin';

  var pass = true;
  void check(String label, bool cond, String got) {
    print('${cond ? "ok  " : "FAIL"}  $label  ->  $got');
    if (!cond) pass = false;
  }

  final ha = openIdx(a.path);
  put(ha, 'venice italy', 'https://example.org/venice');
  put(ha, 'recipe pasta', 'boil then toss');

  final e = export_(ha, bundle.toNativeUtf8());
  check('export_bundle returns 0', e == 0, '$e');
  check('bundle file exists and is non-empty',
      File(bundle).existsSync() && File(bundle).lengthSync() > 0,
      File(bundle).existsSync() ? '${File(bundle).lengthSync()} bytes' : 'missing');

  final hb = openIdx(b.path);
  final i = import_(hb, bundle.toNativeUtf8());
  check('import_bundle returns 0', i == 0, '$i');
  check('imported record found under "venice"',
      rec(hb, 'venice').contains('example.org/venice'), rec(hb, 'venice'));
  check('imported record found under "pasta"',
      rec(hb, 'pasta').contains('boil then toss'), rec(hb, 'pasta'));

  a.deleteSync(recursive: true);
  b.deleteSync(recursive: true);
  if (File(bundle).existsSync()) File(bundle).deleteSync();
  print(pass ? '\nALL PASS' : '\nSOME FAILED');
  exit(pass ? 0 : 1);
}
