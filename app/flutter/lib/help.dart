// help.dart -- the Help page: what the app is for and how to use it, for
// someone who arrived from the store listing and never saw the README.
// Static text, no engine. Every control it names ('Match any tag', 'Keep a
// copy in a folder', ...) is a label in main.dart: when one is renamed, the
// text here moves with it.
import 'package:flutter/material.dart';

const kSourceUrl = 'https://github.com/Anode1/ais';
const kPrivacyUrl = 'https://github.com/Anode1/ais/blob/main/PRIVACY.md';
const kIssuesUrl = 'https://github.com/Anode1/ais/issues';

// (title, body). "Keep a copy" names the Android control; iOS gets its own
// line when that app ships (it has Export to a file only).
const helpSections = <(String, String)>[
  (
    'What AIS is',
    'An index of your own things. You save a link, a note, a file path or a '
        'password under tags you choose, and find it later by those tags. '
        'Everything stays on this phone as plain text. No account, no cloud, '
        'no ads.'
  ),
  (
    'Save',
    'Tap Add. Type what to remember, and the tags you would think of later, '
        'separated by spaces. Example: tags "wifi guest", value the password. '
        'To save from another app, use its Share button and choose AIS.'
  ),
  (
    'Find',
    'Type one or more tags. Two tags give what was saved under both. '
        'Match any tag gives what was saved under either. The microphone '
        'takes the tags by voice. If no tag matches, Search note text instead '
        'looks inside the saved text. Recent lists everything by date, Tags '
        'lists every tag you have used.'
  ),
  (
    'Passwords',
    'Turn on Encrypt in the Add sheet and set a passphrase. The value is '
        'stored encrypted and shown only after you type the passphrase. '
        'A lost passphrase cannot be recovered.'
  ),
  (
    'Keep a copy',
    "The index lives in this app's private storage and is removed if the app "
        'is uninstalled. Menu, Sync & backup, Keep a copy in a folder '
        'refreshes a copy after every change. Restore from a folder on the '
        'empty start screen brings it back.'
  ),
  (
    'Other devices',
    'Menu, Sync & backup. Host a sync on one device shows a code, Join on '
        'the other scans it. The transfer goes directly between the two '
        'devices on the same Wi-Fi, encrypted. A sync folder (Syncthing, a '
        'cloud drive) works too. The same index opens on Linux and macOS, in '
        'a terminal or a browser.'
  ),
];

class HelpPage extends StatelessWidget {
  // Injected so the page needs no plugin: the app passes its own URL opener.
  final void Function(String url) open;
  const HelpPage({super.key, required this.open});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final tt = Theme.of(context).textTheme;
    return Scaffold(
      appBar: AppBar(title: const Text('Help')),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
          children: [
            for (final (title, body) in helpSections) ...[
              const SizedBox(height: 16),
              Text(title, style: tt.titleMedium),
              const SizedBox(height: 4),
              Text(body,
                  style: tt.bodyLarge?.copyWith(color: cs.onSurfaceVariant)),
            ],
            const SizedBox(height: 16),
            const Divider(),
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.bug_report_outlined),
              title: const Text('Report a problem'),
              onTap: () => open(kIssuesUrl),
            ),
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.code),
              title: const Text('Source code and documentation'),
              onTap: () => open(kSourceUrl),
            ),
            ListTile(
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.privacy_tip_outlined),
              title: const Text('Privacy policy'),
              onTap: () => open(kPrivacyUrl),
            ),
          ],
        ),
      ),
    );
  }
}
