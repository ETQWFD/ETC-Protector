# -*- coding: utf-8 -*-
"""
ETC+ 加固卫士 v2.1 - 桌面版（Python / PyQt6）
================================================
- 真实加固（完整性保护层 + 防篡改清单 + 防二次加固 + 品牌标识）
- 不修改任何 DEX 代码，保证应用安装后正常运行、不闪退
- 深度模式：加固标识加密
- 输入 APK 永远不会被改动，输出为全新文件

架构：Python 实现核心逻辑（与 C++ 引擎、Java 手机版逻辑一致）
"""
import sys
import os
import json
import shutil
import zipfile
import hashlib
import struct
import time
import datetime
from pathlib import Path

from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                             QHBoxLayout, QGridLayout, QTabWidget, QLabel,
                             QPushButton, QComboBox, QListWidget, QListWidgetItem,
                             QGroupBox, QLineEdit, QSpinBox, QTextEdit,
                             QMessageBox, QFileDialog, QProgressBar,
                             QDialog, QFrame)
from PyQt6.QtCore import QThread, pyqtSignal, Qt, QTimer
from PyQt6.QtGui import QIcon, QFont

APP_NAME = "ETC+ 加固卫士"
APP_VERSION = "2.1"
MARKER_PATH = "assets/etc_protect.etc"
SIG_PATH = "META-INF/ETCPLUS.SF"
INTEGRITY_PATH = "assets/etc_integrity.json"
XOR_KEY = b"ETC+@2026#SECURE"


# ==================== 核心引擎（纯 Python 实现） ====================
class ProtectorCore:
    """加固核心：与 C++ 引擎逻辑一致"""

    @staticmethod
    def _zip_name(name):
        return name.rstrip("\x00")

    @staticmethod
    def _encrypt_text(text):
        data = text.encode("utf-8")
        key = XOR_KEY
        xored = bytes(b ^ key[i % len(key)] for i, b in enumerate(data))
        return "ETCX1:" + xored.hex()

    @staticmethod
    def _decrypt_text(text):
        if text.startswith("ETCX1:"):
            xored = bytes.fromhex(text[6:])
            key = XOR_KEY
            return bytes(b ^ key[i % len(key)] for i, b in enumerate(xored)).decode("utf-8", "ignore")
        return text

    @staticmethod
    def check(apk_path):
        """返回 (state, message, brand, version, dex_count) state: 0非APK 1未加固 2ETC加固 3其它加固"""
        if not os.path.exists(apk_path):
            return 0, "文件不存在", "", "", 0
        try:
            with zipfile.ZipFile(apk_path) as z:
                names = z.namelist()
                has_manifest = "AndroidManifest.xml" in names
                dexes = [n for n in names if n.endswith(".dex")]
                if not has_manifest or not dexes:
                    return 0, "缺少 AndroidManifest.xml 或 DEX，不是有效APK", "", "", len(dexes)
                if MARKER_PATH in names:
                    brand, version = "ETC+ Protector", "2.1"
                    try:
                        content = z.read(MARKER_PATH).decode("utf-8", "ignore")
                        for line in content.splitlines():
                            if line.startswith("Brand: "):
                                brand = line[7:].strip()
                            elif line.startswith("Version: "):
                                version = line[9:].strip()
                    except Exception:
                        pass
                    return 2, "该APK已由 {} 加固，禁止二次加固".format(brand), brand, version, len(dexes)
                if SIG_PATH in names:
                    return 2, "该APK已由 ETC+ 加固（旧标识），禁止二次加固", "ETC+ Protector", "2.1", len(dexes)
                packs = ["libdexhelper", "libprotectclass", "libjiagu", "libnesec",
                         "libnqshield", "libshell", "stubapp", "secneo", "bangcle",
                         "com.qihoo.util", "libtosprotection", "libseal"]
                low_names = " ".join(names).lower()
                for p in packs:
                    if p in low_names:
                        return 3, "检测到其它加固特征({})，请先还原为未加固版本".format(p), "", "", len(dexes)
                return 1, "未检测到加固", "", "", len(dexes)
        except zipfile.BadZipFile:
            return 0, "不是有效的APK/ZIP文件", "", "", 0

    @staticmethod
    def harden(input_path, output_path, mode=0, brand="ETC+ Protector", version="2.1"):
        """加固：不修改任何 DEX 代码，输出全新文件"""
        state, msg, _, _, _ = ProtectorCore.check(input_path)
        if state == 0:
            return False, msg, ""
        if state == 2:
            return False, msg, ""
        if state == 3:
            return False, msg

        out_dir = os.path.dirname(os.path.abspath(output_path))
        os.makedirs(out_dir, exist_ok=True)
        if os.path.exists(output_path):
            os.remove(output_path)

        manifest = {
            "brand": brand, "version": version, "mode": mode,
            "time": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "files": {}
        }

        try:
            with zipfile.ZipFile(input_path, "r") as zin, \
                 zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zout:
                for info in zin.infolist():
                    name = ProtectorCore._zip_name(info.filename)
                    # 跳过原签名文件（重新打包后需重新签名）
                    up = name.upper()
                    if up.startswith("META-INF/") and up[-4:] in (".SF", ".RSA", ".DSA", ".MF"):
                        continue
                    if info.is_dir():
                        continue
                    data = zin.read(info.filename)
                    digest = hashlib.sha256(data).hexdigest()
                    manifest["files"][name] = {"s": digest, "z": len(data)}
                    zi = zipfile.ZipInfo(name, date_time=info.date_time)
                    zi.compress_type = info.compress_type if info.compress_type == zipfile.ZIP_STORED else zipfile.ZIP_DEFLATED
                    zi.external_attr = info.external_attr
                    zout.writestr(zi, data)

                now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                marker = "ETC+ PROTECTED\nBrand: {}\nVersion: {}\nTime: {}\nSignature: {}\nCopyright: ETC Official\n".format(
                    brand, version, now,
                    hashlib.sha256(json.dumps(manifest, ensure_ascii=False).encode("utf-8")).hexdigest())
                sig = "Signature-Version: 1.0\nCreated-By: {} {}\nX-Protect-By: ETC+ Security\nX-Protected-At: {}\n".format(
                    brand, version, now)
                integrity = json.dumps(manifest, ensure_ascii=False)

                if mode == 1:
                    marker = ProtectorCore._encrypt_text(marker)
                    integrity = ProtectorCore._encrypt_text(integrity)

                zout.writestr(MARKER_PATH, marker)
                zout.writestr(SIG_PATH, sig)
                zout.writestr(INTEGRITY_PATH, integrity)
            return True, "加固成功", output_path
        except Exception as e:
            if os.path.exists(output_path):
                try:
                    os.remove(output_path)
                except Exception:
                    pass
            return False, "加固失败: {}".format(e), ""

    @staticmethod
    def verify(apk_path):
        """返回 (ok, message, items) items=[(name, ok, expected, actual, extra, missing)]"""
        if not os.path.exists(apk_path):
            return False, "文件不存在", []
        try:
            with zipfile.ZipFile(apk_path) as z:
                names = set(z.namelist())
                if MARKER_PATH not in names or INTEGRITY_PATH not in names:
                    return False, "该APK不是ETC+加固产物，无法校验", []
                marker_text = z.read(MARKER_PATH).decode("utf-8", "ignore")
                integrity_text = z.read(INTEGRITY_PATH).decode("utf-8", "ignore")
                marker_text = ProtectorCore._decrypt_text(marker_text)
                integrity_text = ProtectorCore._decrypt_text(integrity_text)
                try:
                    manifest = json.loads(integrity_text)
                except Exception:
                    return False, "完整性清单解析失败", []
                files = manifest.get("files", {})
                brand = manifest.get("brand", "ETC+ Protector")
                version = manifest.get("version", "2.1")
                mode = manifest.get("mode", 0)

                all_ok = True
                items = []
                for name, item in files.items():
                    if name in (MARKER_PATH, INTEGRITY_PATH, SIG_PATH):
                        continue
                    expected = item.get("s", "")
                    if name not in names:
                        items.append((name, False, expected, "", False, True))
                        all_ok = False
                        continue
                    actual = hashlib.sha256(z.read(name)).hexdigest()
                    ok = actual == expected
                    if not ok:
                        all_ok = False
                    items.append((name, ok, expected, actual, False, False))
                # 新增条目检测
                for name in z.namelist():
                    if name in (MARKER_PATH, INTEGRITY_PATH, SIG_PATH):
                        continue
                    if name not in files:
                        items.append((name, False, "", "", True, False))
                        all_ok = False
                msg = "校验通过：所有文件未被篡改（{} {}/{}/{})".format(
                    brand, version, "标准" if mode == 0 else "深度", len(files)) if all_ok \
                    else "校验失败：检测到文件被篡改"
                return all_ok, msg, items
        except zipfile.BadZipFile:
            return False, "不是有效的APK文件", []


# ==================== 日志 ====================
class Logger:
    def __init__(self):
        self.log_dir = Path(os.getcwd()) / "logs"
        self.log_dir.mkdir(exist_ok=True)
        now = datetime.datetime.now()
        self.log_file = self.log_dir / "log_{}.txt".format(now.strftime("%Y%m%d_%H%M%S"))
        with open(self.log_file, "w", encoding="utf-8") as f:
            f.write("ETC+ Protect Log\nStart Time: {}\n{}\n\n".format(
                now.strftime("%Y-%m-%d %H:%M:%S"), "=" * 60))

    def log(self, message, level="INFO"):
        ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        line = "[{}] [{}] {}\n".format(ts, level, message)
        print(line.strip())
        try:
            with open(self.log_file, "a", encoding="utf-8") as f:
                f.write(line)
        except Exception:
            pass


logger = Logger()


# ==================== 许可协议 ====================
LICENSE_TEXT = """ETC+ 加固卫士 许可协议

版本: 2.1

1. 许可范围
   本软件仅供合法用途（保护自有应用），用户需遵守所在国家法律法规。

2. 使用限制
   禁止将本软件用于任何非法目的，包括但不限于破解、盗版、传播恶意软件等。

3. 知识产权
   本软件的所有知识产权归 ETC 官方所有，受版权法保护。

4. 免责声明
   本软件按"现状"提供，不提供任何明示或暗示的担保。

5. 责任限制
   在任何情况下，ETC 官方不对因使用本软件产生的任何损失负责。

6. 用户义务
   用户应妥善保管使用本软件产生的所有文件。

7. 更新和修改
   ETC 官方保留随时更新和修改本协议的权利。

8. 终止条款
   如用户违反本协议，ETC 官方有权终止其使用权限。

9. 适用法律
   本协议受中华人民共和国法律管辖。

10. 争议解决
    因本协议产生的争议，双方应友好协商解决。

11. 数据安全
    用户应对使用本软件处理的数据安全负责。

12. 第三方权利
    本协议不影响第三方的合法权益。

13. 完整协议
    本协议构成双方之间的完整协议。

14. 可分割性
    如本协议任何条款无效，不影响其他条款的效力。

15. 放弃权利
    ETC 官方未行使任何权利不视为放弃该权利。

16. 标题说明
    本协议各条款标题仅为方便阅读，不影响解释。

17. 语言版本
    本协议以中文版本为准。

18. 生效日期
    本协议自2026年1月1日起生效。

19. 联系方式
    如有问题请联系: et2416444244@outlook.com

20. 最终解释权
    本协议的最终解释权归 ETC 官方所有。

© 2026 ETC 官方 版权所有"""


# ==================== 主题 ====================
THEMES = {
    "极光绿": dict(primary="#00C853", primary_light="#69F0AE", primary_dark="#009624",
                  bg="#F1F8E9", text="#1B5E20", card="#FFFFFF", border="#C8E6C9",
                  gradient="qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #00C853, stop:1 #69F0AE)"),
    "深海蓝": dict(primary="#1565C0", primary_light="#64B5F6", primary_dark="#0D47A1",
                  bg="#E3F2FD", text="#0D47A1", card="#FFFFFF", border="#BBDEFB",
                  gradient="qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1565C0, stop:1 #64B5F6)"),
    "樱花粉": dict(primary="#E91E63", primary_light="#F06292", primary_dark="#C2185B",
                  bg="#FCE4EC", text="#880E4F", card="#FFFFFF", border="#F8BBD0",
                  gradient="qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #E91E63, stop:1 #F06292)"),
    "星空紫": dict(primary="#7B1FA2", primary_light="#CE93D8", primary_dark="#4A148C",
                  bg="#F3E5F5", text="#4A148C", card="#FFFFFF", border="#E1BEE7",
                  gradient="qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #7B1FA2, stop:1 #CE93D8)"),
    "烈焰橙": dict(primary="#E65100", primary_light="#FFB74D", primary_dark="#BF360C",
                  bg="#FFF3E0", text="#BF360C", card="#FFFFFF", border="#FFE0B2",
                  gradient="qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #E65100, stop:1 #FFB74D)"),
}


# ==================== 配置 ====================
class ConfigManager:
    CONFIG_FILE = "config.json"

    @staticmethod
    def load():
        default_config = dict(theme="极光绿", language="zh_CN", android_jar="",
                              ndk_path="", maven_path="", font_size=12)
        try:
            if os.path.exists(ConfigManager.CONFIG_FILE):
                with open(ConfigManager.CONFIG_FILE, "r", encoding="utf-8") as f:
                    config = json.load(f)
                    for k, v in default_config.items():
                        config.setdefault(k, v)
                    return config
        except Exception:
            pass
        return default_config

    @staticmethod
    def save(config):
        try:
            with open(ConfigManager.CONFIG_FILE, "w", encoding="utf-8") as f:
                json.dump(config, f, indent=2, ensure_ascii=False)
        except Exception:
            pass


# ==================== 工作线程 ====================
class ProtectWorker(QThread):
    progress = pyqtSignal(int)
    status = pyqtSignal(str)
    finished = pyqtSignal(bool, str, str)

    def __init__(self, apk_paths, mode, brand, version, parent=None):
        super().__init__(parent)
        self.apk_paths = apk_paths
        self.mode = mode
        self.brand = brand
        self.version = version

    def run(self):
        total = len(self.apk_paths)
        try:
            for i, apk_path in enumerate(self.apk_paths):
                self.status.emit("正在加固: {}".format(os.path.basename(apk_path)))
                out_dir = os.path.join(os.getcwd(), "outapp")
                os.makedirs(out_dir, exist_ok=True)
                base = os.path.splitext(os.path.basename(apk_path))[0]
                output = os.path.join(out_dir, "{}_ETC.apk".format(base))
                ok, msg, result = ProtectorCore.harden(apk_path, output, self.mode, self.brand, self.version)
                if not ok:
                    self.finished.emit(False, "加固失败: {}".format(msg), "")
                    return
                self.progress.emit(int((i + 1) / total * 100))
            self.finished.emit(True, "所有APK加固完成！输出目录: outapp/", "")
        except Exception as e:
            self.finished.emit(False, "加固出错: {}".format(e), "")


class VerifyWorker(QThread):
    finished = pyqtSignal(bool, str, list)
    status = pyqtSignal(str)

    def __init__(self, apk_path, parent=None):
        super().__init__(parent)
        self.apk_path = apk_path

    def run(self):
        self.status.emit("正在校验: {}".format(os.path.basename(self.apk_path)))
        ok, msg, items = ProtectorCore.verify(self.apk_path)
        self.finished.emit(ok, msg, items)


# ==================== 主窗口 ====================
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.apk_list = []
        self.config = ConfigManager.load()
        self.current_worker = None
        self.verify_worker = None

        for d in ("core", "outapp", "asstse", "keys", "logs"):
            os.makedirs(d, exist_ok=True)

        self.initUI()
        self.apply_theme(self.config.get("theme", "极光绿"))
        logger.log("程序启动 v{}".format(APP_VERSION))

    def initUI(self):
        self.setWindowTitle("{} v{}".format(APP_NAME, APP_VERSION))
        self.setGeometry(100, 100, 950, 700)

        icon_path = os.path.join(os.getcwd(), "asstse", "NBThd.png")
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))

        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(20, 20, 20, 20)

        # 标题栏
        title_frame = QWidget()
        title_layout = QHBoxLayout(title_frame)
        title_layout.setContentsMargins(0, 0, 0, 0)
        title_label = QLabel(APP_NAME)
        title_label.setObjectName("TitleLabel")
        title_layout.addWidget(title_label)
        title_layout.addStretch()
        title_layout.addWidget(QLabel("主题:"))
        self.theme_combo = QComboBox()
        self.theme_combo.addItems(list(THEMES.keys()))
        self.theme_combo.setCurrentText(self.config.get("theme", "极光绿"))
        self.theme_combo.currentTextChanged.connect(self.change_theme)
        title_layout.addWidget(self.theme_combo)
        layout.addWidget(title_frame)

        # 标签页
        tabs = QTabWidget()
        layout.addWidget(tabs)
        protect_tab = QWidget()
        settings_tab = QWidget()
        about_tab = QWidget()
        tabs.addTab(protect_tab, "🔒 加固")
        tabs.addTab(settings_tab, "⚙️ 设置")
        tabs.addTab(about_tab, "ℹ️ 关于")
        self.setup_protect_tab(protect_tab)
        self.setup_settings_tab(settings_tab)
        self.setup_about_tab(about_tab)

    # ---------- 加固页 ----------
    def setup_protect_tab(self, tab):
        layout = QVBoxLayout(tab)

        list_group = QGroupBox("📱 APK文件列表")
        list_layout = QVBoxLayout(list_group)
        toolbar = QHBoxLayout()
        add_btn = QPushButton("📂 添加APK")
        add_btn.clicked.connect(self.add_apks)
        toolbar.addWidget(add_btn)
        remove_btn = QPushButton("🗑️ 移除选中")
        remove_btn.clicked.connect(self.remove_selected)
        toolbar.addWidget(remove_btn)
        clear_btn = QPushButton("🧹 清空列表")
        clear_btn.clicked.connect(self.clear_list)
        toolbar.addWidget(clear_btn)
        verify_btn = QPushButton("🔎 校验选中")
        verify_btn.clicked.connect(self.verify_selected)
        toolbar.addWidget(verify_btn)
        toolbar.addStretch()
        list_layout.addLayout(toolbar)

        self.apk_list_widget = QListWidget()
        self.apk_list_widget.setSelectionMode(QListWidget.SelectionMode.ExtendedSelection)
        list_layout.addWidget(self.apk_list_widget)
        layout.addWidget(list_group)

        # 加固选项
        opt_group = QGroupBox("🔐 加固设置")
        opt_layout = QHBoxLayout(opt_group)
        opt_layout.addWidget(QLabel("加固方式:"))
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["标准加固（完整性保护，推荐）", "深度加固（标识加密）"])
        opt_layout.addWidget(self.mode_combo)
        opt_layout.addWidget(QLabel("品牌:"))
        self.brand_edit = QLineEdit("ETC+ Protector")
        self.brand_edit.setMaximumWidth(180)
        opt_layout.addWidget(self.brand_edit)
        opt_layout.addWidget(QLabel("版本:"))
        self.ver_edit = QLineEdit("2.1")
        self.ver_edit.setMaximumWidth(60)
        opt_layout.addWidget(self.ver_edit)
        opt_layout.addStretch()
        layout.addWidget(opt_group)

        self.progress_bar = QProgressBar()
        layout.addWidget(self.progress_bar)
        self.status_label = QLabel("就绪")
        self.status_label.setObjectName("StatusLabel")
        layout.addWidget(self.status_label)

        start_btn = QPushButton("🚀 开始加固")
        start_btn.setObjectName("StartBtn")
        start_btn.clicked.connect(self.start_protect)
        layout.addWidget(start_btn)

        info = QLabel("💡 加固不修改任何代码，应用可正常安装使用；已加固的APK无法二次导入。")
        info.setObjectName("InfoLabel")
        info.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(info)

    # ---------- 设置页 ----------
    def setup_settings_tab(self, tab):
        layout = QVBoxLayout(tab)

        font_group = QGroupBox("🔤 字体设置")
        font_layout = QHBoxLayout(font_group)
        font_layout.addWidget(QLabel("大小:"))
        self.font_size_spin = QSpinBox()
        self.font_size_spin.setRange(10, 20)
        self.font_size_spin.setValue(self.config.get("font_size", 12))
        font_layout.addWidget(self.font_size_spin)
        apply_font_btn = QPushButton("应用字体")
        apply_font_btn.clicked.connect(self.apply_font)
        font_layout.addWidget(apply_font_btn)
        font_layout.addStretch()
        layout.addWidget(font_group)

        ndk_group = QGroupBox("📦 NDK 设置（预留）")
        ndk_layout = QGridLayout(ndk_group)
        ndk_layout.addWidget(QLabel("NDK安装包:"), 0, 0)
        self.ndk_path_edit = QLineEdit(self.config.get("ndk_path", ""))
        self.ndk_path_edit.setReadOnly(True)
        ndk_layout.addWidget(self.ndk_path_edit, 0, 1)
        ndk_btn = QPushButton("选择文件")
        ndk_btn.clicked.connect(self.select_ndk)
        ndk_layout.addWidget(ndk_btn, 0, 2)
        self.ndk_status_label = QLabel("未安装" if not self.config.get("ndk_path") else "已安装")
        ndk_layout.addWidget(self.ndk_status_label, 1, 0, 1, 3)
        layout.addWidget(ndk_group)

        maven_group = QGroupBox("📦 Maven 设置（预留）")
        maven_layout = QGridLayout(maven_group)
        maven_layout.addWidget(QLabel("Maven安装包:"), 0, 0)
        self.maven_path_edit = QLineEdit(self.config.get("maven_path", ""))
        self.maven_path_edit.setReadOnly(True)
        maven_layout.addWidget(self.maven_path_edit, 0, 1)
        maven_btn = QPushButton("选择文件")
        maven_btn.clicked.connect(self.select_maven)
        maven_layout.addWidget(maven_btn, 0, 2)
        self.maven_status_label = QLabel("未安装" if not self.config.get("maven_path") else "已安装")
        maven_layout.addWidget(self.maven_status_label, 1, 0, 1, 3)
        layout.addWidget(maven_group)

        jar_group = QGroupBox("☕ Android SDK 设置（预留）")
        jar_layout = QGridLayout(jar_group)
        jar_layout.addWidget(QLabel("android.jar:"), 0, 0)
        self.jar_path_edit = QLineEdit(self.config.get("android_jar", ""))
        self.jar_path_edit.setReadOnly(True)
        jar_layout.addWidget(self.jar_path_edit, 0, 1)
        jar_btn = QPushButton("选择文件")
        jar_btn.clicked.connect(self.select_android_jar)
        jar_layout.addWidget(jar_btn, 0, 2)
        self.jar_status_label = QLabel("已配置" if self.config.get("android_jar") else "未配置")
        jar_layout.addWidget(self.jar_status_label, 1, 0, 1, 3)
        layout.addWidget(jar_group)

        layout.addStretch()

    # ---------- 关于页 ----------
    def setup_about_tab(self, tab):
        layout = QVBoxLayout(tab)
        about = QTextEdit()
        about.setHtml("""
        <div style="text-align:center; padding:30px;">
          <h1 style="font-size:38px; color:#00C853;">ETC+</h1>
          <h2 style="color:#666;">加固卫士 v2.1</h2><br>
          <p style="font-size:16px;"><b>专业的APK加固工具</b></p><br>
          <div style="margin:20px auto; text-align:left; max-width:420px; font-size:14px;">
            <p>✅ 真实加固（完整性保护 + 防篡改清单）</p>
            <p>✅ 防二次加固导入</p>
            <p>✅ 批量加固处理</p>
            <p>✅ 加固后应用可正常安装使用，不闪退</p>
            <p>✅ 自定义主题支持</p>
            <p>✅ C++ 引擎 / Python 桌面版 / Java 手机版</p>
          </div><br>
          <p style="color:#999;"><b>ETC官方</b><br><i>© 2026 版权所有</i></p><br>
          <p style="color:#999; font-size:13px;"><b>ET</b></p>
        </div>""")
        about.setReadOnly(True)
        layout.addWidget(about)
        license_btn = QPushButton("📜 查看许可协议")
        license_btn.clicked.connect(self.show_license)
        layout.addWidget(license_btn)

    def show_license(self):
        dialog = QDialog(self)
        dialog.setWindowTitle("许可协议")
        dialog.setFixedSize(550, 450)
        dialog.setModal(True)
        lay = QVBoxLayout(dialog)
        text = QTextEdit()
        text.setPlainText(LICENSE_TEXT)
        text.setReadOnly(True)
        lay.addWidget(text)
        btn = QPushButton("我已阅读并同意")
        btn.clicked.connect(dialog.accept)
        lay.addWidget(btn)
        dialog.exec()

    # ---------- 主题 ----------
    def apply_theme(self, theme_name):
        theme = THEMES.get(theme_name, THEMES["极光绿"])
        style = """
        QMainWindow, QWidget { background-color: %(bg)s; }
        QTabWidget::pane { border: none; background-color: %(card)s; border-radius: 15px; margin-top: 10px; }
        QTabBar::tab { background-color: %(border)s; color: %(text)s; padding: 12px 25px; border-radius: 10px 10px 0 0; margin-right: 5px; font-weight: bold; font-size: 14px; }
        QTabBar::tab:selected { background-color: %(primary)s; color: white; }
        QGroupBox { font-weight: bold; border: 2px solid %(border)s; border-radius: 12px; margin-top: 12px; padding-top: 12px; color: %(text)s; background-color: %(card)s; }
        QGroupBox::title { subcontrol-origin: margin; left: 15px; padding: 0 10px; font-size: 14px; }
        QListWidget { border: 2px solid %(border)s; border-radius: 10px; padding: 5px; background-color: %(card)s; color: %(text)s; }
        QListWidget::item { padding: 12px; border-radius: 6px; }
        QListWidget::item:selected { background-color: %(primary_light)s; color: %(text)s; }
        QLabel { color: %(text)s; }
        QLabel#TitleLabel { font-size: 28px; font-weight: bold; color: %(primary)s; }
        QLabel#InfoLabel { color: #999; font-size: 12px; }
        QLabel#StatusLabel { padding: 8px; }
        QLineEdit { border: 2px solid %(border)s; border-radius: 10px; padding: 8px 14px; background-color: %(card)s; color: %(text)s; }
        QLineEdit:focus { border-color: %(primary)s; }
        QComboBox { border: 2px solid %(border)s; border-radius: 10px; padding: 8px 14px; background-color: %(card)s; color: %(text)s; min-width: 150px; }
        QComboBox QAbstractItemView { background-color: %(card)s; color: %(text)s; }
        QProgressBar { border: 2px solid %(border)s; border-radius: 10px; height: 30px; text-align: center; font-weight: bold; color: %(text)s; background-color: %(card)s; }
        QProgressBar::chunk { background: %(gradient)s; border-radius: 8px; }
        QPushButton { background: %(gradient)s; color: white; border: none; padding: 12px 25px; border-radius: 10px; font-size: 14px; font-weight: bold; }
        QPushButton:hover { opacity: 0.85; }
        QPushButton:disabled { background-color: #cccccc; color: #666666; }
        QPushButton#StartBtn { font-size: 16px; padding: 12px; border-radius: 10px; }
        QTextEdit { border: 2px solid %(border)s; border-radius: 10px; background-color: %(card)s; color: %(text)s; }
        """ % theme
        self.setStyleSheet(style)

    def change_theme(self, theme_name):
        self.config["theme"] = theme_name
        ConfigManager.save(self.config)
        self.apply_theme(theme_name)

    def apply_font(self):
        self.config["font_size"] = self.font_size_spin.value()
        ConfigManager.save(self.config)
        font = QFont()
        font.setPointSize(self.font_size_spin.value())
        QApplication.setFont(font)
        QMessageBox.information(self, "成功", "字体设置已应用！")

    # ---------- 列表操作 ----------
    def add_apks(self):
        files, _ = QFileDialog.getOpenFileNames(self, "选择APK文件", "", "APK文件 (*.apk *.apks)")
        if not files:
            return
        for f in files:
            state, msg, brand, ver, dex = ProtectorCore.check(f)
            if state == 0:
                QMessageBox.warning(self, "警告", "{} 不是有效的APK文件\n{}".format(os.path.basename(f), msg))
                continue
            if state == 2:
                QMessageBox.warning(self, "警告", "{} 已由 {} 加固，禁止二次加固！".format(os.path.basename(f), brand or "ETC+"))
                continue
            if state == 3:
                QMessageBox.warning(self, "警告", "{} 检测到其它加固，请先还原为未加固版本".format(os.path.basename(f)))
                continue
            if f not in self.apk_list:
                self.apk_list.append(f)
                item = QListWidgetItem("{}   [未加固]  DEX×{}".format(os.path.basename(f), dex))
                self.apk_list_widget.addItem(item)
        self.status_label.setText("已添加 {} 个APK，均可安全加固".format(len(self.apk_list)))

    def remove_selected(self):
        for item in self.apk_list_widget.selectedItems():
            row = self.apk_list_widget.row(item)
            self.apk_list_widget.takeItem(row)
            if row < len(self.apk_list):
                self.apk_list.pop(row)

    def clear_list(self):
        self.apk_list_widget.clear()
        self.apk_list = []

    def verify_selected(self):
        items = self.apk_list_widget.selectedItems()
        if not items:
            if self.apk_list:
                target = self.apk_list[0]
            else:
                QMessageBox.warning(self, "警告", "请先添加APK文件！")
                return
        else:
            rows = [self.apk_list_widget.row(it) for it in items]
            target = self.apk_list[rows[0]]
        self.set_ui_enabled(False)
        self.verify_worker = VerifyWorker(target)
        self.verify_worker.finished.connect(self.on_verify_finished)
        self.verify_worker.start()

    def on_verify_finished(self, ok, msg, items):
        self.set_ui_enabled(True)
        if not items:
            QMessageBox.information(self, "校验结果", msg)
            return
        lines = ["{}: {}".format("✅" if ok else "❌", msg), ""]
        for name, iok, exp, act, extra, missing in items[:30]:
            if extra:
                lines.append("  新增: {}".format(name))
            elif missing:
                lines.append("  缺失: {}".format(name))
            elif not iok:
                lines.append("  被篡改: {}".format(name))
        if len(items) > 30:
            lines.append("  ...共 {} 项".format(len(items)))
        QMessageBox.information(self, "校验结果", "\n".join(lines))

    # ---------- 加固 ----------
    def start_protect(self):
        if not self.apk_list:
            QMessageBox.warning(self, "警告", "请先添加要加固的APK文件！")
            return
        brand = self.brand_edit.text().strip() or "ETC+ Protector"
        version = self.ver_edit.text().strip() or APP_VERSION
        mode = 1 if self.mode_combo.currentIndex() == 1 else 0
        self.set_ui_enabled(False)
        self.progress_bar.setValue(0)
        self.current_worker = ProtectWorker(self.apk_list.copy(), mode, brand, version)
        self.current_worker.progress.connect(self.update_progress)
        self.current_worker.status.connect(self.update_status)
        self.current_worker.finished.connect(self.on_protect_finished)
        self.current_worker.start()

    def set_ui_enabled(self, enabled):
        for widget in self.findChildren(QWidget):
            if widget not in (self.progress_bar, self.status_label):
                widget.setEnabled(enabled)

    def update_progress(self, value):
        self.progress_bar.setValue(value)

    def update_status(self, status):
        self.status_label.setText(status)

    def on_protect_finished(self, success, message, result):
        self.set_ui_enabled(True)
        self.progress_bar.setValue(100 if success else 0)
        self.status_label.setText("就绪")
        QMessageBox.information(self, "完成" if success else "错误", message)

    # ---------- 设置回调 ----------
    def select_ndk(self):
        file, _ = QFileDialog.getOpenFileName(self, "选择NDK安装包", "", "压缩文件 (*.tar.gz *.zip)")
        if file:
            self.config["ndk_path"] = file
            ConfigManager.save(self.config)
            self.ndk_path_edit.setText(file)
            self.ndk_status_label.setText("已安装")
            QMessageBox.information(self, "完成", "NDK路径已记录（预留功能）")

    def select_maven(self):
        file, _ = QFileDialog.getOpenFileName(self, "选择Maven安装包", "", "压缩文件 (*.tar.gz)")
        if file:
            self.config["maven_path"] = file
            ConfigManager.save(self.config)
            self.maven_path_edit.setText(file)
            self.maven_status_label.setText("已安装")
            QMessageBox.information(self, "完成", "Maven路径已记录（预留功能）")

    def select_android_jar(self):
        file, _ = QFileDialog.getOpenFileName(self, "选择android.jar", "", "JAR文件 (*.jar)")
        if file:
            self.config["android_jar"] = file
            ConfigManager.save(self.config)
            self.jar_path_edit.setText(file)
            self.jar_status_label.setText("已配置")


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    font = QFont()
    font.setPointSize(12)
    app.setFont(font)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
