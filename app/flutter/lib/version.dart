// version.dart -- the app's own version, fixed at BUILD time.
//
// tool/version.sh derives both numbers from `git describe` and passes them to
// `flutter build` twice: as --build-name/--build-number (the platform metadata
// Play and the App Store read) and as the --dart-define pair below (what the
// About screen reads). One source of truth, the git tag, so the store listing
// and the screen cannot disagree.
//
// The defaults mirror pubspec.yaml's `version:` so a plain `flutter build`,
// `flutter run` or `dart analyze` with no defines still shows something sane --
// they are a fallback, not the source of truth. Keep them in step with pubspec
// when the tag moves; nothing else reads them.
const String kAppVersion =
    String.fromEnvironment('APP_VERSION', defaultValue: '0.3.9');
const String kAppBuild = String.fromEnvironment('APP_BUILD', defaultValue: '9');

/// The standard `1.2.3 (456)` display form.
String get appVersionLabel => '$kAppVersion ($kAppBuild)';
