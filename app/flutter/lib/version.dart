// version.dart -- the app's own version, fixed at BUILD time. One source of
// truth, the git tag, so the store listing and the screen cannot disagree.
//
// tool/version.sh derives both numbers from `git describe` and passes them to
// `flutter build` twice: as --build-name/--build-number (the platform metadata
// Play and the App Store read) and as the --dart-define pair below (what the
// About screen reads). The defaults mirror pubspec.yaml's `version:`, so a build
// with no defines still shows something sane; keep them in step with pubspec.
const String kAppVersion =
    String.fromEnvironment('APP_VERSION', defaultValue: '0.3.18');
const String kAppBuild = String.fromEnvironment('APP_BUILD', defaultValue: '18');

/// The standard `1.2.3 (456)` display form.
String get appVersionLabel => '$kAppVersion ($kAppBuild)';
