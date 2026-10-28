package dev.tuip.xclipserdna

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import android.widget.Toast
import java.io.File

/**
 * Settings screen. Writes /sdcard/XclipseRDNA/xclipse.env, which libxclipse_shim.so
 * reads inside the game process before Mesa starts (same idea as /sdcard/MG for MobileGlues).
 */
class MainActivity : Activity() {

    private val dir = File(Environment.getExternalStorageDirectory(), "XclipseRDNA")
    private val envFile = File(dir, "xclipse.env")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }

        root.addView(TextView(this).apply {
            text = "Xclipse RDNA Renderer\nExynos 1580 / Xclipse 540 (RDNA 3)"
            textSize = 18f
        })

        root.addView(TextView(this).apply { text = "\nDriver mode" })
        val mode = RadioGroup(this)
        val rbSystem = RadioButton(this).apply {
            text = "System Vulkan driver (Samsung) - stable"; id = 1001; isChecked = true
        }
        val rbRadv = RadioButton(this).apply {
            text = "RADV Xclipse (experimental, put zip contents in /sdcard/XclipseRDNA/radv/)"; id = 1002
        }
        mode.addView(rbSystem); mode.addView(rbRadv)
        root.addView(mode)

        root.addView(TextView(this).apply { text = "\nPreset" })
        val preset = RadioGroup(this)
        val presets = listOf("Balanced", "Performance", "Max compatibility")
        presets.forEachIndexed { i, n ->
            preset.addView(RadioButton(this).apply { text = n; id = 2000 + i; isChecked = i == 0 })
        }
        root.addView(preset)

        val gl33 = CheckBox(this).apply { text = "Report OpenGL 3.3 instead of the driver's own version" }
        root.addView(gl33)

        root.addView(Button(this).apply {
            text = "Save"
            setOnClickListener {
                if (!hasAllFiles()) { askAllFiles(); return@setOnClickListener }
                val radv = mode.checkedRadioButtonId == 1002
                val p = preset.checkedRadioButtonId - 2000
                save(radv, p, gl33.isChecked)
            }
        })

        root.addView(TextView(this).apply {
            text = "\nIn the launcher: Settings -> Renderer -> \"Xclipse RDNA\". Force-stop the launcher after installing/updating this APK."
        })
        setContentView(root)
    }

    private fun hasAllFiles() =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager()

    private fun askAllFiles() {
        Toast.makeText(this, "Allow \"All files access\" then press Save again", Toast.LENGTH_LONG).show()
        startActivity(
            Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName"))
        )
    }

    private fun save(radv: Boolean, preset: Int, gl33: Boolean) {
        val lines = mutableListOf<String>()
        lines += "XCLIPSE_MODE=${if (radv) "radv" else "system"}"
        if (radv) lines += "XCLIPSE_RADV_DIR=${File(dir, "radv").absolutePath}"

        lines += "MESA_LOADER_DRIVER_OVERRIDE=zink"
        // Off by default: the Mesa glthread batch thread is a known crash source with Kopper on
        // vendor Vulkan drivers. The "Performance" preset turns it back on.
        lines += "mesa_glthread=false"
        lines += "MESA_SHADER_CACHE_DIR=${File(dir, "shadercache").absolutePath}"

        // Only override the advertised version when asked: forcing 4.6 makes Zink fail to create a
        // context on drivers that cannot back it.
        if (gl33) {
            lines += "MESA_GL_VERSION_OVERRIDE=3.3"
            lines += "MESA_GLSL_VERSION_OVERRIDE=330"
        }

        when (preset) {
            1 -> {                                          // Performance
                lines += "ZINK_DESCRIPTORS=lazy"
                lines += "mesa_glthread=true"
            }
            2 -> lines += "ZINK_DESCRIPTORS=lazy"           // Max compatibility
        }

        dir.mkdirs()
        File(dir, "radv").mkdirs()
        envFile.writeText(lines.joinToString("\n") + "\n")
        Toast.makeText(this, "Saved to ${envFile.absolutePath}", Toast.LENGTH_LONG).show()
    }
}
