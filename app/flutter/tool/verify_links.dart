// Harness for the links import at the FFI seam. dlopens libais.so directly.
// Run from app/flutter with LD_LIBRARY_PATH pointing at a built libais.so.
// Import a Pocket-shaped CSV, assert the records and the id watermark that
// the app's Undo relies on, delete that run, assert the older record stays.
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

typedef _OpenN = Pointer<Void> Function(Pointer<Utf8>);
typedef _StoreN = Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _StoreD = int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _LinksN = Int64 Function(Pointer<Void>, Pointer<Utf8>);
typedef _LinksD = int Function(Pointer<Void>, Pointer<Utf8>);
typedef _IdN = Int64 Function(Pointer<Void>);
typedef _IdD = int Function(Pointer<Void>);
typedef _DelN = Int32 Function(Pointer<Void>, Int64);
typedef _DelD = int Function(Pointer<Void>, int);
typedef _RecallN = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32);
typedef _RecallD = Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, int);
typedef _FreeN = Void Function(Pointer<Utf8>);
typedef _FreeD = void Function(Pointer<Utf8>);

void main() {
  final lib = DynamicLibrary.open('libais.so');
  final open = lib.lookupFunction<_OpenN, _OpenN>('ais_embed_open');
  final store = lib.lookupFunction<_StoreN, _StoreD>('ais_embed_store');
  final links = lib.lookupFunction<_LinksN, _LinksD>('ais_embed_import_links');
  final nextId = lib.lookupFunction<_IdN, _IdD>('ais_embed_next_id');
  final count = lib.lookupFunction<_IdN, _IdD>('ais_embed_count');
  final del = lib.lookupFunction<_DelN, _DelD>('ais_embed_del');
  final recall = lib.lookupFunction<_RecallN, _RecallD>('ais_embed_recall');
  final free = lib.lookupFunction<_FreeN, _FreeD>('ais_embed_free');

  String rec(Pointer<Void> h, String k) {
    final p = recall(h, k.toNativeUtf8(), 0);
    if (p == nullptr) return '';
    final s = p.toDartString();
    free(p);
    return s;
  }

  final dir = Directory.systemTemp.createTempSync('links_');
  final csv = File('${dir.path}/part_000000.csv')
    ..writeAsStringSync('\u{FEFF}title,url,time_added,tags,status\r\n'
        '"Pie, baked",https://pie.example/,1690000000,recipes|baking,unread\r\n'
        'Bread,https://bread.example/,1690000001,baking,archive\r\n'
        ',https://nolink.example/x y,1,,unread\r\n');

  var pass = true;
  void check(String label, bool cond, String got) {
    print('${cond ? "ok  " : "FAIL"}  $label  ->  $got');
    if (!cond) pass = false;
  }

  final h = open(dir.path.toNativeUtf8());
  store(h, 'mine'.toNativeUtf8(), 'https://mine.example/'.toNativeUtf8());
  final before = nextId(h);
  final n = links(h, csv.path.toNativeUtf8());
  final after = nextId(h);
  check('import_links returns the records put', n == 2, '$n');
  check('next_id brackets the two new ids', after - before == 2, '$before..$after');
  check('a link recalls under its tag', rec(h, 'baking').contains('bread.example'), rec(h, 'baking'));
  check('and under the pocket marker', rec(h, 'pocket').contains('pie.example'), rec(h, 'pocket'));
  check('a missing file is -1', links(h, '${dir.path}/none.csv'.toNativeUtf8()) == -1, 'ok');
  for (var id = before; id < after; id++) {
    check('undo deletes id $id', del(h, id) == 0, 'del');
  }
  check('the older record survives the undo', rec(h, 'mine').contains('mine.example'), rec(h, 'mine'));
  check('live count is back to one', count(h) == 1, '${count(h)}');

  dir.deleteSync(recursive: true);
  print(pass ? '\nALL PASS' : '\nSOME FAILED');
  exit(pass ? 0 : 1);
}
