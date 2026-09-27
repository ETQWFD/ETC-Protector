// main_cli.cpp - ETC+ 加固引擎命令行入口（测试/集成用）
// 用法: etc_protector check <apk>
//       etc_protector harden <input.apk> <output.apk> [mode=0|1] [brand] [version]
//       etc_protector verify <apk>
#include "etc_engine.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("ETC+ 加固引擎 v2.1 (C++)\n"
               "Usage:\n"
               "  %s check <apk>\n"
               "  %s harden <input.apk> <output.apk> [mode=0|1] [brand] [version]\n"
               "  %s verify <apk>\n", argv[0], argv[0], argv[0]);
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "check" && argc >= 3) {
        etc::CheckResult r = etc::check_apk(argv[2]);
        printf("state=%d\n", (int)r.state);
        printf("message=%s\n", r.message.c_str());
        printf("dex_count=%d\n", r.dex_count);
        printf("entry_count=%zu\n", r.entry_count);
        printf("brand=%s\n", r.brand.c_str());
        printf("version=%s\n", r.version.c_str());
        return r.state == etc::ApkState::NOT_APK ? 2 : 0;
    }

    if (cmd == "harden" && argc >= 4) {
        etc::HardenOptions opt;
        if (argc >= 5) opt.mode = atoi(argv[4]);
        if (argc >= 6) opt.brand = argv[5];
        if (argc >= 7) opt.version = argv[6];
        etc::HardenResult hr = etc::harden_apk(argv[2], argv[3], opt);
        printf("ok=%d\n", hr.ok ? 1 : 0);
        printf("message=%s\n", hr.message.c_str());
        if (hr.ok) {
            printf("output=%s\n", hr.output_path.c_str());
            printf("in_size=%zu\n", hr.in_size);
            printf("out_size=%zu\n", hr.out_size);
        }
        return hr.ok ? 0 : 1;
    }

    if (cmd == "verify" && argc >= 3) {
        etc::VerifyResult r = etc::verify_apk(argv[2]);
        printf("ok=%d\n", r.ok ? 1 : 0);
        printf("protected=%d\n", r.protected_ ? 1 : 0);
        printf("brand=%s\n", r.brand.c_str());
        printf("version=%s\n", r.version.c_str());
        printf("mode=%d\n", r.mode);
        printf("time=%s\n", r.protected_time.c_str());
        printf("message=%s\n", r.message.c_str());
        for (auto& it : r.items) {
            printf("item|%s|ok=%d|extra=%d|missing=%d|exp=%s|act=%s\n",
                   it.name.c_str(), it.ok ? 1 : 0, it.extra ? 1 : 0,
                   it.missing ? 1 : 0, it.expected_hash.c_str(), it.actual_hash.c_str());
        }
        return r.ok ? 0 : 1;
    }

    printf("未知命令: %s\n", cmd.c_str());
    return 1;
}
