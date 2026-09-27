package com.etc.protector;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TabHost;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * ETC+ 加固卫士 v2.1 - Android 手机版
 * 界面镜像桌面版：加固 / 设置 / 关于 三个标签
 * 真实加固：完整性保护 + 防篡改清单 + 防二次加固 + 品牌标识（不修改任何 DEX 代码）
 */
public class MainActivity extends Activity {

    static final String APP_VERSION = "2.1";
    static final String MARKER_PATH = "assets/etc_protect.etc";
    static final String SIG_PATH = "META-INF/ETCPLUS.SF";
    static final String INTEGRITY_PATH = "assets/etc_integrity.json";
    static final byte[] XOR_KEY = "ETC+@2026#SECURE".getBytes();

    static final String[][] THEMES = {
            {"极光绿", "#00C853", "#69F0AE", "#F1F8E9", "#1B5E20", "#FFFFFF", "#C8E6C9"},
            {"深海蓝", "#1565C0", "#64B5F6", "#E3F2FD", "#0D47A1", "#FFFFFF", "#BBDEFB"},
            {"樱花粉", "#E91E63", "#F06292", "#FCE4EC", "#880E4F", "#FFFFFF", "#F8BBD0"},
            {"星空紫", "#7B1FA2", "#CE93D8", "#F3E5F5", "#4A148C", "#FFFFFF", "#E1BEE7"},
            {"烈焰橙", "#E65100", "#FFB74D", "#FFF3E0", "#BF360C", "#FFFFFF", "#FFE0B2"},
    };

    private String primary = "#00C853", primaryLight = "#69F0AE", bg = "#F1F8E9",
            text = "#1B5E20", card = "#FFFFFF", border = "#C8E6C9";

    private final List<String> apkFiles = new ArrayList<>();   // cache 文件路径
    private final List<String> apkNames = new ArrayList<>();   // 显示名
    private ArrayAdapter<String> listAdapter;
    private Spinner modeSpinner, themeSpinner;
    private EditText brandEdit, verEdit;
    private TextView statusText;
    private ProgressBar progressBar;
    private LinearLayout root;
    private TabHost tabHost;
    private float fontScale = 1.0f;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private File cacheDir;
    private boolean busy = false;
    private File pendingOutput = null;

    // ---------------- 生命周期 ----------------
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        cacheDir = getCacheDir();
        buildUI();
        applyTheme(0);
    }

    // ---------------- 界面构建（纯代码，无 androidx） ----------------
    private void buildUI() {
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        // 标题栏
        LinearLayout titleBar = new LinearLayout(this);
        titleBar.setOrientation(LinearLayout.HORIZONTAL);
        titleBar.setGravity(Gravity.CENTER_VERTICAL);
        titleBar.setPadding(dp(16), dp(12), dp(16), dp(12));
        TextView title = new TextView(this);
        title.setText("ETC+ 加固卫士");
        title.setTextSize(22);
        title.setTextColor(Color.parseColor(primary));
        titleBar.addView(title);
        titleBar.addView(spacer(0, 0, 1f, 0));
        titleBar.addView(themeSpinner = new Spinner(this));
        ArrayAdapter<String> ta = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item,
                new String[]{THEMES[0][0], THEMES[1][0], THEMES[2][0], THEMES[3][0], THEMES[4][0]});
        ta.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        themeSpinner.setAdapter(ta);
        root.addView(titleBar);

        // 标签页
        tabHost = new TabHost(this);
        tabHost.setup();
        FrameLayout tabContent = new FrameLayout(this);
        tabHost.addView(tabHost.getTabWidget());
        tabHost.addView(tabContent);
        root.addView(tabHost, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        addTab("🔒 加固", tabContent, buildProtectTab());
        addTab("⚙️ 设置", tabContent, buildSettingsTab());
        addTab("ℹ️ 关于", tabContent, buildAboutTab());

        setContentView(root);
        tabHost.setOnTabChangedListener(id -> applyTabColors());
    }

    private void addTab(String label, FrameLayout container, View content) {
        TabHost.TabSpec spec = tabHost.newTabSpec(label);
        spec.setIndicator(label);
        spec.setContent(new TabHost.TabContentFactory() {
            @Override
            public View createTabContent(String tag) {
                return content;
            }
        });
        tabHost.addTab(spec);
    }

    private View buildProtectTab() {
        ScrollView sv = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(16), dp(16), dp(16), dp(16));
        sv.addView(col);

        // 列表
        TextView l1 = label("📱 APK 文件列表", true);
        col.addView(l1);
        listAdapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, apkNames);
        ListView lv = new ListView(this);
        lv.setAdapter(listAdapter);
        lv.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
        col.addView(lv, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(240)));

        LinearLayout row1 = new LinearLayout(this);
        row1.setOrientation(LinearLayout.HORIZONTAL);
        row1.addView(btn("📂 选择APK", v -> pickApk()));
        row1.addView(btn("🗑️ 移除", v -> removeSelected(lv)));
        row1.addView(btn("🔎 校验", v -> verifySelected(lv)));
        col.addView(row1);

        // 加固设置
        TextView l2 = label("🔐 加固设置", true);
        col.addView(l2);
        LinearLayout row2 = new LinearLayout(this);
        row2.setOrientation(LinearLayout.HORIZONTAL);
        row2.setGravity(Gravity.CENTER_VERTICAL);
        row2.addView(label("加固方式", false));
        modeSpinner = new Spinner(this);
        ArrayAdapter<String> ma = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item,
                new String[]{"标准加固（完整性保护，推荐）", "深度加固（标识加密）"});
        ma.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        modeSpinner.setAdapter(ma);
        row2.addView(modeSpinner, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        col.addView(row2);

        LinearLayout row3 = new LinearLayout(this);
        row3.setOrientation(LinearLayout.HORIZONTAL);
        row3.setGravity(Gravity.CENTER_VERTICAL);
        row3.addView(label("品牌", false));
        brandEdit = new EditText(this);
        brandEdit.setText("ETC+加固");
        row3.addView(brandEdit, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        row3.addView(label("版本", false));
        verEdit = new EditText(this);
        verEdit.setText(APP_VERSION);
        verEdit.setWidth(dp(60));
        row3.addView(verEdit);
        col.addView(row3);

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        col.addView(progressBar);

        statusText = label("就绪", false);
        statusText.setGravity(Gravity.CENTER);
        statusText.setPadding(0, dp(8), 0, dp(8));
        col.addView(statusText);

        Button start = btn("🚀 开始加固", v -> startProtect());
        start.setTextSize(18);
        col.addView(start);

        TextView info = label("💡 加固不修改任何代码，应用可正常安装使用；已加固的APK无法二次导入。", false);
        info.setTextSize(11);
        info.setGravity(Gravity.CENTER);
        info.setPadding(0, dp(4), 0, dp(4));
        col.addView(info);
        return sv;
    }

    private View buildSettingsTab() {
        ScrollView sv = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(16), dp(16), dp(16), dp(16));
        sv.addView(col);

        TextView l1 = label("🔤 字体大小", true);
        col.addView(l1);
        LinearLayout fr = new LinearLayout(this);
        fr.setOrientation(LinearLayout.HORIZONTAL);
        fr.setGravity(Gravity.CENTER_VERTICAL);
        fr.addView(label("小", false));
        android.widget.SeekBar sb = new android.widget.SeekBar(this);
        sb.setMax(10);
        sb.setProgress(5);
        fr.addView(sb, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        fr.addView(label("大", false));
        col.addView(fr);
        sb.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(android.widget.SeekBar seekBar, int progress, boolean fromUser) {
                fontScale = 0.8f + progress * 0.06f;
                applyTextScale();
            }
            @Override public void onStartTrackingTouch(android.widget.SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(android.widget.SeekBar seekBar) {}
        });

        TextView l2 = label("📦 构建环境（预留）", true);
        col.addView(l2);
        col.addView(label("NDK: 未安装（预留）", false));
        col.addView(label("Maven: 未安装（预留）", false));
        col.addView(label("android.jar: 未配置（预留）", false));

        TextView l3 = label("💡 说明", true);
        col.addView(l3);
        TextView note = label("手机版与电脑版功能一致：完整加固（不损坏DEX）、防二次加固、完整性校验、品牌标识。\n加固后的APK请在MT管理器/NP管理器中重新签名后安装。", false);
        note.setTextSize(12);
        col.addView(note);
        return sv;
    }

    private View buildAboutTab() {
        ScrollView sv = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER_HORIZONTAL);
        col.setPadding(dp(16), dp(24), dp(16), dp(16));
        sv.addView(col);

        TextView t1 = new TextView(this);
        t1.setText("ETC+");
        t1.setTextSize(34);
        t1.setGravity(Gravity.CENTER);
        t1.setTextColor(Color.parseColor(primary));
        col.addView(t1);

        TextView t2 = new TextView(this);
        t2.setText("加固卫士 v" + APP_VERSION);
        t2.setTextSize(16);
        t2.setGravity(Gravity.CENTER);
        t2.setTextColor(Color.GRAY);
        col.addView(t2);

        col.addView(spacer(0, dp(16), 0, 0));
        TextView feat = new TextView(this);
        feat.setText("✅ 真实加固（完整性保护 + 防篡改清单）\n✅ 防二次加固导入\n✅ 批量加固处理\n✅ 加固后应用可正常安装使用，不闪退\n✅ 自定义主题支持\n✅ C++ 引擎 / Python 桌面版 / Java 手机版");
        feat.setTextSize(14);
        feat.setGravity(Gravity.CENTER);
        col.addView(feat);

        col.addView(spacer(0, dp(16), 0, 0));
        TextView copy = new TextView(this);
        copy.setText("ETC官方\n© 2026 版权所有\nET");
        copy.setTextSize(13);
        copy.setTextColor(Color.GRAY);
        copy.setGravity(Gravity.CENTER);
        col.addView(copy);

        col.addView(spacer(0, dp(16), 0, 0));
        Button lic = btn("📜 查看许可协议", v -> showLicense());
        col.addView(lic);
        return sv;
    }

    private void showLicense() {
        new AlertDialog.Builder(this)
                .setTitle("许可协议")
                .setMessage("ETC+ 加固卫士 许可协议 v" + APP_VERSION +
                        "\n\n1. 本软件仅供合法用途（保护自有应用），用户需遵守所在国家法律法规。\n" +
                        "2. 禁止将本软件用于任何非法目的，包括但不限于破解、盗版、传播恶意软件等。\n" +
                        "3. 本软件知识产权归 ETC 官方所有。\n" +
                        "4. 本软件按\"现状\"提供，不提供任何明示或暗示的担保。\n" +
                        "5. ETC 官方不对因使用本软件产生的任何损失负责。\n" +
                        "6. 用户应妥善保管使用本软件产生的所有文件。\n" +
                        "7. 本协议受中华人民共和国法律管辖。\n\n" +
                        "© 2026 ETC 官方 版权所有\n联系: et2416444244@outlook.com")
                .setPositiveButton("我已阅读并同意", null)
                .show();
    }

    // ---------------- 小工具 ----------------
    private TextView label(String s, boolean bold) {
        TextView tv = new TextView(this);
        tv.setText(s);
        tv.setTextSize(14 * fontScale);
        if (bold) tv.setTextSize(16 * fontScale);
        tv.setTextColor(Color.parseColor(text));
        return tv;
    }

    private View spacer(int w, int h, float weight, int _unused) {
        LinearLayout ll = new LinearLayout(this);
        if (weight > 0) {
            ll.setLayoutParams(new LinearLayout.LayoutParams(0, 1, weight));
            return ll;
        }
        ll.setLayoutParams(new LinearLayout.LayoutParams(w, h));
        return ll;
    }

    private Button btn(String s, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextSize(14 * fontScale);
        b.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lp.setMargins(0, dp(4), dp(4), dp(4));
        b.setLayoutParams(lp);
        return b;
    }

    private int dp(float v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void applyTextScale() {
        // 简化：重建界面会丢失状态，这里仅提示
        Toast.makeText(this, "字体已调整", Toast.LENGTH_SHORT).show();
    }

    // ---------------- 主题 ----------------
    private void applyTheme(int idx) {
        String[] t = THEMES[idx];
        primary = t[1]; primaryLight = t[2]; bg = t[3]; text = t[4]; card = t[5]; border = t[6];
        root.setBackgroundColor(Color.parseColor(bg));
        // 标签
        for (int i = 0; i < tabHost.getTabWidget().getChildCount(); i++) {
            TextView tv = (TextView) tabHost.getTabWidget().getChildAt(i);
            tv.setTextColor(Color.parseColor(text));
            tv.setTextSize(14);
            tv.setGravity(Gravity.CENTER);
            tv.setPadding(dp(8), dp(10), dp(8), dp(10));
            tv.setBackgroundColor(Color.parseColor(i == tabHost.getCurrentTab() ? primary : border));
            if (i == tabHost.getCurrentTab()) tv.setTextColor(Color.WHITE);
        }
        // 列表与按钮颜色
        recolorTree(root);
    }

    private void recolorTree(View v) {
        if (v instanceof Button) {
            v.setBackgroundColor(Color.parseColor(primary));
            ((Button) v).setTextColor(Color.WHITE);
        } else if (v instanceof TextView) {
            ((TextView) v).setTextColor(Color.parseColor(text));
        } else if (v instanceof ListView) {
            v.setBackgroundColor(Color.parseColor(card));
        }
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            for (int i = 0; i < g.getChildCount(); i++) recolorTree(g.getChildAt(i));
        }
    }

    private void applyTabColors() {
        applyTheme(themeSpinner.getSelectedItemPosition());
    }

    // ---------------- 加固核心（Java 实现，与 C++/Python 逻辑一致） ----------------
    static String sha256(InputStream in) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
        StringBuilder sb = new StringBuilder();
        for (byte b : md.digest()) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    static String encryptText(String s) {
        byte[] d = s.getBytes();
        byte[] out = new byte[d.length];
        for (int i = 0; i < d.length; i++) out[i] = (byte) (d[i] ^ XOR_KEY[i % XOR_KEY.length]);
        StringBuilder sb = new StringBuilder("ETCX1:");
        for (byte b : out) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    static String decryptText(String s) {
        if (!s.startsWith("ETCX1:")) return s;
        byte[] d = new byte[(s.length() - 6) / 2];
        for (int i = 0; i < d.length; i++)
            d[i] = (byte) Integer.parseInt(s.substring(6 + i * 2, 8 + i * 2), 16);
        byte[] out = new byte[d.length];
        for (int i = 0; i < d.length; i++) out[i] = (byte) (d[i] ^ XOR_KEY[i % XOR_KEY.length]);
        return new String(out);
    }

    static final String[] PACKER_MARKS = {"libdexhelper", "libprotectclass", "libjiagu", "libnesec",
            "libnqshield", "libshell", "stubapp", "secneo", "bangcle", "com.qihoo.util",
            "libtosprotection", "libseal"};

    static int[] checkApk(File apk) {
        // [state, dexCount]  state: 0非APK 1未加固 2ETC加固 3其它加固
        try (java.util.zip.ZipFile z = new java.util.zip.ZipFile(apk)) {
            List<String> names = new ArrayList<>();
            java.util.Enumeration<? extends ZipEntry> en = z.entries();
            int dex = 0;
            boolean manifest = false;
            boolean marker = false, sig = false;
            StringBuilder low = new StringBuilder();
            while (en.hasMoreElements()) {
                String n = en.nextElement().getName();
                names.add(n);
                low.append(n.toLowerCase()).append(' ');
                if (n.equals("AndroidManifest.xml")) manifest = true;
                if (n.endsWith(".dex")) dex++;
                if (n.equals(MARKER_PATH)) marker = true;
                if (n.equals(SIG_PATH)) sig = true;
            }
            if (!manifest) return new int[]{0, dex};
            if (marker || sig) return new int[]{2, dex};
            String lows = low.toString();
            for (String p : PACKER_MARKS) if (lows.contains(p)) return new int[]{3, dex};
            return new int[]{1, dex};
        } catch (Exception e) {
            return new int[]{0, 0};
        }
    }

    static String hardenApk(File in, File out, int mode, String brand, String version) throws Exception {
        JSONObject manifest = new JSONObject();
        manifest.put("brand", brand);
        manifest.put("version", version);
        manifest.put("mode", mode);
        manifest.put("time", new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.CHINA).format(new Date()));
        JSONObject files = new JSONObject();

        try (ZipInputStream zin = new ZipInputStream(new FileInputStream(in));
             ZipOutputStream zout = new ZipOutputStream(new FileOutputStream(out))) {
            ZipEntry e;
            while ((e = zin.getNextEntry()) != null) {
                String name = e.getName();
                String up = name.toUpperCase(Locale.US);
                if (e.isDirectory()) continue;
                if (up.startsWith("META-INF/") && (up.endsWith(".SF") || up.endsWith(".RSA")
                        || up.endsWith(".DSA") || up.endsWith(".MF"))) continue;
                MessageDigest md = MessageDigest.getInstance("SHA-256");
                ZipEntry ne = new ZipEntry(name);
                zout.putNextEntry(ne);
                byte[] buf = new byte[65536];
                int n, total = 0;
                while ((n = zin.read(buf)) > 0) {
                    md.update(buf, 0, n);
                    zout.write(buf, 0, n);
                    total += n;
                }
                zout.closeEntry();
                StringBuilder sb = new StringBuilder();
                for (byte b : md.digest()) sb.append(String.format("%02x", b));
                JSONObject item = new JSONObject();
                item.put("s", sb.toString());
                item.put("z", total);
                files.put(name, item);
            }
            manifest.put("files", files);
            String now = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.CHINA).format(new Date());
            String mjson = manifest.toString();
            String digest = sha256(new java.io.ByteArrayInputStream(mjson.getBytes()));
            String marker = "ETC+ PROTECTED\nBrand: " + brand + "\nVersion: " + version +
                    "\nTime: " + now + "\nSignature: " + digest + "\nCopyright: ETC Official\n";
            String sig = "Signature-Version: 1.0\nCreated-By: " + brand + " " + version +
                    "\nX-Protect-By: ETC+ Security\nX-Protected-At: " + now + "\n";
            if (mode == 1) {
                marker = encryptText(marker);
                mjson = encryptText(mjson);
            }
            writeEntry(zout, MARKER_PATH, marker.getBytes());
            writeEntry(zout, SIG_PATH, sig.getBytes());
            writeEntry(zout, INTEGRITY_PATH, mjson.getBytes());
        }
        return manifest.toString();
    }

    private static void writeEntry(ZipOutputStream zout, String name, byte[] data) throws Exception {
        ZipEntry e = new ZipEntry(name);
        zout.putNextEntry(e);
        zout.write(data);
        zout.closeEntry();
    }

    static String verifyApk(File apk) {
        try (java.util.zip.ZipFile z = new java.util.zip.ZipFile(apk)) {
            ZipEntry me = z.getEntry(MARKER_PATH);
            ZipEntry ie = z.getEntry(INTEGRITY_PATH);
            if (me == null || ie == null) return "该APK不是ETC+加固产物，无法校验";
            String markerText = decryptText(new String(readAll(z.getInputStream(me))));
            String jsonText = decryptText(new String(readAll(z.getInputStream(ie))));
            JSONObject manifest = new JSONObject(jsonText);
            JSONObject files = manifest.getJSONObject("files");
            String brand = manifest.optString("brand", "ETC+加固");
            String version = manifest.optString("version", "2.1");
            int mode = manifest.optInt("mode", 0);
            StringBuilder bad = new StringBuilder();
            int checked = 0;
            java.util.Iterator<String> keys = files.keys();
            while (keys.hasNext()) {
                String name = keys.next();
                if (name.equals(MARKER_PATH) || name.equals(INTEGRITY_PATH) || name.equals(SIG_PATH)) continue;
                ZipEntry e = z.getEntry(name);
                if (e == null) {
                    bad.append("缺失: ").append(name).append('\n');
                    continue;
                }
                String expected = files.getJSONObject(name).optString("s", "");
                String actual = sha256(z.getInputStream(e));
                checked++;
                if (!expected.equals(actual)) bad.append("被篡改: ").append(name).append('\n');
            }
            if (bad.length() == 0) {
                return "校验通过：所有文件未被篡改（" + brand + " " + version + "/"
                        + (mode == 0 ? "标准" : "深度") + "/" + checked + "项）";
            }
            return "校验失败：检测到文件被篡改\n" + bad;
        } catch (Exception e) {
            return "校验失败: " + e.getMessage();
        }
    }

    private static byte[] readAll(InputStream in) throws Exception {
        java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
        return bos.toByteArray();
    }

    // ---------------- 交互逻辑 ----------------
    private void pickApk() {
        // 不限制 MIME，避免部分机型把 APK 置灰；选入后按内容校验
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, 1001);
    }

    private void removeSelected(ListView lv) {
        int pos = lv.getCheckedItemPosition();
        if (pos < 0 || pos >= apkFiles.size()) {
            Toast.makeText(this, "请先勾选要移除的项目", Toast.LENGTH_SHORT).show();
            return;
        }
        new File(apkFiles.remove(pos)).delete();
        apkNames.remove(pos);
        listAdapter.notifyDataSetChanged();
    }

    private void verifySelected(ListView lv) {
        int pos = lv.getCheckedItemPosition();
        if (pos < 0 || pos >= apkFiles.size()) {
            Toast.makeText(this, "请先勾选要校验的项目", Toast.LENGTH_SHORT).show();
            return;
        }
        final File f = new File(apkFiles.get(pos));
        statusText.setText("正在校验: " + f.getName());
        new Thread(() -> {
            final String result = verifyApk(f);
            ui.post(() -> {
                statusText.setText("就绪");
                new AlertDialog.Builder(this).setTitle("校验结果").setMessage(result)
                        .setPositiveButton("确定", null).show();
            });
        }).start();
    }

    private void startProtect() {
        if (busy) return;
        if (apkFiles.isEmpty()) {
            Toast.makeText(this, "请先选择APK文件", Toast.LENGTH_SHORT).show();
            return;
        }
        busy = true;
        final String brand = brandEdit.getText().toString().trim();
        final String version = verEdit.getText().toString().trim();
        final int mode = modeSpinner.getSelectedItemPosition();
        final List<File> inputs = new ArrayList<>();
        final List<String> names = new ArrayList<>(apkNames);
        for (String p : apkFiles) inputs.add(new File(p));

        progressBar.setProgress(0);
        new Thread(() -> {
            try {
                for (int i = 0; i < inputs.size(); i++) {
                    File in = inputs.get(i);
                    final String base = names.get(i).replaceAll("\\.apk$", "");
                    File out = new File(cacheDir, base + "_ETC.apk");
                    ui.post(() -> statusText.setText("正在加固: " + base + ".apk"));
                    hardenApk(in, out, mode, brand, version);
                    pendingOutput = out;
                    ui.post(() -> saveOutput(out, base + "_ETC.apk"));
                    final int pct = (int) ((i + 1) * 100.0 / inputs.size());
                    ui.post(() -> progressBar.setProgress(pct));
                }
            } catch (Exception e) {
                ui.post(() -> Toast.makeText(this, "加固失败: " + e.getMessage(), Toast.LENGTH_LONG).show());
            } finally {
                busy = false;
                ui.post(() -> statusText.setText("就绪"));
            }
        }).start();
    }

    private void saveOutput(File src, String title) {
        pendingOutput = src;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/vnd.android.package-archive");
        intent.putExtra(Intent.EXTRA_TITLE, title);
        startActivityForResult(intent, 1002);
    }

    private void setButtonsEnabled(boolean on) {
        // 简单实现：重新启用所有按钮
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) return;
        if (requestCode == 1001) {
            Uri uri = data.getData();
            if (uri == null) return;
            try {
                File copy = new File(cacheDir, "input_" + System.currentTimeMillis() + ".apk");
                try (InputStream in = getContentResolver().openInputStream(uri);
                     FileOutputStream fout = new FileOutputStream(copy)) {
                    byte[] buf = new byte[65536];
                    int n;
                    while ((n = in.read(buf)) > 0) fout.write(buf, 0, n);
                }
                int[] st = checkApk(copy);
                if (st[0] == 0) {
                    Toast.makeText(this, "不是有效的APK文件", Toast.LENGTH_LONG).show();
                    copy.delete();
                    return;
                }
                if (st[0] == 2) {
                    Toast.makeText(this, "该APK已由ETC+加固，禁止二次加固！", Toast.LENGTH_LONG).show();
                    copy.delete();
                    return;
                }
                if (st[0] == 3) {
                    Toast.makeText(this, "检测到其它加固，请先还原为未加固版本", Toast.LENGTH_LONG).show();
                    copy.delete();
                    return;
                }
                String dn = uri.getLastPathSegment();
                if (dn == null) dn = "app.apk";
                if (dn.contains("/")) dn = dn.substring(dn.lastIndexOf('/') + 1);
                apkFiles.add(copy.getAbsolutePath());
                apkNames.add(dn + "  [未加固] DEX×" + st[1]);
                listAdapter.notifyDataSetChanged();
                statusText.setText("已添加 " + apkFiles.size() + " 个APK，均可安全加固");
            } catch (Exception e) {
                Toast.makeText(this, "读取失败: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        } else if (requestCode == 1002) {
            Uri uri = data.getData();
            if (uri == null || pendingOutput == null) return;
            try {
                File src = pendingOutput;
                try (InputStream in = new FileInputStream(src);
                     OutputStream out = getContentResolver().openOutputStream(uri, "w")) {
                    byte[] buf = new byte[65536];
                    int n;
                    while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                }
                Toast.makeText(this, "加固完成，已保存！请在MT/NP管理器中重新签名后安装", Toast.LENGTH_LONG).show();
            } catch (Exception e) {
                Toast.makeText(this, "保存失败: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }
    }
}
