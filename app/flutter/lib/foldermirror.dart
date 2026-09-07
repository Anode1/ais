// foldermirror.dart -- folder sync on Android, where the shared folder is a
// content:// tree the C engine cannot opendir(). The tree is mirrored into a
// private directory, the engine runs its ordinary folder pass there, and the
// engine's own bundle is streamed back out. Pure Dart (no engine, no platform
// channel) so `flutter test` drives it; MainActivity does the streaming.
import 'dart:io';

/// A device bundle is exactly 16 lowercase hex chars + ".aisb" (c/sync.c).
/// Anything else in the folder (a kept copy, a Syncthing conflict file) is
/// neither mirrored nor pruned.
bool isDeviceBundleName(String name) =>
    RegExp(r'^[0-9a-f]{16}\.aisb$').hasMatch(name);

/// True for a SAF tree (the picker's result on Android) as opposed to a path.
bool isTreeUri(String folder) => folder.startsWith('content://');

/// What to show for a remembered folder: the human tail of a tree URI
/// ("Documents/ais" for primary:Documents/ais), the path itself otherwise.
String folderLabel(String folder) {
  if (!isTreeUri(folder)) return folder;
  final tail = folder.substring(folder.lastIndexOf('/') + 1);
  final id = Uri.decodeComponent(tail);
  final colon = id.indexOf(':');
  final rel = colon < 0 ? id : id.substring(colon + 1);
  return rel.isEmpty ? 'Storage' : rel;
}

/// This device's bundle name, from the engine's <index>/syncid ("id nonce seq").
/// Read AFTER a pass: a clone heal writes a fresh id during one. Null when the
/// file is absent or malformed (the engine has never run a folder pass here).
String? ownBundleName(String indexDir) {
  try {
    final line = File('$indexDir/syncid').readAsStringSync().trim();
    final id = line.split(RegExp(r'\s+')).first;
    if (RegExp(r'^[0-9a-f]{16}$').hasMatch(id)) return '$id.aisb';
  } catch (_) {}
  return null;
}

/// Bring [mirror] in line with what the tree holds: every device bundle not in
/// [present] is deleted, so a bundle removed from the share (or a whole share
/// emptied) is seen by the engine exactly as it would be on a real directory.
/// Files that are not device bundles are left alone. Creates the directory.
void pruneMirror(Directory mirror, Set<String> present) {
  mirror.createSync(recursive: true);
  for (final e in mirror.listSync()) {
    final name = e.uri.pathSegments.where((s) => s.isNotEmpty).last;
    if (e is File && isDeviceBundleName(name) && !present.contains(name)) {
      try {
        e.deleteSync();
      } catch (_) {}
    }
  }
}
