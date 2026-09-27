# ETC+ 加固卫士 (ETC+ Protector)

> 真正可用的 APK 加固工具 · 三语言重构版（C++ / Python / Java）
> 版本 v2.1 · © 2026 ETC官方

## 简介

ETC+ 加固卫士是一款面向 Android APK 的**防篡改加固工具**，用 C++、Python、Java 三种语言重构实现：

- **C++ 核心引擎**（`engine/`）：SHA-256 全文件校验、加固、篡改检测，三端共用同一套逻辑与密钥；
- **Python 桌面版**（`desktop_python/`）：PyQt6 界面，镜像原版「加固 / 设置 / 关于」三标签 + 5 主题；
- **原生 Win32 C++ 桌面版**（`desktop_cpp_win32/`）：无需 Python 运行时，直接双击运行；
- **Java Android 手机版**（`android/`）：原生 Java + 系统框架，手机上即可完成加固。

## 加固原理（真实有效，不损坏应用）

1. **不修改任何 DEX**——不注入壳、不篡改字节码，应用安装后 100% 正常使用、不闪退；
2. **全文件 SHA-256 清单**——加固时计算 APK 内所有文件的哈希并加密写入 `assets/etc_integrity.json`，任何文件被篡改都能被 `检测` 功能发现；
3. **品牌标识注入**——写入 `assets/etc_protect.etc` 与 `META-INF/ETCPLUS.SF`，MT 管理器与系统「加固状态」字段将显示 **ETC+ Protector**；
4. **禁止二次加固**——对已加固的 APK 导入时会检测到保护标识并拒绝，防止重复处理导致问题；
5. **输出全新文件**——加固产物为新 APK，自动 zipalign + v2/v3 签名，可直接安装。

## 构建与使用

### 构建 EXE（Windows 桌面版）

```bash
bash scripts/build_windows_exe.sh
# 产物：out/ETC_Protector.exe（原生 PE32+，仅依赖系统 DLL）
```

### 构建 APK（Android 手机版）

```bash
bash scripts/build_apk.sh
# 产物：android/out/ETC_Protector.apk（com.etc.protector, v2.1, minSdk 24）
```

### 命令行引擎（Linux / 自测）

```bash
g++ -O2 -std=c++17 engine/main_cli.cpp engine/etc_engine.cpp \
  engine/third_party/miniz.c engine/third_party/miniz_tdef.c \
  engine/third_party/miniz_tinfl.c engine/third_party/miniz_zip.c \
  -o out/etc_protector_cli
./out/etc_protector_cli check 你的.apk    # 检测加固状态
./out/etc_protector_cli harden 你的.apk   # 加固
./out/etc_protector_cli verify 你的.apk   # 校验完整性
```

## 验证结果

| 项目 | 结果 |
|---|---|
| C++ 引擎（check/harden/verify/二次加固拒绝） | 全部通过 |
| EXE 编译 | PE32+ GUI，静态链接，仅系统 DLL |
| APK 签名 | v2 + v3 方案验证通过 |
| 加固后安装运行 | 不闪退，正常使用 |
| 篡改检测 | 修改任意文件后 verify 失败 |
| 加固状态字段 | 显示「ETC+ Protector」 |

## 关于杀毒软件误报

本项目桌面版采用**原生 C++ 静态编译**（非 PyInstaller 脚本打包、无运行时解释器、体积小），
误报概率远低于脚本打包工具。若 360 等仍提示风险，请见 `docs/AV-误报说明.md` 的处理方案。

## 目录结构

```
engine/               C++ 核心引擎（sha256 + miniz + 加固逻辑）
desktop_python/       Python/PyQt6 桌面版
desktop_cpp_win32/    原生 Win32 C++ 桌面版
android/              Java Android 手机版（含构建脚本产物）
scripts/              构建流水线（EXE / APK）
docs/                 官网页面、截图、误报说明
out/                  构建产物（EXE 等）
```

## 许可

见 LICENSE。本工具仅供合法用途，禁止用于破解、盗版等非法行为。
