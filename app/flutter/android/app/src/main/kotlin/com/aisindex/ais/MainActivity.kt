package com.aisindex.ais

import android.content.Intent
import android.net.Uri
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

// Scan-to-join: the phone's camera opens an ais://sync?host=..&token=.. link
// (shown as a QR by the host device); Android routes it here. We hand it to Dart
// over a MethodChannel -- no in-app QR scanner. A link that cold-started the app
// waits for getInitialLink; one that arrives while running is pushed as onLink.
class MainActivity : FlutterActivity() {
    private var channel: MethodChannel? = null
    private var initialLink: String? = null

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
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        linkOf(intent)?.let { channel?.invokeMethod("onLink", it) }
    }

    private fun linkOf(intent: Intent?): String? {
        val data: Uri? = intent?.data
        return if (intent?.action == Intent.ACTION_VIEW && data?.scheme == "ais") data.toString() else null
    }
}
