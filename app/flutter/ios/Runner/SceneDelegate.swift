import Flutter
import UIKit

class SceneDelegate: FlutterSceneDelegate {
  // Scan-to-join: iOS opens an ais://sync?host=..&token=.. link (a QR the host
  // device showed) and routes it to the scene; forward it to Dart over the same
  // 'ais/deeplink' channel the Android side uses. No in-app QR scanner.
  // (iOS is deferred to TestFlight, so this path is unverified on device.)
  override func scene(
    _ scene: UIScene,
    openURLContexts URLContexts: Set<UIOpenURLContext>
  ) {
    super.scene(scene, openURLContexts: URLContexts)
    if let url = URLContexts.first(where: { $0.url.scheme == "ais" })?.url {
      forwardLink(url)
    }
  }

  private func forwardLink(_ url: URL) {
    guard let controller = window?.rootViewController as? FlutterViewController else { return }
    FlutterMethodChannel(name: "ais/deeplink", binaryMessenger: controller.binaryMessenger)
      .invokeMethod("onLink", arguments: url.absoluteString)
  }
}
