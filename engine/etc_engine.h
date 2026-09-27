// etc_engine.h - ETC+ 加固引擎（C++ 核心，跨平台）
#ifndef ETC_ENGINE_H
#define ETC_ENGINE_H

#include <string>
#include <vector>
#include <cstdint>

namespace etc {

// 检测结果状态
enum class ApkState {
    NOT_APK = 0,       // 不是有效的 APK
    UNPROTECTED = 1,   // 未加固
    ETC_PROTECTED = 2, // 已由 ETC+ 加固（禁止二次加固）
    UNKNOWN = 3        // 存在其它加固特征或异常
};

struct CheckResult {
    ApkState state = ApkState::NOT_APK;
    std::string message;      // 人类可读
    int dex_count = 0;        // DEX 数量
    size_t entry_count = 0;   // 文件条目数
    std::string brand;        // 检测到的加固品牌
    std::string version;
};

struct HardenOptions {
    int mode = 0;              // 0=标准(明文标识) 1=深度(标识加密)
    bool add_marker = true;
    bool write_integrity = true;
    std::string brand = "ETC+加固";
    std::string version = "2.1";
    int zip_level = 6;         // deflate 压缩级别
};

struct HardenResult {
    bool ok = false;
    std::string message;
    std::string output_path;
    size_t in_size = 0;
    size_t out_size = 0;
};

struct VerifyItem {
    std::string name;
    bool ok = false;           // 哈希是否一致
    std::string expected_hash;
    std::string actual_hash;
    bool extra = false;        // 清单外新增
    bool missing = false;      // 清单内有但文件缺失
};

struct VerifyResult {
    bool ok = false;
    bool protected_ = false;   // 是否为 ETC+ 加固产物
    std::string brand;
    std::string version;
    std::string protected_time;
    int mode = 0;
    std::vector<VerifyItem> items;
    std::string message;
};

// 检查 APK 状态
CheckResult check_apk(const std::string& apk_path);

// 加固 APK（输入文件不会被修改，输出为全新文件）
HardenResult harden_apk(const std::string& input, const std::string& output,
                        const HardenOptions& opt);

// 校验完整性
VerifyResult verify_apk(const std::string& apk_path);

// 工具函数
std::string sha256_hex(const void* data, size_t len);
std::string sha256_file(const std::string& path);
bool file_exists(const std::string& path);
std::string read_text_file(const std::string& path);

} // namespace etc

#endif
