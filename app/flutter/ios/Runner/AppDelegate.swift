import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }

  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)
    registerBackupChannel(engineBridge)
  }

  // Nothing goes to any cloud. On iOS the index lives under Documents/, which iOS
  // backs up to iCloud BY DEFAULT -- the same leak Android's allowBackup is, only
  // the other way round. NSURLIsExcludedFromBackupKey is the only supported way to
  // opt a directory out, and it is native-only, so Dart asks for it over this
  // channel right after it creates the index dir (the flag needs the item to exist).
  // Android never calls this; see AndroidManifest.xml for its half.
  private func registerBackupChannel(_ engineBridge: FlutterImplicitEngineBridge) {
    guard let messenger = engineBridge.pluginRegistry
      .registrar(forPlugin: "AisBackup")?.messenger() else { return }
    FlutterMethodChannel(name: "ais/backup", binaryMessenger: messenger)
      .setMethodCallHandler { call, result in
        guard call.method == "excludeFromBackup", let path = call.arguments as? String else {
          result(FlutterMethodNotImplemented)
          return
        }
        var url = URL(fileURLWithPath: path)
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        do {
          try url.setResourceValues(values)
          result(true)
        } catch {
          result(false)
        }
      }
  }
}
