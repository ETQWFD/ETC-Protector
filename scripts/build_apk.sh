#!/usr/bin/env bash
# ETC+ 加固卫士 - Android APK 手动构建脚本（无 Gradle）
# 工具链: JDK11 + build-tools(aapt2/d8/apksigner/zipalign) + android.jar
set -e
TOOL=/home/user/.etc_toolchain
export JAVA_HOME=$TOOL/jdk/jdk-11.0.32.1+1
export PATH=$JAVA_HOME/bin:$TOOL/android/sdk/android-14:$PATH

BT=$TOOL/android/sdk/android-14
ANDROID_JAR=$TOOL/android/sdk/android-36/android.jar
PROJ=$(cd "$(dirname "$0")/.." && pwd)/android
SRC=$PROJ/app/src/main
OUT=$PROJ/out
rm -rf $OUT && mkdir -p $OUT/classes $OUT/dex $OUT/res $OUT/gen

echo "[1/6] javac 编译 Java 源码"
javac -source 1.8 -target 1.8 -encoding UTF-8 -nowarn \
  -classpath "$ANDROID_JAR" -d $OUT/classes \
  $(find $SRC/java -name '*.java')

echo "[2/6] d8 转为 classes.dex"
java -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 \
  --release --lib "$ANDROID_JAR" --min-api 24 --output $OUT/dex \
  $(find $OUT/classes -name '*.class')

echo "[3/6] aapt 打包资源与清单"
aapt package -f -M $SRC/AndroidManifest.xml -S $SRC/res \
  -I "$ANDROID_JAR" -F $OUT/base.apk \
  --version-code 2 --version-name 2.1 \
  --min-sdk-version 24 --target-sdk-version 34

echo "[4/6] 合并 classes.dex 并 zipalign"
python3 - <<PY
import zipfile
base = "$OUT/base.apk"
final = "$OUT/unsigned.apk"
zin = zipfile.ZipFile(base)
with zipfile.ZipFile(final, 'w', zipfile.ZIP_DEFLATED) as zout:
    for it in zin.infolist():
        zout.writestr(it, zin.read(it.filename))
    zout.writestr('classes.dex', open("$OUT/dex/classes.dex",'rb').read())
print("merged ->", final)
PY
zipalign -p 4 $OUT/unsigned.apk $OUT/aligned.apk

echo "[5/6] 生成签名并 apksigner 签名"
KS=$PROJ/keys/debug.keystore
if [ ! -f "$KS" ]; then
  mkdir -p $(dirname "$KS")
  keytool -genkey -v -keystore "$KS" -alias debugkey \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -storepass android -keypass android \
    -dname "CN=ETC Protector, O=ETC, C=CN" 2>/dev/null
fi
apksigner sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --out $OUT/ETC_Protector.apk $OUT/aligned.apk

echo "[6/6] 验证签名"
apksigner verify --verbose $OUT/ETC_Protector.apk | head -5
echo "=== 完成: $OUT/ETC_Protector.apk ==="
ls -la $OUT/ETC_Protector.apk
