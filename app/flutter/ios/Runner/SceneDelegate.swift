import Flutter
import UIKit

// Scan-to-join: iOS opens an ais://sync?host=..&token=.. link (a QR the host
// device showed) and routes it to the scene; it reaches Dart over the same
// 'ais/deeplink' channel the Android side uses. No in-app QR scanner.
//
// Two arrival paths, and BOTH are needed. A link that arrives while the app runs
// comes through scene(_:openURLContexts:). A link that COLD-STARTS the app --
// which is the ordinary case, since the user is pointing a camera at a QR, not
// switching back to an app already open -- is delivered in the connection options
// instead, and is simply lost if only the first is implemented. Dart asks for it
// with getInitialLink (see _wireDeepLinks).
//
// The channel is NOT made here. The first device test (2026-10-08) opened the app
// from the camera and nothing happened: at scene-connect time the window's root
// view controller is not reliably the Flutter controller yet, so a channel built
// from it installs no handler, Dart's getInitialLink fails into its silent catch,
// and the scan does nothing. AppDelegate installs the handler from the engine
// bridge's messenger, which exists by then; this file only stores and forwards.
class SceneDelegate: FlutterSceneDelegate {
  override func scene(
    _ scene: UIScene,
    willConnectTo session: UISceneSession,
    options connectionOptions: UIScene.ConnectionOptions
  ) {
    if let url = connectionOptions.urlContexts.first(where: { $0.url.scheme == "ais" })?.url {
      DeepLink.arrived(url)
    }
    super.scene(scene, willConnectTo: session, options: connectionOptions)
  }

  override func scene(
    _ scene: UIScene,
    openURLContexts URLContexts: Set<UIOpenURLContext>
  ) {
    super.scene(scene, openURLContexts: URLContexts)
    if let url = URLContexts.first(where: { $0.url.scheme == "ais" })?.url {
      DeepLink.arrived(url)
    }
  }
}

// One pending link and one channel, shared by the scene (which receives links)
// and the app delegate (which owns the engine's messenger). A link that arrives
// before the channel exists waits for getInitialLink; one that arrives after is
// pushed as onLink. getInitialLink is answered exactly once, like MainActivity.kt:
// the link is consumed by the first ask so a later rebuild cannot replay a sync
// the user already did.
enum DeepLink {
  private static var pending: String?
  private static var channel: FlutterMethodChannel?

  static func install(messenger: FlutterBinaryMessenger) {
    let ch = FlutterMethodChannel(name: "ais/deeplink", binaryMessenger: messenger)
    ch.setMethodCallHandler { call, result in
      if call.method == "getInitialLink" {
        result(pending)
        pending = nil
      } else {
        result(FlutterMethodNotImplemented)
      }
    }
    channel = ch
  }

  static func arrived(_ url: URL) {
    if let ch = channel {
      ch.invokeMethod("onLink", arguments: url.absoluteString)
    } else {
      pending = url.absoluteString
    }
  }
}
