#!/usr/bin/env bash
# ETC+ 加固卫士 - Windows EXE 交叉编译脚本（Linux 上用 mingw-w64）
set -e
cd "$(dirname "$0")/.."
OUT=out
mkdir -p $OUT

echo "[1/3] windres 编译资源"
x86_64-w64-mingw32-windres desktop_cpp_win32/resource.rc -O coff -o desktop_cpp_win32/resource.o

echo "[2/3] g++ 交叉编译（原生 Win32，非脚本打包）"
x86_64-w64-mingw32-g++ -O2 -std=c++17 -w -municode -I engine \
  desktop_cpp_win32/ETC_Protector.cpp \
  engine/etc_engine.cpp \
  engine/third_party/miniz.c engine/third_party/miniz_tdef.c \
  engine/third_party/miniz_tinfl.c engine/third_party/miniz_zip.c \
  desktop_cpp_win32/resource.o \
  -o $OUT/ETC_Protector.exe \
  -mwindows -static -static-libgcc -static-libstdc++ \
  -lcomctl32 -lgdi32 -lcomdlg32 -lshlwapi -luuid -lole32

echo "[3/3] 完成"
ls -la $OUT/ETC_Protector.exe
file $OUT/ETC_Protector.exe
