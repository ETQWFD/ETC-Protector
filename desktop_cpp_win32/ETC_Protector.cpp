// ETC_Protector.cpp - ETC+ 加固卫士 Windows 版（C++ / Win32）
// 原生编译（非 PyInstaller），误报率远低于脚本打包；内嵌 C++ 加固引擎
#define UNICODE
#define _UNICODE
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include "etc_engine.h"

// ---------------- 资源 ID ----------------
#define IDI_ICON1                   101
#define IDC_TABS                    1001
#define IDC_LV_APKS                 1002
#define IDC_BTN_ADD                 1003
#define IDC_BTN_REMOVE              1004
#define IDC_BTN_CLEAR               1005
#define IDC_BTN_VERIFY              1006
#define IDC_COMBO_MODE              1007
#define IDC_EDIT_BRAND              1008
#define IDC_EDIT_VER                1009
#define IDC_PROGRESS                1010
#define IDC_STATUS                  1011
#define IDC_BTN_START               1012
#define IDC_COMBO_THEME             1013
#define IDC_EDIT_FONTSIZE           1014
#define IDC_SPIN_FONT               1015
#define IDC_BTN_FONT                1016
#define IDC_EDIT_NDK                1017
#define IDC_BTN_NDK                 1018
#define IDC_EDIT_MAVEN              1019
#define IDC_BTN_MAVEN               1020
#define IDC_EDIT_JAR                1021
#define IDC_BTN_JAR                 1022
#define IDC_BTN_LICENSE             1023
#define IDC_ST_THEME_LBL            1024
#define IDC_ST_NDK_ST                1025
#define IDC_ST_MAVEN_ST             1026
#define IDC_ST_JAR_ST               1027
#define IDC_ST_FONT_LBL             1028
#define IDC_ST_INFO                 1029

// 自定义消息（工作线程 -> UI）
#define WM_APP_PROGRESS             (WM_APP + 1)
#define WM_APP_STATUS               (WM_APP + 2)
#define WM_APP_FINISH               (WM_APP + 3)

// ---------------- 工具函数 ----------------
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

static std::string wide_to_utf8(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len, nullptr, nullptr);
    return out;
}

static std::wstring exe_dir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

static void ensure_dir(const std::wstring& d) {
    CreateDirectoryW(d.c_str(), nullptr);
}

// ---------------- 主题 ----------------
struct Theme {
    const wchar_t* name;
    COLORREF primary, primary_light, bg, text, card, border;
};

static const Theme THEMES[] = {
    {L"极光绿", RGB(0,200,83), RGB(105,240,174), RGB(241,248,233), RGB(27,94,32), RGB(255,255,255), RGB(200,230,201)},
    {L"深海蓝", RGB(21,101,192), RGB(100,181,246), RGB(227,242,253), RGB(13,71,161), RGB(255,255,255), RGB(187,222,251)},
    {L"樱花粉", RGB(233,30,99), RGB(240,98,146), RGB(252,228,236), RGB(136,14,79), RGB(255,255,255), RGB(248,187,208)},
    {L"星空紫", RGB(123,31,162), RGB(206,147,216), RGB(243,229,245), RGB(74,20,140), RGB(255,255,255), RGB(225,190,231)},
    {L"烈焰橙", RGB(230,81,0), RGB(255,183,77), RGB(255,243,224), RGB(191,54,12), RGB(255,255,255), RGB(255,224,178)},
};
static const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

// ---------------- 配置（key=value 简单格式） ----------------
struct Config {
    int theme = 0;
    std::wstring brand = L"ETC+加固";
    std::wstring version = L"2.1";
    int font_size = 12;
    std::wstring ndk, maven, jar;
};

static Config g_config;
static std::wstring g_cfg_path;

static std::string read_all_bytes(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static void write_all_bytes(const std::wstring& path, const std::string& s) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

static void load_config() {
    g_cfg_path = exe_dir() + L"\\etc_config.txt";
    std::string raw = read_all_bytes(g_cfg_path);
    std::wstring content = utf8_to_wide(raw);
    std::wistringstream ss(content);
    std::wstring line;
    while (std::getline(ss, line)) {
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == L"theme") g_config.theme = _wtoi(v.c_str());
        else if (k == L"brand") g_config.brand = v;
        else if (k == L"version") g_config.version = v;
        else if (k == L"font_size") g_config.font_size = _wtoi(v.c_str());
        else if (k == L"ndk") g_config.ndk = v;
        else if (k == L"maven") g_config.maven = v;
        else if (k == L"jar") g_config.jar = v;
    }
}

static void save_config() {
    std::wstring out = L"theme=" + std::to_wstring(g_config.theme) + L"\n"
        + L"brand=" + g_config.brand + L"\n"
        + L"version=" + g_config.version + L"\n"
        + L"font_size=" + std::to_wstring(g_config.font_size) + L"\n"
        + L"ndk=" + g_config.ndk + L"\n"
        + L"maven=" + g_config.maven + L"\n"
        + L"jar=" + g_config.jar + L"\n";
    write_all_bytes(g_cfg_path, wide_to_utf8(out));
}

// ---------------- 日志 ----------------
static void log_msg(const std::wstring& msg) {
    std::wstring dir = exe_dir() + L"\\logs";
    ensure_dir(dir);
    time_t t = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &t);
    wchar_t ts[64];
    wcsftime(ts, 64, L"%Y-%m-%d %H:%M:%S", &tm);
    std::wstring line = L"[" + std::wstring(ts) + L"] " + msg + L"\n";
    write_all_bytes(dir + L"\\log.txt", wide_to_utf8(line));
}

// ---------------- 应用状态 ----------------
struct AppState {
    HINSTANCE hInst = nullptr;
    HWND hwndMain = nullptr;
    HWND hwndTabs = nullptr;
    HWND hwndTab1 = nullptr, hwndTab2 = nullptr, hwndTab3 = nullptr;
    HWND lv = nullptr;
    HWND comboMode = nullptr, editBrand = nullptr, editVer = nullptr;
    HWND progress = nullptr, status = nullptr, btnStart = nullptr, comboTheme = nullptr;
    HWND editFontSize = nullptr, spinFont = nullptr;
    HWND editNdk = nullptr, editMaven = nullptr, editJar = nullptr;
    HWND stNdk = nullptr, stMaven = nullptr, stJar = nullptr;
    std::vector<std::wstring> apkFiles;
    std::vector<std::wstring> apkStates;
    int themeIdx = 0;
    std::atomic<bool> busy{false};
    HBRUSH brushBg = nullptr, brushCard = nullptr, brushBtn = nullptr;
    HFONT hFont = nullptr;
};

static AppState g;

// ---------------- 引擎桥接 ----------------
static void refresh_list();

static std::wstring status_text_from_state(int st, int dex) {
    switch (st) {
        case 1: return L"[未加固]  DEX×" + std::to_wstring(dex);
        case 2: return L"[已加固 ETC+]  DEX×" + std::to_wstring(dex);
        case 3: return L"[其它加固]  DEX×" + std::to_wstring(dex);
        default: return L"[无效]";
    }
}

static void add_apk(const std::wstring& path) {
    etc::CheckResult cr = etc::check_apk(wide_to_utf8(path));
    if (cr.state == etc::ApkState::NOT_APK) {
        MessageBoxW(g.hwndMain, (L"不是有效的APK文件\n" + utf8_to_wide(cr.message)).c_str(), L"警告", MB_ICONWARNING);
        return;
    }
    if (cr.state == etc::ApkState::ETC_PROTECTED) {
        MessageBoxW(g.hwndMain, (L"该APK已由 " + utf8_to_wide(cr.brand) + L" 加固，禁止二次加固！").c_str(), L"警告", MB_ICONWARNING);
        return;
    }
    if (cr.state == etc::ApkState::UNKNOWN) {
        MessageBoxW(g.hwndMain, (L"检测到其它加固特征，请先还原为未加固版本\n" + utf8_to_wide(cr.message)).c_str(), L"警告", MB_ICONWARNING);
        return;
    }
    size_t pos = path.find_last_of(L"\\/");
    std::wstring name = pos == std::wstring::npos ? path : path.substr(pos + 1);
    g.apkFiles.push_back(path);
    g.apkStates.push_back(status_text_from_state((int)cr.state, cr.dex_count));
    refresh_list();
    SetWindowTextW(g.status, (L"已添加 " + std::to_wstring(g.apkFiles.size()) + L" 个APK，均可安全加固").c_str());
}

// ---------------- 签名（可选） ----------------
static bool find_in_path(const std::wstring& exe, std::wstring& out) {
    wchar_t buf[4096];
    DWORD n = GetEnvironmentVariableW(L"PATH", buf, 4096);
    if (!n) return false;
    std::wistringstream ss(buf);
    std::wstring dir;
    while (std::getline(ss, dir, L';')) {
        if (dir.empty()) continue;
        std::wstring p = dir + L"\\" + exe;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) { out = p; return true; }
        p = dir + L"\\" + exe + L".exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) { out = p; return true; }
    }
    return false;
}

static bool run_wait(const std::wstring& cmd) {
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    std::wstring cl = cmd;
    if (!CreateProcessW(nullptr, &cl[0], nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

// 尝试签名：找到 apksigner/jarsigner 则签名；找不到则返回 false（不阻塞）
static bool try_sign_apk(const std::wstring& apk) {
    std::wstring exe_dir_ = exe_dir();
    std::wstring ks = exe_dir_ + L"\\keys\\debug.keystore";
    if (GetFileAttributesW(ks.c_str()) == INVALID_FILE_ATTRIBUTES) {
        ensure_dir(exe_dir_ + L"\\keys");
        std::wstring keytool;
        if (!find_in_path(L"keytool", keytool)) return false;
        std::wstring cmd = L"\"" + keytool + L"\" -genkey -v -keystore \"" + ks +
            L"\" -alias debugkey -keyalg RSA -keysize 2048 -validity 10000 " +
            L"-storepass android -keypass android -dname \"CN=ETC Protector, O=ETC, C=CN\"";
        run_wait(cmd);
    }
    std::wstring apksigner;
    if (find_in_path(L"apksigner", apksigner)) {
        std::wstring cmd = L"\"" + apksigner + L"\" sign --ks \"" + ks +
            L"\" --ks-pass pass:android --key-pass pass:android \"" + apk + L"\"";
        if (run_wait(cmd)) return true;
    }
    std::wstring jarsigner;
    if (find_in_path(L"jarsigner", jarsigner)) {
        std::wstring cmd = L"\"" + jarsigner + L"\" -keystore \"" + ks +
            L"\" -storepass android -keypass android \"" + apk + L"\" debugkey";
        if (run_wait(cmd)) return true;
    }
    return false;
}

// ---------------- 工作线程 ----------------
struct WorkerJob {
    std::vector<std::wstring> inputs;
    int mode = 0;
    std::wstring brand, version;
};

static void worker_run(WorkerJob* job);

static DWORD WINAPI WorkerThreadProc(LPVOID p) {
    WorkerJob* job = (WorkerJob*)p;
    worker_run(job);
    return 0;
}

static void worker_run(WorkerJob* job) {
    size_t total = job->inputs.size();
    bool all_ok = true;
    std::wstring lastMsg = L"所有APK加固完成！输出目录: outapp\\";
    std::wstring outDir = exe_dir() + L"\\outapp";
    ensure_dir(outDir);
    for (size_t i = 0; i < total; i++) {
        const std::wstring& in = job->inputs[i];
        std::wstring base = in;
        size_t dot = base.find_last_of(L'.');
        size_t slash = base.find_last_of(L"\\/");
        if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
            base = base.substr(0, dot);
        size_t nm = base.find_last_of(L"\\/");
        std::wstring shortName = nm == std::wstring::npos ? base : base.substr(nm + 1);
        std::wstring out = outDir + L"\\" + shortName + L"_ETC.apk";

        std::wstring stMsg = L"正在加固: " + shortName + L".apk";
        wchar_t* pSt = new wchar_t[stMsg.size() + 1];
        wcscpy(pSt, stMsg.c_str());
        PostMessageW(g.hwndMain, WM_APP_STATUS, 0, (LPARAM)pSt);

        etc::HardenOptions opt;
        opt.mode = job->mode;
        opt.brand = wide_to_utf8(job->brand);
        opt.version = wide_to_utf8(job->version);
        etc::HardenResult hr = etc::harden_apk(wide_to_utf8(in), wide_to_utf8(out), opt);
        if (hr.ok) {
            bool signed_ = try_sign_apk(out);
            if (!signed_) {
                lastMsg = L"加固完成（未签名：未检测到 apksigner/jarsigner，请在MT管理器/NP管理器中签名后安装）\n输出目录: outapp\\";
            }
        } else {
            all_ok = false;
            lastMsg = L"加固失败: " + utf8_to_wide(hr.message);
        }
        PostMessageW(g.hwndMain, WM_APP_PROGRESS, (int)((i + 1) * 100 / total), 0);
    }
    delete job;
    struct FinishMsg { bool ok; std::wstring text; };
    FinishMsg* fm = new FinishMsg{ all_ok, lastMsg };
    PostMessageW(g.hwndMain, WM_APP_FINISH, 0, (LPARAM)fm);
}

// ---------------- 列表刷新 ----------------
static void refresh_list() {
    if (!g.lv) return;
    ListView_DeleteAllItems(g.lv);
    for (size_t i = 0; i < g.apkFiles.size(); i++) {
        size_t pos = g.apkFiles[i].find_last_of(L"\\/");
        std::wstring name = pos == std::wstring::npos ? g.apkFiles[i] : g.apkFiles[i].substr(pos + 1);
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        it.iSubItem = 0;
        it.pszText = (LPWSTR)name.c_str();
        ListView_InsertItem(g.lv, &it);
        ListView_SetItemText(g.lv, (int)i, 1, (LPWSTR)g.apkStates[i].c_str());
        ListView_SetItemText(g.lv, (int)i, 2, (LPWSTR)L"ETC+ 引擎");
    }
}

// ---------------- 校验 ----------------
static void do_verify(int row) {
    if (row < 0 || row >= (int)g.apkFiles.size()) return;
    etc::VerifyResult vr = etc::verify_apk(wide_to_utf8(g.apkFiles[row]));
    std::wstring text = vr.protected_
        ? (vr.ok ? L"✅ " : L"❌ ") + utf8_to_wide(vr.message) + L"\n\n"
        : L"该APK不是ETC+加固产物，无法校验";
    if (vr.protected_ && !vr.items.empty()) {
        int cnt = 0;
        for (auto& it : vr.items) {
            if (it.extra) text += L"\n  新增: " + utf8_to_wide(it.name);
            else if (it.missing) text += L"\n  缺失: " + utf8_to_wide(it.name);
            else if (!it.ok) text += L"\n  被篡改: " + utf8_to_wide(it.name);
            if (++cnt > 20) { text += L"\n  ..."; break; }
        }
    }
    MessageBoxW(g.hwndMain, text.c_str(), L"校验结果", MB_ICONINFORMATION | MB_OK);
}

// ---------------- 主题应用 ----------------
static void apply_theme() {
    const Theme& t = THEMES[g.themeIdx];
    if (g.brushBg) DeleteObject(g.brushBg);
    if (g.brushCard) DeleteObject(g.brushCard);
    if (g.brushBtn) DeleteObject(g.brushBtn);
    g.brushBg = CreateSolidBrush(t.bg);
    g.brushCard = CreateSolidBrush(t.card);
    g.brushBtn = CreateSolidBrush(t.primary);
    if (g.hwndMain) {
        InvalidateRect(g.hwndMain, nullptr, TRUE);
        if (g.lv) ListView_SetBkColor(g.lv, t.card);
        if (g.progress) SendMessageW(g.progress, PBM_SETBARCOLOR, 0, t.primary);
    }
    g_config.theme = g.themeIdx;
    save_config();
}

// ---------------- 界面创建 ----------------
static HWND mk_ctl(const wchar_t* cls, const wchar_t* text, DWORD style,
                   HWND parent, int id, int x, int y, int w, int ht) {
    HWND hw = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                              x, y, w, ht, parent, (HMENU)(INT_PTR)id, g.hInst, nullptr);
    if (hw && g.hFont) SendMessageW(hw, WM_SETFONT, (WPARAM)g.hFont, TRUE);
    return hw;
}

static void create_protect_tab(HWND parent) {
    HWND tab = parent;

    mk_ctl(L"BUTTON", L"📂 添加APK", WS_TABSTOP, tab, IDC_BTN_ADD, 12, 8, 100, 34);
    mk_ctl(L"BUTTON", L"🗑️ 移除选中", WS_TABSTOP, tab, IDC_BTN_REMOVE, 118, 8, 100, 34);
    mk_ctl(L"BUTTON", L"🧹 清空列表", WS_TABSTOP, tab, IDC_BTN_CLEAR, 224, 8, 100, 34);
    mk_ctl(L"BUTTON", L"🔎 校验选中", WS_TABSTOP, tab, IDC_BTN_VERIFY, 330, 8, 100, 34);

    g.lv = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        12, 48, 880, 240, tab, (HMENU)(INT_PTR)IDC_LV_APKS, g.hInst, nullptr);
    if (g.hFont) SendMessageW(g.lv, WM_SETFONT, (WPARAM)g.hFont, TRUE);
    LVCOLUMNW c1{ LVCF_TEXT | LVCF_WIDTH | LVCFMT_LEFT, 0, 380, L"文件名" };
    ListView_InsertColumn(g.lv, 0, &c1);
    LVCOLUMNW c2{ LVCF_TEXT | LVCF_WIDTH | LVCFMT_LEFT, 0, 300, L"状态" };
    ListView_InsertColumn(g.lv, 1, &c2);
    LVCOLUMNW c3{ LVCF_TEXT | LVCF_WIDTH | LVCFMT_LEFT, 0, 180, L"引擎" };
    ListView_InsertColumn(g.lv, 2, &c3);
    ListView_SetExtendedListViewStyle(g.lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    mk_ctl(L"STATIC", L"🔐 加固设置", SS_LEFT, tab, -1, 12, 300, 120, 20);

    mk_ctl(L"STATIC", L"加固方式:", SS_RIGHT, tab, -1, 12, 328, 70, 24);
    g.comboMode = mk_ctl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                         tab, IDC_COMBO_MODE, 88, 326, 240, 120);
    SendMessageW(g.comboMode, CB_ADDSTRING, 0, (LPARAM)L"标准加固（完整性保护，推荐）");
    SendMessageW(g.comboMode, CB_ADDSTRING, 0, (LPARAM)L"深度加固（标识加密）");
    SendMessageW(g.comboMode, CB_SETCURSEL, 0, 0);

    mk_ctl(L"STATIC", L"品牌:", SS_RIGHT, tab, -1, 336, 328, 50, 24);
    g.editBrand = mk_ctl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, tab, IDC_EDIT_BRAND, 392, 324, 160, 26);
    SetWindowTextW(g.editBrand, g_config.brand.c_str());

    mk_ctl(L"STATIC", L"版本:", SS_RIGHT, tab, -1, 562, 328, 40, 24);
    g.editVer = mk_ctl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, tab, IDC_EDIT_VER, 606, 324, 56, 26);
    SetWindowTextW(g.editVer, g_config.version.c_str());

    g.progress = mk_ctl(L"msctls_progress32", L"", 0, tab, IDC_PROGRESS, 12, 366, 880, 28);

    g.status = mk_ctl(L"STATIC", L"就绪", SS_LEFT, tab, IDC_STATUS, 12, 402, 880, 22);

    g.btnStart = mk_ctl(L"BUTTON", L"🚀 开始加固", WS_TABSTOP, tab, IDC_BTN_START, 240, 440, 420, 46);
    HFONT big = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
    SendMessageW(g.btnStart, WM_SETFONT, (WPARAM)big, TRUE);

    mk_ctl(L"STATIC",
        L"💡 加固不修改任何代码，应用可正常安装使用；已加固的APK无法二次导入。",
        SS_CENTER, tab, IDC_ST_INFO, 12, 500, 880, 22);
}

static void create_settings_tab(HWND parent) {
    mk_ctl(L"STATIC", L"🔤 字体大小", SS_LEFT, parent, -1, 12, 8, 200, 22);
    mk_ctl(L"STATIC", L"大小:", SS_RIGHT, parent, IDC_ST_FONT_LBL, 12, 38, 50, 24);
    g.editFontSize = mk_ctl(L"EDIT", L"", WS_TABSTOP | ES_NUMBER, parent, IDC_EDIT_FONTSIZE, 66, 36, 50, 26);
    wchar_t fs[8];
    swprintf(fs, 8, L"%d", g_config.font_size);
    SetWindowTextW(g.editFontSize, fs);
    g.spinFont = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT |
        UDS_ALIGNRIGHT | UDS_ARROWKEYS, 0, 0, 0, 0, parent, (HMENU)(INT_PTR)IDC_SPIN_FONT, g.hInst, nullptr);
    SendMessageW(g.spinFont, UDM_SETBUDDY, (WPARAM)g.editFontSize, 0);
    SendMessageW(g.spinFont, UDM_SETRANGE32, 10, 20);
    mk_ctl(L"BUTTON", L"应用字体", WS_TABSTOP, parent, IDC_BTN_FONT, 130, 36, 90, 26);

    mk_ctl(L"STATIC", L"📦 NDK 设置（预留）", SS_LEFT, parent, -1, 12, 90, 240, 22);
    g.editNdk = mk_ctl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY, parent, IDC_EDIT_NDK, 12, 118, 560, 26);
    SetWindowTextW(g.editNdk, g_config.ndk.c_str());
    mk_ctl(L"BUTTON", L"选择文件", WS_TABSTOP, parent, IDC_BTN_NDK, 580, 118, 90, 26);
    g.stNdk = mk_ctl(L"STATIC", L"未安装（预留）", SS_LEFT, parent, IDC_ST_NDK_ST, 12, 150, 300, 20);

    mk_ctl(L"STATIC", L"📦 Maven 设置（预留）", SS_LEFT, parent, -1, 12, 190, 240, 22);
    g.editMaven = mk_ctl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY, parent, IDC_EDIT_MAVEN, 12, 218, 560, 26);
    SetWindowTextW(g.editMaven, g_config.maven.c_str());
    mk_ctl(L"BUTTON", L"选择文件", WS_TABSTOP, parent, IDC_BTN_MAVEN, 580, 218, 90, 26);
    g.stMaven = mk_ctl(L"STATIC", L"未安装（预留）", SS_LEFT, parent, IDC_ST_MAVEN_ST, 12, 250, 300, 20);

    mk_ctl(L"STATIC", L"☕ Android SDK 设置（预留）", SS_LEFT, parent, -1, 12, 290, 240, 22);
    g.editJar = mk_ctl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY, parent, IDC_EDIT_JAR, 12, 318, 560, 26);
    SetWindowTextW(g.editJar, g_config.jar.c_str());
    mk_ctl(L"BUTTON", L"选择文件", WS_TABSTOP, parent, IDC_BTN_JAR, 580, 318, 90, 26);
    g.stJar = mk_ctl(L"STATIC", L"未配置（预留）", SS_LEFT, parent, IDC_ST_JAR_ST, 12, 350, 300, 20);
}

static void create_about_tab(HWND parent) {
    HWND text = mk_ctl(L"STATIC",
        L"ETC+ 加固卫士 v2.1\n\n"
        L"专业的APK加固工具\n\n"
        L"✅ 真实加固（完整性保护 + 防篡改清单）\n"
        L"✅ 防二次加固导入\n"
        L"✅ 批量加固处理\n"
        L"✅ 加固后应用可正常安装使用，不闪退\n"
        L"✅ 自定义主题支持\n"
        L"✅ C++ 引擎 / Python 桌面版 / Java 手机版\n\n\n"
        L"ETC官方\n© 2026 版权所有\nET",
        SS_CENTER, parent, -1, 12, 20, 880, 300);
    SendMessageW(text, WM_SETFONT, (WPARAM)g.hFont, TRUE);
    mk_ctl(L"BUTTON", L"📜 查看许可协议", WS_TABSTOP, parent, IDC_BTN_LICENSE, 340, 380, 200, 40);
}

static const wchar_t* LICENSE_TEXT =
    L"ETC+ 加固卫士 许可协议 v2.1\n\n"
    L"1. 本软件仅供合法用途（保护自有应用），用户需遵守所在国家法律法规。\n"
    L"2. 禁止将本软件用于任何非法目的，包括但不限于破解、盗版、传播恶意软件等。\n"
    L"3. 本软件知识产权归 ETC 官方所有。\n"
    L"4. 本软件按\"现状\"提供，不提供任何明示或暗示的担保。\n"
    L"5. ETC 官方不对因使用本软件产生的任何损失负责。\n"
    L"6. 用户应妥善保管使用本软件产生的所有文件。\n"
    L"7. 本协议受中华人民共和国法律管辖。\n\n"
    L"© 2026 ETC 官方 版权所有\n联系: et2416444244@outlook.com";

static void show_license() {
    MessageBoxW(g.hwndMain, LICENSE_TEXT, L"许可协议", MB_ICONINFORMATION | MB_OK);
}

// ---------------- 窗口过程 ----------------
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);

// 标签页容器窗口过程：把子控件的命令/颜色消息转发给主窗口过程
static LRESULT CALLBACK TabHostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CREATE) return 0;
    return MainWndProc(h, m, w, l);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g.hwndMain = hwnd;
            INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS };
            InitCommonControlsEx(&icc);

            for (auto d : { L"core", L"outapp", L"keys", L"logs", L"asstse" }) ensure_dir(exe_dir() + L"\\" + d);

            g.hFont = CreateFontW(-g_config.font_size * 1.4, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Microsoft YaHei UI");

            // 标题
            HWND title = mk_ctl(L"STATIC", L"ETC+ 加固卫士", SS_LEFT, hwnd, -1, 16, 14, 260, 40);
            HFONT titleFont = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            SendMessageW(title, WM_SETFONT, (WPARAM)titleFont, TRUE);

            mk_ctl(L"STATIC", L"主题:", SS_RIGHT, hwnd, IDC_ST_THEME_LBL, 760, 22, 50, 24);
            g.comboTheme = mk_ctl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                  hwnd, IDC_COMBO_THEME, 814, 20, 120, 160);
            for (int i = 0; i < THEME_COUNT; i++)
                SendMessageW(g.comboTheme, CB_ADDSTRING, 0, (LPARAM)THEMES[i].name);
            SendMessageW(g.comboTheme, CB_SETCURSEL, g_config.theme, 0);

            // 标签页
            g.hwndTabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE,
                12, 62, 920, 590, hwnd, (HMENU)(INT_PTR)IDC_TABS, g.hInst, nullptr);
            if (g.hFont) SendMessageW(g.hwndTabs, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            TCITEMW ti{};
            ti.mask = TCIF_TEXT;
            ti.pszText = (LPWSTR)L"🔒 加固";
            TabCtrl_InsertItem(g.hwndTabs, 0, &ti);
            ti.pszText = (LPWSTR)L"⚙️ 设置";
            TabCtrl_InsertItem(g.hwndTabs, 1, &ti);
            ti.pszText = (LPWSTR)L"ℹ️ 关于";
            TabCtrl_InsertItem(g.hwndTabs, 2, &ti);

            RECT rc;
            TabCtrl_GetItemRect(g.hwndTabs, 0, &rc);
            GetClientRect(g.hwndTabs, &rc);
            TabCtrl_AdjustRect(g.hwndTabs, FALSE, &rc);
            rc.top += 30;
            rc.left += 10; rc.right -= 10; rc.bottom -= 10;

            // 三个标签页作为主窗口子窗口（消息转发类 ETCPageWnd）
            g.hwndTab1 = CreateWindowExW(0, L"ETCPageWnd", L"", WS_CHILD | WS_VISIBLE,
                24, 96, 906, 540, hwnd, (HMENU)(INT_PTR)100, g.hInst, nullptr);
            g.hwndTab2 = CreateWindowExW(0, L"ETCPageWnd", L"", WS_CHILD,
                24, 96, 906, 540, hwnd, (HMENU)(INT_PTR)101, g.hInst, nullptr);
            g.hwndTab3 = CreateWindowExW(0, L"ETCPageWnd", L"", WS_CHILD,
                24, 96, 906, 540, hwnd, (HMENU)(INT_PTR)102, g.hInst, nullptr);

            create_protect_tab(g.hwndTab1);
            create_settings_tab(g.hwndTab2);
            create_about_tab(g.hwndTab3);

            ShowWindow(g.hwndTab1, SW_SHOW);
            apply_theme();
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)lParam;
            if (nm->idFrom == IDC_TABS && nm->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(g.hwndTabs);
                ShowWindow(g.hwndTab1, sel == 0 ? SW_SHOW : SW_HIDE);
                ShowWindow(g.hwndTab2, sel == 1 ? SW_SHOW : SW_HIDE);
                ShowWindow(g.hwndTab3, sel == 2 ? SW_SHOW : SW_HIDE);
                return 0;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            switch (id) {
                case IDC_BTN_ADD: {
                    if (g.busy) break;
                    OPENFILENAMEW ofn{ sizeof(ofn) };
                    std::vector<wchar_t> buf(32768, 0);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"APK文件 (*.apk;*.apks)\0*.apk;*.apks\0所有文件 (*.*)\0*.*\0";
                    ofn.lpstrFile = buf.data();
                    ofn.nMaxFile = (DWORD)buf.size();
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
                    if (GetOpenFileNameW(&ofn)) {
                        std::wstring first(buf.data());
                        if (ofn.nFileOffset < (DWORD)first.size()) {
                            // 多选: 目录 + 文件名
                            std::wstring dir = first;
                            wchar_t* p = buf.data() + ofn.nFileOffset;
                            while (*p) {
                                add_apk(dir + L"\\" + p);
                                p += wcslen(p) + 1;
                            }
                        } else {
                            add_apk(first);
                        }
                    }
                    break;
                }
                case IDC_BTN_REMOVE: {
                    int sel = ListView_GetNextItem(g.lv, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < (int)g.apkFiles.size()) {
                        g.apkFiles.erase(g.apkFiles.begin() + sel);
                        g.apkStates.erase(g.apkStates.begin() + sel);
                        refresh_list();
                    }
                    break;
                }
                case IDC_BTN_CLEAR:
                    g.apkFiles.clear();
                    g.apkStates.clear();
                    refresh_list();
                    break;
                case IDC_BTN_VERIFY: {
                    int sel = ListView_GetNextItem(g.lv, -1, LVNI_SELECTED);
                    if (sel < 0) { MessageBoxW(hwnd, L"请先选择一个APK文件", L"提示", MB_ICONINFORMATION); break; }
                    do_verify(sel);
                    break;
                }
                case IDC_COMBO_MODE:
                    if (code == CBN_SELCHANGE) { /* 模式选择 */ }
                    break;
                case IDC_COMBO_THEME:
                    if (code == CBN_SELCHANGE) {
                        g.themeIdx = (int)SendMessageW(g.comboTheme, CB_GETCURSEL, 0, 0);
                        apply_theme();
                    }
                    break;
                case IDC_BTN_START: {
                    if (g.busy) break;
                    if (g.apkFiles.empty()) {
                        MessageBoxW(hwnd, L"请先添加要加固的APK文件！", L"警告", MB_ICONWARNING);
                        break;
                    }
                    wchar_t brand[128], ver[32];
                    GetWindowTextW(g.editBrand, brand, 128);
                    GetWindowTextW(g.editVer, ver, 32);
                    g_config.brand = brand;
                    g_config.version = ver;
                    save_config();
                    g.busy = true;
                    EnableWindow(g.btnStart, FALSE);
                    WorkerJob* job = new WorkerJob;
                    job->inputs = g.apkFiles;
                    job->mode = (int)SendMessageW(g.comboMode, CB_GETCURSEL, 0, 0);
                    job->brand = brand;
                    job->version = ver;
                    SendMessageW(g.progress, PBM_SETPOS, 0, 0);
                    HANDLE th = CreateThread(nullptr, 0, WorkerThreadProc, job, 0, nullptr);
                    if (th) CloseHandle(th);
                    break;
                }
                case IDC_BTN_FONT: {
                    wchar_t buf[8];
                    GetWindowTextW(g.editFontSize, buf, 8);
                    int sz = _wtoi(buf);
                    if (sz >= 10 && sz <= 20) {
                        g_config.font_size = sz;
                        save_config();
                        HFONT f = CreateFontW(-(int)(sz * 1.4), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
                        EnumChildWindows(hwnd, [](HWND h, LPARAM lp) -> BOOL {
                            SendMessageW(h, WM_SETFONT, (WPARAM)lp, TRUE);
                            return TRUE;
                        }, (LPARAM)f);
                        if (g.hFont) DeleteObject(g.hFont);
                        g.hFont = f;
                    }
                    break;
                }
                case IDC_BTN_NDK: {
                    OPENFILENAMEW ofn{ sizeof(ofn) };
                    wchar_t buf[MAX_PATH] = {};
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"压缩文件 (*.tar.gz;*.zip)\0*.tar.gz;*.zip\0";
                    ofn.lpstrFile = buf;
                    ofn.nMaxFile = MAX_PATH;
                    if (GetOpenFileNameW(&ofn)) {
                        g_config.ndk = buf;
                        save_config();
                        SetWindowTextW(g.editNdk, buf);
                        SetWindowTextW(g.stNdk, L"已安装（预留）");
                    }
                    break;
                }
                case IDC_BTN_MAVEN: {
                    OPENFILENAMEW ofn{ sizeof(ofn) };
                    wchar_t buf[MAX_PATH] = {};
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"压缩文件 (*.tar.gz)\0*.tar.gz\0";
                    ofn.lpstrFile = buf;
                    ofn.nMaxFile = MAX_PATH;
                    if (GetOpenFileNameW(&ofn)) {
                        g_config.maven = buf;
                        save_config();
                        SetWindowTextW(g.editMaven, buf);
                        SetWindowTextW(g.stMaven, L"已安装（预留）");
                    }
                    break;
                }
                case IDC_BTN_JAR: {
                    OPENFILENAMEW ofn{ sizeof(ofn) };
                    wchar_t buf[MAX_PATH] = {};
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"JAR文件 (*.jar)\0*.jar\0";
                    ofn.lpstrFile = buf;
                    ofn.nMaxFile = MAX_PATH;
                    if (GetOpenFileNameW(&ofn)) {
                        g_config.jar = buf;
                        save_config();
                        SetWindowTextW(g.editJar, buf);
                        SetWindowTextW(g.stJar, L"已配置（预留）");
                    }
                    break;
                }
                case IDC_BTN_LICENSE:
                    show_license();
                    break;
            }
            break;
        }

        case WM_APP_PROGRESS:
            SendMessageW(g.progress, PBM_SETPOS, wParam, 0);
            return 0;

        case WM_APP_STATUS: {
            wchar_t* p = (wchar_t*)lParam;
            if (p) { SetWindowTextW(g.status, p); delete[] p; }
            return 0;
        }

        case WM_APP_FINISH: {
            struct FinishMsg { bool ok; std::wstring text; }* fm = (FinishMsg*)lParam;
            EnableWindow(g.btnStart, TRUE);
            g.busy = false;
            SendMessageW(g.progress, PBM_SETPOS, fm->ok ? 100 : 0, 0);
            SetWindowTextW(g.status, L"就绪");
            MessageBoxW(hwnd, fm->text.c_str(), fm->ok ? L"完成" : L"错误",
                        fm->ok ? MB_ICONINFORMATION : MB_ICONERROR);
            log_msg(fm->text);
            delete fm;
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND h = (HWND)lParam;
            const Theme& t = THEMES[g.themeIdx];
            SetTextColor(hdc, t.text);
            SetBkColor(hdc, t.bg);
            if (h == g.lv || h == g.editBrand || h == g.editVer || h == g.editFontSize ||
                h == g.editNdk || h == g.editMaven || h == g.editJar) {
                SetBkColor(hdc, t.card);
                return (LRESULT)g.brushCard;
            }
            return (LRESULT)g.brushBg;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            const Theme& t = THEMES[g.themeIdx];
            SetTextColor(hdc, t.text);
            SetBkColor(hdc, t.card);
            return (LRESULT)g.brushCard;
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            const Theme& t = THEMES[g.themeIdx];
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, t.primary);
            return (LRESULT)g.brushBtn;
        }
        case WM_ERASEBKGND: {
            const Theme& t = THEMES[g.themeIdx];
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH br = CreateSolidBrush(t.bg);
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            return 1;
        }

        case WM_DESTROY:
            if (hwnd != g.hwndMain) return 0;
            if (g.brushBg) DeleteObject(g.brushBg);
            if (g.brushCard) DeleteObject(g.brushCard);
            if (g.brushBtn) DeleteObject(g.brushBtn);
            if (g.hFont) DeleteObject(g.hFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------- 入口 ----------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    g.hInst = hInst;
    load_config();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ETCProtectorMainWnd";
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    WNDCLASSEXW pc{ sizeof(pc) };
    pc.lpfnWndProc = TabHostProc;
    pc.hInstance = hInst;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.lpszClassName = L"ETCPageWnd";
    pc.hbrBackground = nullptr;
    RegisterClassExW(&pc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"ETC+ 加固卫士 v2.1",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 700,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    // 强制设置标题栏/任务栏大图标与小图标，确保左上角图标必定显示
    HICON hBig = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    HICON hSmall = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hBig);
    if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
