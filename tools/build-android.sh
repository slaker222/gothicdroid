#!/usr/bin/env bash
set -euo pipefail

# tools/build-android.sh - сборка OpenGothic под Android (arm64-v8a)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Окружение
export ANDROID_SDK_ROOT="/home/alex/android-sdk"
if [ -d "/home/alex/android-sdk/ndk/27.0.12077973" ]; then
  export ANDROID_NDK_ROOT="/home/alex/android-sdk/ndk/27.0.12077973"
fi
export JAVA_HOME="${JAVA_HOME:-/home/alex/.jdks/jdk-17.0.10+7}"
export PATH="${PROJECT_ROOT}/tools/bin:${ANDROID_SDK_ROOT}/cmake/3.22.1/bin:${JAVA_HOME}/bin:${PATH}"

BUILD_DIR="${PROJECT_ROOT}/build-android-arm64"
GLSLANG="${PROJECT_ROOT}/tools/bin/glslangValidator"

echo "=== Запуск сборки OpenGothic под Android ==="
echo "PROJECT_ROOT:     ${PROJECT_ROOT}"
echo "ANDROID_NDK_ROOT: ${ANDROID_NDK_ROOT}"
echo "GLSLANG:          ${GLSLANG}"
echo "BUILD_DIR:        ${BUILD_DIR}"

if [ ! -f "${GLSLANG}" ]; then
  echo "ОШИБКА: glslangValidator не найден по пути ${GLSLANG}" >&2
  exit 1
fi

if [ ! -f "${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake" ]; then
  echo "ОШИБКА: android.toolchain.cmake не найден в ${ANDROID_NDK_ROOT}" >&2
  exit 1
fi

cmake -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DGLSLANGVALIDATOR="${GLSLANG}" \
  -DTEMPEST_BUILD_VULKAN=ON \
  -DTEMPEST_BUILD_AUDIO=ON \
  -DTEMPEST_BUILD_SHARED=OFF \
  "${PROJECT_ROOT}"

echo "=== Компиляция Ninja ==="
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target Gothic2Notr

echo "=== Копирование библиотек в Android-проект ==="
JNILIBS_DIR="${PROJECT_ROOT}/android/app/src/main/jniLibs/arm64-v8a"
mkdir -p "${JNILIBS_DIR}"
cp "${BUILD_DIR}/opengothic/libopengothic.so" "${JNILIBS_DIR}/libopengothic.so"
cp "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "${JNILIBS_DIR}/libc++_shared.so"

echo "=== Сборка APK (Gradle assembleDebug) ==="
cd "${PROJECT_ROOT}/android"
./gradlew assembleDebug

APK_PATH="${PROJECT_ROOT}/android/app/build/outputs/apk/debug/app-debug.apk"
echo "=== Сборка APK успешно завершена ==="
ls -lh "${APK_PATH}"
echo "APK готов: ${APK_PATH}"
