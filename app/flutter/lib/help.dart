// help.dart -- the Help page: what the app is for and how to use it, for
// someone who arrived from the store listing and never saw the README.
// Static text, no engine. Every control it names ('Any tag', 'Keep a
// copy in a folder', ...) is a label or tooltip in main.dart: when one is renamed, the
// text here moves with it.
import 'package:flutter/material.dart';

const kSourceUrl = 'https://github.com/Anode1/ais';
const kPrivacyUrl = 'https://github.com/Anode1/ais/blob/main/PRIVACY.md';
const kIssuesUrl = 'https://github.com/Anode1/ais/issues';

// (title, body), the Android text. iOS differs where the app does: no share
// intake, no folder picker (Export / Import to a file instead), no sync folder.
const _whatAis = (
  'What AIS is',
  'Save things you want to find again. You save a link, a note, a file path or '
      'a password under tags you choose, and find it later by those tags. '
      'Everything stays on this phone as plain text. No account, no cloud, '
      'no ads.'
);

const _find = (
  'Find',
  'Type one or more tags. Two tags find what has both.\n\n'
      'The Any tag chip finds what has either. The microphone takes tags by voice.\n\n'
      'Recent lists everything by date. Tags lists every tag you used.'
);

const _secrets = (
  'Secrets',
  'Turn on Lock with a password in the Add sheet and set a password. The text '
      'is stored encrypted and shown only after you type the password. '
      'Forget this password and the note is gone for good.'
);

const helpSections = <(String, String)>[
  _whatAis,
  (
    'Save',
    'Tap Add. Type the text, then the tags you would think of later, '
        'separated by spaces. Example: tags "wifi guest", text the password. '
        'To save from another app, use its Share button and choose AIS.'
  ),
  _find,
  _secrets,
  (
    'Keep a copy',
    'Uninstalling AIS deletes your notes. Menu, Sync & backup, Keep a copy in '
        'a folder refreshes a copy after every change. Restore from a folder '
        'on the empty start screen brings it back.'
  ),
  (
    'Other devices',
    'Menu, Sync & backup. Show a code on one device; on the other, point the '
        'camera at it and tap the link, or choose Scan or type a code and type '
        'what is under the code. The transfer goes directly between the two '
        'devices on the same Wi-Fi, encrypted. A sync folder (Syncthing, a '
        'cloud drive) works too.'
  ),
  (
    'Links from another app',
    "Menu, Import bookmarks takes a browser's bookmarks file, or the CSV that "
        'Pocket, Instapaper or Raindrop export. Each link is filed under its own '
        'tags and folder words plus one tag naming the source (pocket, bookmark), '
        'so the batch is found as one. Undo removes what the import added.'
  ),
];

const helpSectionsIos = <(String, String)>[
  _whatAis,
  (
    'Save',
    'Tap Add. Type the text, then the tags you would think of later, '
        'separated by spaces. Example: tags "wifi guest", text the password.'
  ),
  _find,
  _secrets,
  (
    'Keep a copy',
    'Nothing here is in iCloud. Menu, Sync & backup, Export to a file saves a '
        'copy: keep it in Files or send it somewhere safe. Import from a file, '
        'or Import a backup file on the empty start screen, brings it back.'
  ),
  (
    'Other devices',
    'Menu, Sync & backup. Show a code on one device; on the other, point the '
        'camera at it and tap the link, or choose Scan or type a code and type '
        'what is under the code. The transfer goes directly between the two '
        'devices on the same Wi-Fi, encrypted.'
  ),
];

class HelpPage extends StatelessWidget {
  // Injected so the page needs no plugin: the app passes its own URL opener.
  final void Function(String url) open;
  final bool ios;
  const HelpPage({super.key, required this.open, this.ios = false});

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
            for (final (title, body) in (ios ? helpSectionsIos : helpSections)) ...[
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
