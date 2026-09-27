// etc_engine.cpp - ETC+ 加固引擎实现（miniz + 自研SHA-256，无外部依赖）
#include "etc_engine.h"
#include "sha256.h"
#include "third_party/miniz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace etc {

// ---------- 基础工具 ----------
std::string sha256_hex(const void* data, size_t len) {
    Sha256 h;
    h.update(data, len);
    return h.hex();
}

bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static std::string read_binary_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "无法打开文件: " + path;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)sz);
    if (sz > 0) {
        size_t rd = fread(out.data(), 1, (size_t)sz, f);
        if (rd != (size_t)sz) { fclose(f); return "读取失败: " + path; }
    }
    fclose(f);
    return "";
}

std::string read_text_file(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!read_binary_file(path, buf).empty()) return "";
    return std::string(buf.begin(), buf.end());
}

std::string sha256_file(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!read_binary_file(path, buf).empty()) return "";
    return sha256_hex(buf.data(), buf.size());
}

// miniz 的 get_filename 返回长度包含结尾 '\0'，这里统一裁剪
static std::string zip_name(const char* name, mz_uint len) {
    while (len > 0 && name[len - 1] == '\0') len--;
    return std::string(name, len);
}

static std::string now_str() {
    time_t t = time(nullptr);
    struct tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// ---------- 简易 JSON ----------
static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

class Json {
public:
    enum Type { NUL, OBJ, ARR, STR, NUM, BOOL };
    Type type = NUL;
    std::vector<std::pair<std::string, Json>> obj;
    std::vector<Json> arr;
    std::string str;
    double num = 0;
    bool b = false;

    const Json* get(const std::string& k) const {
        for (auto& p : obj) if (p.first == k) return &p.second;
        return nullptr;
    }
    static Json make_obj() { Json j; j.type = OBJ; return j; }
    void set(const std::string& k, Json v) {
        for (auto& p : obj) if (p.first == k) { p.second = std::move(v); return; }
        obj.emplace_back(k, std::move(v));
    }
    static Json make_str(const std::string& s) { Json j; j.type = STR; j.str = s; return j; }
    static Json make_num(double n) { Json j; j.type = NUM; j.num = n; return j; }

    std::string dump() const {
        switch (type) {
            case NUL: return "null";
            case BOOL: return b ? "true" : "false";
            case NUM: { char b[32]; snprintf(b, sizeof(b), "%lld", (long long)num); return b; }
            case STR: return "\"" + json_escape(str) + "\"";
            case ARR: {
                std::string s = "[";
                for (size_t i = 0; i < arr.size(); i++) {
                    if (i) s += ",";
                    s += arr[i].dump();
                }
                return s + "]";
            }
            case OBJ: {
                std::string s = "{";
                for (size_t i = 0; i < obj.size(); i++) {
                    if (i) s += ",";
                    s += "\"" + json_escape(obj[i].first) + "\":" + obj[i].second.dump();
                }
                return s + "}";
            }
        }
        return "null";
    }

    // 简单递归下降解析
    static bool parse(const char*& p, Json& out) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '{') {
            p++;
            out = make_obj();
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (*p == '}') { p++; return true; }
            while (true) {
                Json k;
                if (!parse(p, k) || k.type != STR) return false;
                while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
                if (*p != ':') return false;
                p++;
                Json v;
                if (!parse(p, v)) return false;
                out.set(k.str, v);
                while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
                if (*p == ',') { p++; continue; }
                if (*p == '}') { p++; return true; }
                return false;
            }
        }
        if (*p == '[') {
            p++;
            out.type = ARR;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            if (*p == ']') { p++; return true; }
            while (true) {
                Json v;
                if (!parse(p, v)) return false;
                out.arr.push_back(v);
                while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; return true; }
                return false;
            }
        }
        if (*p == '"') {
            p++;
            std::string s;
            while (*p && *p != '"') {
                if (*p == '\\') {
                    p++;
                    if (*p == 'n') s += '\n';
                    else if (*p == 't') s += '\t';
                    else if (*p == '"') s += '"';
                    else if (*p == '\\') s += '\\';
                    else if (*p == 'u') {
                        // 简化：跳过4位hex（APK条目名几乎不含）
                        s += "?";
                        p += 4;
                    }
                    else s += *p;
                    p++;
                } else {
                    s += *p++;
                }
            }
            if (*p != '"') return false;
            p++;
            out.type = STR; out.str = s;
            return true;
        }
        if (*p == 't') { p += 4; out.type = BOOL; out.b = true; return true; }
        if (*p == 'f') { p += 5; out.type = BOOL; out.b = false; return true; }
        if (*p == 'n') { p += 4; out.type = NUL; return true; }
        if (*p == '-' || (*p >= '0' && *p <= '9')) {
            char* end;
            out.type = NUM;
            out.num = strtod(p, &end);
            p = end;
            return true;
        }
        return false;
    }

    static bool parse_string(const std::string& s, Json& out) {
        const char* p = s.c_str();
        return parse(p, out) && (*p == '\0');
    }
};

// ---------- 标识加密（深度模式） ----------
static const char* kXorKey = "ETC+@2026#SECURE";

static std::string xor_bytes(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t klen = strlen(kXorKey);
    for (size_t i = 0; i < in.size(); i++)
        out += (char)((unsigned char)in[i] ^ (unsigned char)kXorKey[i % klen]);
    return out;
}

static std::string hex_encode(const std::string& in) {
    static const char* h = "0123456789abcdef";
    std::string out;
    for (unsigned char c : in) {
        out += h[c >> 4];
        out += h[c & 0xF];
    }
    return out;
}

static std::string hex_decode(const std::string& in) {
    std::string out;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (size_t i = 0; i + 1 < in.size(); i += 2)
        out += (char)((hv(in[i]) << 4) | hv(in[i + 1]));
    return out;
}

static std::string encrypt_text(const std::string& in) {
    return std::string("ETCX1:") + hex_encode(xor_bytes(in));
}

static bool decrypt_text(const std::string& in, std::string& out) {
    if (in.rfind("ETCX1:", 0) == 0) {
        out = xor_bytes(hex_decode(in.substr(6)));
        return true;
    }
    out = in;
    return false;
}

// ---------- 常量 ----------
static const char* kMarkerPath = "assets/etc_protect.etc";
static const char* kSigPath    = "META-INF/ETCPLUS.SF";
static const char* kIntegrity  = "assets/etc_integrity.json";
static const char* kMarkerHead = "ETC+ PROTECTED";

// ---------- 读取 zip 条目（流式） ----------
static bool extract_entry(mz_zip_archive* zip, mz_uint idx,
                          std::vector<uint8_t>& buf, std::string& err) {
    buf.clear();
    mz_zip_reader_extract_iter_state* it =
        mz_zip_reader_extract_iter_new(zip, idx, 0);
    if (!it) { err = "无法读取条目"; return false; }
    uint8_t chunk[65536];
    size_t got;
    while ((got = mz_zip_reader_extract_iter_read(it, chunk, sizeof(chunk))) > 0) {
        buf.insert(buf.end(), chunk, chunk + got);
    }
    mz_zip_reader_extract_iter_free(it);
    return true;
}

// ---------- 检查 ----------
CheckResult check_apk(const std::string& apk_path) {
    CheckResult r;
    if (!file_exists(apk_path)) {
        r.state = ApkState::NOT_APK;
        r.message = "文件不存在";
        return r;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, apk_path.c_str(), 0)) {
        r.state = ApkState::NOT_APK;
        r.message = "不是有效的APK/ZIP文件";
        return r;
    }

    mz_uint n = mz_zip_reader_get_num_files(&zip);
    bool has_manifest = false, has_dex = false, has_marker = false, has_sig = false;
    std::vector<std::string> packer_hits;

    for (mz_uint i = 0; i < n; i++) {
        char name[512];
        mz_uint len = mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        if (len == 0 || len >= sizeof(name)) continue;
        std::string nm = zip_name(name, len);
        if (nm == "AndroidManifest.xml") has_manifest = true;
        if (nm.size() >= 4 && nm.substr(nm.size() - 4) == ".dex") has_dex = true;
        if (nm == kMarkerPath) has_marker = true;
        if (nm == kSigPath) has_sig = true;
        std::string low = nm;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        static const char* packs[] = {
            "libdexhelper", "libprotectclass", "libjiagu", "libnesec",
            "libnqshield", "libshell", "lib-qqpireflect", "libdpt", "stubapp",
            "secneo", "bangcle", "com.qihoo.util", "libtosprotection",
            "libseal", "libshellx"
        };
        for (const char* pk : packs) {
            if (low.find(pk) != std::string::npos) {
                packer_hits.push_back(nm);
                break;
            }
        }
    }

    r.entry_count = n;
    if (!has_manifest) {
        mz_zip_reader_end(&zip);
        r.state = ApkState::NOT_APK;
        r.message = "缺少 AndroidManifest.xml，不是有效APK";
        return r;
    }

    // 统计 dex 数量
    mz_zip_archive zip2;
    memset(&zip2, 0, sizeof(zip2));
    if (mz_zip_reader_init_file(&zip2, apk_path.c_str(), 0)) {
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip2); i++) {
            char name[512];
            mz_uint len = mz_zip_reader_get_filename(&zip2, i, name, sizeof(name));
            std::string nm = zip_name(name, len);
            if (nm.size() >= 4 && nm.substr(nm.size() - 4) == ".dex") r.dex_count++;
        }
        mz_zip_reader_end(&zip2);
    }

    if (has_marker) {
        // 读取标记确认品牌
        mz_zip_archive zip3;
        memset(&zip3, 0, sizeof(zip3));
        if (mz_zip_reader_init_file(&zip3, apk_path.c_str(), 0)) {
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip3); i++) {
                char name[512];
                mz_uint len = mz_zip_reader_get_filename(&zip3, i, name, sizeof(name));
                std::string nm = zip_name(name, len);
                if (nm == kMarkerPath) {
                    std::vector<uint8_t> buf;
                    std::string err;
                    if (extract_entry(&zip3, i, buf, err)) {
                        std::string content(buf.begin(), buf.end());
                        // 解析品牌/版本
                        size_t p1 = content.find("Brand: ");
                        size_t p2 = content.find("Version: ");
                        if (p1 != std::string::npos) {
                            size_t e = content.find('\n', p1);
                            r.brand = content.substr(p1 + 7, e - p1 - 7);
                        }
                        if (p2 != std::string::npos) {
                            size_t e = content.find('\n', p2);
                            r.version = content.substr(p2 + 9, e - p2 - 9);
                        }
                    }
                    break;
                }
            }
            mz_zip_reader_end(&zip3);
        }
        r.state = ApkState::ETC_PROTECTED;
        if (r.brand.empty()) r.brand = "ETC+加固";
        r.message = "该APK已由 " + r.brand + " 加固，禁止二次加固";
        return r;
    }

    if (has_sig) {
        r.state = ApkState::ETC_PROTECTED;
        r.brand = "ETC+加固";
        r.version = "2.1";
        r.message = "该APK已由 ETC+ 加固（旧标识），禁止二次加固";
        return r;
    }

    if (!packer_hits.empty()) {
        r.state = ApkState::UNKNOWN;
        r.message = "检测到其它加固特征: " + packer_hits[0] + (packer_hits.size() > 1 ? " 等" : "");
        return r;
    }

    r.state = ApkState::UNPROTECTED;
    r.message = "未检测到加固";
    return r;
}

// ---------- 加固 ----------
HardenResult harden_apk(const std::string& input, const std::string& output,
                        const HardenOptions& opt) {
    HardenResult hr;
    hr.output_path = output;

    // 1. 预检
    CheckResult cr = check_apk(input);
    if (cr.state == ApkState::NOT_APK) {
        hr.message = cr.message;
        return hr;
    }
    if (cr.state == ApkState::ETC_PROTECTED) {
        hr.message = cr.message;
        return hr;
    }
    if (cr.state == ApkState::UNKNOWN) {
        hr.message = "检测到其它加固特征，为避免冲突请先还原为未加固版本: " + cr.message;
        return hr;
    }

    // 2. 打开输入
    mz_zip_archive in;
    memset(&in, 0, sizeof(in));
    if (!mz_zip_reader_init_file(&in, input.c_str(), 0)) {
        hr.message = "无法读取APK文件";
        return hr;
    }
    mz_uint n = mz_zip_reader_get_num_files(&in);

    // 3. 构建清单
    Json manifest = Json::make_obj();
    manifest.set("brand", Json::make_str(opt.brand));
    manifest.set("version", Json::make_str(opt.version));
    manifest.set("mode", Json::make_num(opt.mode));
    manifest.set("time", Json::make_str(now_str()));
    Json files = Json::make_obj();
    std::vector<std::string> entry_names;

    // 4. 写出新包
    if (file_exists(output)) remove(output.c_str());
    mz_zip_archive out;
    memset(&out, 0, sizeof(out));
    if (!mz_zip_writer_init_file(&out, output.c_str(), 0)) {
        mz_zip_reader_end(&in);
        hr.message = "无法创建输出文件";
        return hr;
    }

    for (mz_uint i = 0; i < n; i++) {
        char name[1024];
        mz_uint len = mz_zip_reader_get_filename(&in, i, name, sizeof(name));
        if (len == 0 || len >= sizeof(name)) continue;
        std::string nm = zip_name(name, len);

        // 跳过原签名文件（重新打包后需重新签名）
        std::string up = nm;
        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
        if (up.rfind("META-INF/", 0) == 0 &&
            (up.size() >= 4 && (up.substr(up.size() - 4) == ".SF" ||
                                up.substr(up.size() - 4) == ".RSA" ||
                                up.substr(up.size() - 4) == ".DSA" ||
                                up.substr(up.size() - 4) == ".MF"))) {
            continue;
        }

        // 目录条目跳过
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&in, i, &st)) continue;
        if (st.m_is_directory) continue;

        std::vector<uint8_t> buf;
        std::string err;
        if (!extract_entry(&in, i, buf, err)) {
            mz_zip_writer_end(&out);
            mz_zip_reader_end(&in);
            hr.message = "读取条目失败: " + nm + " - " + err;
            return hr;
        }

        std::string h = sha256_hex(buf.data(), buf.size());
        Json item = Json::make_obj();
        item.set("s", Json::make_str(h));
        item.set("z", Json::make_num((double)buf.size()));
        files.set(nm, item);
        entry_names.push_back(nm);

        if (!mz_zip_writer_add_mem(&out, nm.c_str(), buf.data(), buf.size(),
                                   (mz_uint)opt.zip_level)) {
            mz_zip_writer_end(&out);
            mz_zip_reader_end(&in);
            hr.message = "写入条目失败: " + nm;
            return hr;
        }
    }

    manifest.set("files", files);
    std::string manifest_text = manifest.dump();

    // 5. 添加保护文件
    std::string marker =
        std::string(kMarkerHead) + "\n"
        "Brand: " + opt.brand + "\n"
        "Version: " + opt.version + "\n"
        "Time: " + now_str() + "\n"
        "Signature: " + sha256_hex(manifest_text.data(), manifest_text.size()) + "\n"
        "Copyright: ETC Official\n";

    std::string sig =
        "Signature-Version: 1.0\n"
        "Created-By: " + opt.brand + " " + opt.version + "\n"
        "X-Protect-By: ETC+ Security\n"
        "X-Protected-At: " + now_str() + "\n";

    if (opt.mode == 1) {
        marker = encrypt_text(marker);
        manifest_text = encrypt_text(manifest_text);
    }

    if (opt.add_marker) {
        mz_zip_writer_add_mem(&out, kMarkerPath, marker.data(), marker.size(), 6);
        mz_zip_writer_add_mem(&out, kSigPath, sig.data(), sig.size(), 6);
    }
    if (opt.write_integrity) {
        mz_zip_writer_add_mem(&out, kIntegrity, manifest_text.data(),
                              manifest_text.size(), 6);
    }

    if (!mz_zip_writer_finalize_archive(&out)) {
        mz_zip_writer_end(&out);
        mz_zip_reader_end(&in);
        hr.message = "打包失败";
        return hr;
    }
    mz_zip_writer_end(&out);
    mz_zip_reader_end(&in);

    hr.ok = true;
    hr.message = "加固成功";
    // 文件大小
    {
        FILE* f = fopen(input.c_str(), "rb");
        if (f) { fseek(f, 0, SEEK_END); hr.in_size = (size_t)ftell(f); fclose(f); }
        f = fopen(output.c_str(), "rb");
        if (f) { fseek(f, 0, SEEK_END); hr.out_size = (size_t)ftell(f); fclose(f); }
    }
    return hr;
}

// ---------- 校验 ----------
VerifyResult verify_apk(const std::string& apk_path) {
    VerifyResult r;
    if (!file_exists(apk_path)) {
        r.message = "文件不存在";
        return r;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, apk_path.c_str(), 0)) {
        r.message = "不是有效的APK文件";
        return r;
    }

    mz_uint n = mz_zip_reader_get_num_files(&zip);
    std::vector<uint8_t> marker_buf, integrity_buf;
    bool found_marker = false, found_integrity = false;

    for (mz_uint i = 0; i < n; i++) {
        char name[1024];
        mz_uint len = mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        if (len == 0 || len >= sizeof(name)) continue;
        std::string nm = zip_name(name, len);
        std::string err;
        if (nm == kMarkerPath) {
            found_marker = true;
            extract_entry(&zip, i, marker_buf, err);
        } else if (nm == kIntegrity) {
            found_integrity = true;
            extract_entry(&zip, i, integrity_buf, err);
        }
    }

    if (!found_marker || !found_integrity) {
        r.message = "该APK不是ETC+加固产物，无法校验";
        mz_zip_reader_end(&zip);
        return r;
    }

    r.protected_ = true;
    std::string marker_text(marker_buf.begin(), marker_buf.end());
    std::string integrity_text(integrity_buf.begin(), integrity_buf.end());
    decrypt_text(marker_text, marker_text);
    decrypt_text(integrity_text, integrity_text);

    // 品牌信息
    {
        size_t p1 = marker_text.find("Brand: ");
        size_t p2 = marker_text.find("Version: ");
        if (p1 != std::string::npos) {
            size_t e = marker_text.find('\n', p1);
            r.brand = marker_text.substr(p1 + 7, e - p1 - 7);
        }
        if (p2 != std::string::npos) {
            size_t e = marker_text.find('\n', p2);
            r.version = marker_text.substr(p2 + 9, e - p2 - 9);
        }
    }

    Json manifest;
    if (!Json::parse_string(integrity_text, manifest) || manifest.type != Json::OBJ) {
        r.message = "完整性清单解析失败";
        mz_zip_reader_end(&zip);
        return r;
    }
    const Json* fmode = manifest.get("mode");
    if (fmode) r.mode = (int)fmode->num;
    const Json* ftime = manifest.get("time");
    if (ftime && ftime->type == Json::STR) r.protected_time = ftime->str;
    const Json* fbrand = manifest.get("brand");
    if (fbrand && fbrand->type == Json::STR && !r.brand.empty() == false) r.brand = fbrand->str;
    const Json* fver = manifest.get("version");
    if (fver && fver->type == Json::STR && r.version.empty()) r.version = fver->str;

    const Json* ffiles = manifest.get("files");
    bool all_ok = true;
    int checked = 0;

    if (ffiles && ffiles->type == Json::OBJ) {
        for (auto& kv : ffiles->obj) {
            const std::string& name = kv.first;
            if (name == kMarkerPath || name == kIntegrity || name == kSigPath) continue;
            const Json& item = kv.second;
            VerifyItem vi;
            vi.name = name;
            if (item.type == Json::OBJ) {
                const Json* s = item.get("s");
                if (s && s->type == Json::STR) vi.expected_hash = s->str;
            }
            // 实际读取
            std::vector<uint8_t> buf;
            std::string err;
            bool found = false;
            for (mz_uint i = 0; i < n; i++) {
                char nm2[1024];
                mz_uint l2 = mz_zip_reader_get_filename(&zip, i, nm2, sizeof(nm2));
                std::string enm = zip_name(nm2, l2);
                if (enm == name) {
                    found = true;
                    extract_entry(&zip, i, buf, err);
                    break;
                }
            }
            if (!found) {
                vi.missing = true;
                vi.ok = false;
                all_ok = false;
            } else {
                vi.actual_hash = sha256_hex(buf.data(), buf.size());
                vi.ok = (vi.actual_hash == vi.expected_hash);
                if (!vi.ok) all_ok = false;
            }
            checked++;
            r.items.push_back(vi);
        }
    }

    // 额外新增条目检测
    for (mz_uint i = 0; i < n; i++) {
        char nm2[1024];
        mz_uint l2 = mz_zip_reader_get_filename(&zip, i, nm2, sizeof(nm2));
        if (l2 == 0 || l2 >= sizeof(nm2)) continue;
        std::string nm = zip_name(nm2, l2);
        if (nm == kMarkerPath || nm == kIntegrity || nm == kSigPath) continue;
        bool in_manifest = false;
        if (ffiles && ffiles->type == Json::OBJ) {
            for (auto& kv : ffiles->obj)
                if (kv.first == nm) { in_manifest = true; break; }
        }
        if (!in_manifest) {
            VerifyItem vi;
            vi.name = nm;
            vi.extra = true;
            vi.ok = false;
            all_ok = false;
            r.items.push_back(vi);
        }
    }

    r.ok = all_ok;
    r.message = all_ok ? "校验通过：所有文件未被篡改" : "校验失败：检测到文件被篡改";
    mz_zip_reader_end(&zip);
    return r;
}

} // namespace etc
