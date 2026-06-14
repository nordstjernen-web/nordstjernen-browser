/* Nordstjernen — Android host activity: URL bar over the engine render surface,
 * with history, reload, link following and rotation relayout. */

package com.nordstjernen.browser

import android.app.role.RoleManager
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "nordstjernen"
    }

    private val ioExecutor = Executors.newSingleThreadExecutor()
    private val loadGen = AtomicInteger(0)
    private val backStack = ArrayDeque<String>()

    private lateinit var urlBar: EditText
    private lateinit var pageView: PageView
    private lateinit var progress: ProgressBar
    private lateinit var banner: TextView
    private lateinit var backButton: Button

    private var initialized = false
    private var currentUrl: String? = null

    private val browserRoleLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {}

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_main)

        // targetSdk 36 enforces edge-to-edge with no opt-out on Android 16+;
        // pad the root view by the system bar / cutout / IME insets so the
        // toolbar and page stay clear of them on every API level.
        val root = findViewById<View>(R.id.root)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()
            )
            val ime = insets.getInsets(WindowInsetsCompat.Type.ime())
            v.setPadding(bars.left, bars.top, bars.right, maxOf(bars.bottom, ime.bottom))
            WindowInsetsCompat.CONSUMED
        }

        urlBar = findViewById(R.id.urlBar)
        pageView = findViewById(R.id.pageView)
        progress = findViewById(R.id.progress)
        banner = findViewById(R.id.banner)
        backButton = findViewById(R.id.backButton)

        findViewById<Button>(R.id.goButton).setOnClickListener { navigate(urlBar.text.toString()) }
        findViewById<Button>(R.id.reloadButton).setOnClickListener { reload() }
        backButton.setOnClickListener { goBack() }
        urlBar.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_GO) { navigate(urlBar.text.toString()); true } else false
        }
        urlBar.setOnClickListener { showUrlKeyboard() }
        urlBar.setOnFocusChangeListener { _, hasFocus ->
            if (hasFocus && initialized) showUrlKeyboard()
        }

        pageView.renderScale = resources.displayMetrics.density.toDouble()
        pageView.onNavigate = { url -> navigate(url) }
        pageView.onLinkLongPress = { url -> showLinkMenu(url) }
        pageView.onViewportWidthChanged = { currentUrl?.let { load(it) } }

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (!goBack()) { isEnabled = false; onBackPressedDispatcher.onBackPressed() }
            }
        })
        updateBackButton()

        if (!NativeBrowser.available) {
            banner.visibility = View.VISIBLE
            banner.text = getString(R.string.engine_unavailable)
            return
        }

        maybeRequestBrowserRole()

        ioExecutor.execute {
            val caBundle = extractCaBundle()
            val rc = NativeBrowser.nativeInit(filesDir.absolutePath, caBundle)
            initialized = rc == 0
            runOnUiThread {
                if (initialized) {
                    navigate(initialUrl())
                } else {
                    banner.visibility = View.VISIBLE
                    banner.text = getString(R.string.engine_init_failed)
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (initialized) navigate(initialUrl())
    }

    private fun initialUrl(): String {
        val data = intent?.data?.toString()
        return if (!data.isNullOrEmpty()) data else getString(R.string.home_url)
    }

    private fun navigate(input: String) {
        if (!initialized) return
        val raw = input.trim()
        if (raw.isEmpty()) return
        val url = normalizeUrl(raw)
        currentUrl?.let { if (it != url) backStack.addLast(it) }
        load(url)
    }

    private fun reload() {
        currentUrl?.let { load(it) }
    }

    private fun goBack(): Boolean {
        if (backStack.isEmpty()) return false
        load(backStack.removeLast())
        return true
    }

    private fun load(url: String) {
        if (!initialized) return
        currentUrl = url
        urlBar.setText(url)
        hideKeyboard()
        updateBackButton()
        progress.visibility = View.VISIBLE

        val gen = loadGen.incrementAndGet()
        // The engine lays out in CSS px; convert the device-px surface width to
        // CSS px (≈ dp) so pages get a phone-width mobile layout, then render
        // scaled by the density for crisp text.
        val density = resources.displayMetrics.density.toDouble()
        val widthPx = if (pageView.width > 0) pageView.width else resources.displayMetrics.widthPixels
        val heightPx = if (pageView.height > 0) pageView.height else resources.displayMetrics.heightPixels
        val viewportCss = Math.max(320, (widthPx / density).toInt())
        val viewportCssHeight = Math.max(240, (heightPx / density).toInt())
        val started = SystemClock.uptimeMillis()
        Log.i(TAG, "load start url=$url viewport=${viewportCss}x$viewportCssHeight view=${widthPx}x$heightPx density=$density gen=$gen")
        ioExecutor.execute {
            val handle = NativeBrowser.nativeOpen(url, viewportCss, viewportCssHeight, 600)
            val size = if (handle != 0L) NativeBrowser.nativePageSize(handle) else null
            val title = if (handle != 0L) NativeBrowser.nativeTitle(handle) else null
            val elapsed = SystemClock.uptimeMillis() - started
            Log.i(TAG, "load nativeOpen url=$url handle=$handle size=${size?.getOrNull(0)}x${size?.getOrNull(1)} title=${title ?: ""} elapsed=${elapsed}ms")
            runOnUiThread {
                if (gen != loadGen.get()) {
                    Log.i(TAG, "load stale url=$url gen=$gen current=${loadGen.get()}")
                    if (handle != 0L) NativeBrowser.nativeClose(handle)
                    return@runOnUiThread
                }
                progress.visibility = View.GONE
                if (handle == 0L || size == null) {
                    Log.e(TAG, "load failed url=$url handle=$handle")
                    if (handle != 0L) NativeBrowser.nativeClose(handle)
                    Toast.makeText(this, getString(R.string.load_failed, url), Toast.LENGTH_SHORT).show()
                    return@runOnUiThread
                }
                setTitle(if (!title.isNullOrEmpty()) title else getString(R.string.app_name))
                pageView.setDocument(handle, size[0], size[1])
            }
        }
    }

    private fun showLinkMenu(url: String) {
        val items = arrayOf(getString(R.string.open), getString(R.string.copy_link))
        AlertDialog.Builder(this)
            .setTitle(url)
            .setItems(items) { _, which ->
                when (which) {
                    0 -> navigate(url)
                    1 -> {
                        val cm = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
                        cm.setPrimaryClip(ClipData.newPlainText("url", url))
                        Toast.makeText(this, getString(R.string.link_copied), Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .show()
    }

    private fun normalizeUrl(input: String): String {
        if (input.startsWith("about:") || input.contains("://")) return input
        if (!input.contains('.') || input.contains(' ')) {
            return "https://duckduckgo.com/html/?q=" + android.net.Uri.encode(input)
        }
        return "https://$input"
    }

    private fun updateBackButton() {
        backButton.isEnabled = backStack.isNotEmpty()
    }

    // Offer the system default-browser chooser once, on first launch
    // (RoleManager exists on Android 10+; the user can change it any time
    // in Settings, so never re-prompt).
    private fun maybeRequestBrowserRole() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        val roleManager = getSystemService(RoleManager::class.java) ?: return
        if (!roleManager.isRoleAvailable(RoleManager.ROLE_BROWSER)) return
        if (roleManager.isRoleHeld(RoleManager.ROLE_BROWSER)) return
        val prefs = getPreferences(MODE_PRIVATE)
        if (prefs.getBoolean("browser_role_asked", false)) return
        prefs.edit().putBoolean("browser_role_asked", true).apply()
        browserRoleLauncher.launch(roleManager.createRequestRoleIntent(RoleManager.ROLE_BROWSER))
    }

    private fun extractCaBundle(): String {
        val systemBundle = File("/system/etc/security/cacerts.pem")
        if (systemBundle.exists()) return systemBundle.absolutePath
        val out = File(filesDir, "cacert.pem")
        if (!out.exists()) {
            runCatching {
                resources.openRawResource(R.raw.cacert).use { input ->
                    out.outputStream().use { input.copyTo(it) }
                }
            }
        }
        return if (out.exists()) out.absolutePath else ""
    }

    private fun hideKeyboard() {
        val imm = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
        imm.hideSoftInputFromWindow(urlBar.windowToken, 0)
    }

    private fun showUrlKeyboard() {
        urlBar.post {
            urlBar.requestFocus()
            val imm = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
            imm.showSoftInput(urlBar, InputMethodManager.SHOW_IMPLICIT)
        }
    }

    override fun onDestroy() {
        pageView.recycleDocument()
        super.onDestroy()
    }
}
