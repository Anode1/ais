// Folder sync on Android goes through a private mirror of the SAF tree
// (foldermirror.dart). These pin the pure parts: which names count as device
// bundles, what a remembered tree is called on screen, where our own bundle's
// name comes from, and that pruning tracks the tree without touching anything else.
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:ais/foldermirror.dart';

void main() {
  late Directory tmp;
  setUp(() => tmp = Directory.systemTemp.createTempSync('ais_mirror'));
  tearDown(() => tmp.deleteSync(recursive: true));

  test('device bundle names are 16 hex + .aisb, nothing else', () {
    expect(isDeviceBundleName('0123456789abcdef.aisb'), isTrue);
    expect(isDeviceBundleName('0123456789ABCDEF.aisb'), isFalse);
    expect(isDeviceBundleName('ais-backup.aisb'), isFalse);
    expect(isDeviceBundleName('0123456789abcdef.aisb.sync-conflict'), isFalse);
    expect(isDeviceBundleName('0123456789abcde.aisb'), isFalse);
  });

  test('a tree URI is labelled by its folder, a path by itself', () {
    expect(
        folderLabel('content://com.android.externalstorage.documents/tree/'
            'primary%3ADocuments%2Fais'),
        'Documents/ais');
    expect(
        folderLabel('content://com.android.externalstorage.documents/tree/'
            'primary%3A'),
        'Storage');
    expect(folderLabel('/home/vas/share'), '/home/vas/share');
    expect(isTreeUri('content://x/tree/y'), isTrue);
    expect(isTreeUri('/mnt/share'), isFalse);
  });

  test('own bundle name comes from syncid, absent or malformed is null', () {
    expect(ownBundleName(tmp.path), isNull);
    File('${tmp.path}/syncid').writeAsStringSync(
        'a1b2c3d4e5f60718 00112233445566778899aabbccddeeff 7\n');
    expect(ownBundleName(tmp.path), 'a1b2c3d4e5f60718.aisb');
    File('${tmp.path}/syncid').writeAsStringSync('garbage\n');
    expect(ownBundleName(tmp.path), isNull);
  });

  test('prune drops device bundles the tree no longer has, keeps the rest', () {
    final m = Directory('${tmp.path}/mirror');
    pruneMirror(m, {}); // creates it
    expect(m.existsSync(), isTrue);
    File('${m.path}/0000000000000001.aisb').writeAsStringSync('a');
    File('${m.path}/0000000000000002.aisb').writeAsStringSync('b');
    File('${m.path}/ais-backup.aisb').writeAsStringSync('c');
    File('${m.path}/notes.txt').writeAsStringSync('d');
    pruneMirror(m, {'0000000000000002.aisb'});
    final left = m.listSync().map((e) => e.path.split('/').last).toSet();
    expect(left, {'0000000000000002.aisb', 'ais-backup.aisb', 'notes.txt'});
  });
}
