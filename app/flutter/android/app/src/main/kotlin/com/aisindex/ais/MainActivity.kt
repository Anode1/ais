package com.aisindex.ais

import android.content.Intent
import android.net.Uri
import android.view.WindowManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

// Scan-to-join: the phone's camera opens an ais://sync?host=..&token=.. link
// (shown as a QR by the host device); Android routes it here. We hand it to Dart
// over a MethodChannel -- no in-app QR scanner. A link that cold-started the app
// waits for getInitialLink; one that arrives while running is pushed as onLink.
class MainActivity : FlutterActivity() {
    private var channel: MethodChannel? = null
    private var share: MethodChannel? = null
    private var screen: MethodChannel? = null
    private var initialLink: String? = null
    private var initialShared: String? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        initialLink = linkOf(intent)
        channel = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "ais/deeplink")
        channel!!.setMethodCallHandler { call, result ->
            if (call.method == "getInitialLink") {
                result.success(initialLink)
                initialLink = null
            } else {
                result.notImplemented()
            }
        }

        // Share-sheet intake (ACTION_SEND, text/plain): same shape as ais/deeplink.
        // A share that cold-started the app waits for getInitialShared; one that
        // arrives while running is pushed as onShared. Dart prefills the Add sheet.
        initialShared = sharedTextOf(intent)
        share = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "ais/share")
        share!!.setMethodCallHandler { call, result ->
            if (call.method == "getInitialShared") {
                result.success(initialShared)
                initialShared = null
            } else {
                result.notImplemented()
            }
        }

        // Keep the screen on while hosting a sync. The host shows a QR and waits
        // up to two minutes for the other phone to scan it, and the usual screen
        // timeout is well under that -- so the code the user is holding a camera
        // up to went black halfway through, which reads as the feature being
        // broken. A window flag, not a wakelock: it needs no permission, it only
        // applies while this window is in front, and it dies with the window.
        screen = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "ais/screen")
        screen!!.setMethodCallHandler { call, result ->
            if (call.method == "keepAwake") {
                val on = call.arguments as? Boolean ?: false
                runOnUiThread {
                    if (on) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
                result.success(true)
            } else {
                result.notImplemented()
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        linkOf(intent)?.let { channel?.invokeMethod("onLink", it) }
        sharedTextOf(intent)?.let { share?.invokeMethod("onShared", it) }
    }

    private fun linkOf(intent: Intent?): String? {
        val data: Uri? = intent?.data
        return if (intent?.action == Intent.ACTION_VIEW && data?.scheme == "ais") data.toString() else null
    }

    // Only EXTRA_TEXT crosses: EXTRA_SUBJECT is deliberately dropped, so a
    // shared title is never invented into tags -- tags stay the user's.
    private fun sharedTextOf(intent: Intent?): String? {
        if (intent?.action != Intent.ACTION_SEND || intent.type != "text/plain") return null
        return intent.getStringExtra(Intent.EXTRA_TEXT)?.takeIf { it.isNotBlank() }
    }
}
