import Flutter
import UIKit

// Scan-to-join: iOS opens an ais://sync?host=..&token=.. link (a QR the host
// device showed) and routes it to the scene; forward it to Dart over the same
// 'ais/deeplink' channel the Android side uses. No in-app QR scanner.
//
// Two arrival paths, and BOTH are needed. A link that arrives while the app runs
// comes through scene(_:openURLContexts:). A link that COLD-STARTS the app --
// which is the ordinary case, since the user is pointing a camera at a QR, not
// switching back to an app already open -- is delivered in the connection options
// instead, and is simply lost if only the first is implemented. Dart asks for it
// with getInitialLink (see _wireDeepLinks); nothing answered that call before, so
// it fell through to the silent catch and the scan did nothing at all.
//
// (iOS is deferred: the engine is not yet linked into the Runner target, see
// ios/TODO.md, so this path is written to the Android contract but unverified on
// a device.)
class SceneDelegate: FlutterSceneDelegate {
  private var initialLink: String?

  override func scene(
    _ scene: UIScene,
    willConnectTo session: UISceneSession,
    options connectionOptions: UIScene.ConnectionOptions
  ) {
    // Held, not forwarded: the Flutter view controller and its channel do not
    // exist yet at connect time. Dart pulls it once it is ready.
    initialLink = connectionOptions.urlContexts
      .first(where: { $0.url.scheme == "ais" })?.url.absoluteString
    super.scene(scene, willConnectTo: session, options: connectionOptions)
    installChannel()
  }

  override func scene(
    _ scene: UIScene,
    openURLContexts URLContexts: Set<UIOpenURLContext>
  ) {
    super.scene(scene, openURLContexts: URLContexts)
    if let url = URLContexts.first(where: { $0.url.scheme == "ais" })?.url {
      forwardLink(url)
    }
  }

  private func channel() -> FlutterMethodChannel? {
    guard let controller = window?.rootViewController as? FlutterViewController
    else { return nil }
    return FlutterMethodChannel(
      name: "ais/deeplink", binaryMessenger: controller.binaryMessenger)
  }

  // Answer getInitialLink exactly once, like MainActivity.kt: the link is consumed
  // by the first ask so a later rebuild cannot replay a sync the user already did.
  private func installChannel() {
    channel()?.setMethodCallHandler { [weak self] call, result in
      if call.method == "getInitialLink" {
        result(self?.initialLink)
        self?.initialLink = nil
      } else {
        result(FlutterMethodNotImplemented)
      }
    }
  }

  private func forwardLink(_ url: URL) {
    channel()?.invokeMethod("onLink", arguments: url.absoluteString)
  }
}
