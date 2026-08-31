package com.aisindex.ais

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import android.view.WindowManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.File

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
    private var pendingTree: MethodChannel.Result? = null

    companion object {
        private const val OPEN_TREE = 7431
    }

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

        // Uninstall survival: the index dies with the app's private dir, so a
        // bundle copy is kept in a user-chosen SAF folder. Scoped storage means
        // Dart's C engine cannot POSIX-open a content:// tree; the copy is
        // streamed here instead. The tree permission is persisted, so refreshes
        // need no re-pick; an uninstall revokes it, which is fine -- restoring
        // re-picks the folder anyway. iOS has no handler for these yet.
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "ais/backup")
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "pickBackupTree" -> {
                        if (pendingTree != null) {
                            result.error("busy", "a folder picker is already open", null)
                            return@setMethodCallHandler
                        }
                        pendingTree = result
                        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or
                                Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
                        try {
                            startActivityForResult(intent, OPEN_TREE)
                        } catch (e: Exception) {
                            pendingTree = null
                            result.error("picker", e.toString(), null)
                        }
                    }
                    "exportToTree" -> {
                        val tree = call.argument<String>("tree")
                        val src = call.argument<String>("src")
                        val name = call.argument<String>("name")
                        if (tree == null || src == null || name == null) {
                            result.success(false); return@setMethodCallHandler
                        }
                        // Off the UI thread: a provider can be slow (SD card, cloud).
                        Thread {
                            val ok = try { copyIntoTree(Uri.parse(tree), File(src), name) }
                                     catch (_: Exception) { false }
                            runOnUiThread { result.success(ok) }
                        }.start()
                    }
                    "importFromTree" -> {
                        val tree = call.argument<String>("tree")
                        val dest = call.argument<String>("dest")
                        val name = call.argument<String>("name")
                        if (tree == null || dest == null || name == null) {
                            result.success(false); return@setMethodCallHandler
                        }
                        Thread {
                            val ok = try { copyOutOfTree(Uri.parse(tree), File(dest), name) }
                                     catch (_: Exception) { false }
                            runOnUiThread { result.success(ok) }
                        }.start()
                    }
                    else -> result.notImplemented()
                }
            }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode == OPEN_TREE) {
            val res = pendingTree
            pendingTree = null
            val uri = if (resultCode == Activity.RESULT_OK) data?.data else null
            if (uri != null) {
                try {
                    contentResolver.takePersistableUriPermission(uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION or
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                } catch (_: Exception) {}
                res?.success(uri.toString())
            } else {
                res?.success(null) // cancelled
            }
            return
        }
        super.onActivityResult(requestCode, resultCode, data)
    }

    // The bundle document in TREE: the exact NAME if present; for a restore,
    // the newest *.aisb otherwise, so a dated manual export also works.
    private fun findBundle(tree: Uri, name: String, anyAisb: Boolean): Uri? {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(
            tree, DocumentsContract.getTreeDocumentId(tree))
        var bestId: String? = null
        var bestTs = -1L
        contentResolver.query(children, arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED), null, null, null)?.use { c ->
            while (c.moveToNext()) {
                val id = c.getString(0) ?: continue
                val n = c.getString(1) ?: continue
                if (n == name) return DocumentsContract.buildDocumentUriUsingTree(tree, id)
                if (anyAisb && n.endsWith(".aisb") && c.getLong(2) > bestTs) {
                    bestTs = c.getLong(2)
                    bestId = id
                }
            }
        }
        return bestId?.let { DocumentsContract.buildDocumentUriUsingTree(tree, it) }
    }

    private fun copyIntoTree(tree: Uri, src: File, name: String): Boolean {
        val dest = findBundle(tree, name, anyAisb = false)
            ?: DocumentsContract.createDocument(contentResolver,
                DocumentsContract.buildDocumentUriUsingTree(
                    tree, DocumentsContract.getTreeDocumentId(tree)),
                "application/octet-stream", name)
            ?: return false
        val out = contentResolver.openOutputStream(dest, "wt") ?: return false
        out.use { o -> src.inputStream().use { it.copyTo(o) } }
        return true
    }

    private fun copyOutOfTree(tree: Uri, dest: File, name: String): Boolean {
        val doc = findBundle(tree, name, anyAisb = true) ?: return false
        val input = contentResolver.openInputStream(doc) ?: return false
        input.use { i -> dest.outputStream().use { i.copyTo(it) } }
        return true
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
