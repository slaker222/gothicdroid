#!/bin/bash
set -e

# ==============================================================================
# OpenGothic WebAssembly / Browser Build Script
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-web"
DIST_DIR="${ROOT_DIR}/dist-web"

echo "=== OpenGothic Web Build Pipeline ==="

# 1. Setup environment paths and toolchains
if [ -f "/home/alex/emsdk/emsdk_env.sh" ]; then
    source "/home/alex/emsdk/emsdk_env.sh" > /dev/null 2>&1
elif [ -f "${HOME}/emsdk/emsdk_env.sh" ]; then
    source "${HOME}/emsdk/emsdk_env.sh" > /dev/null 2>&1
fi

export PATH="${ROOT_DIR}/tools/bin:/home/alex/Android/Sdk/cmake/3.22.1/bin:${PATH}"

# Check for Emscripten toolchain
if ! command -v emcmake &> /dev/null; then
    echo ""
    echo "[-] Ошибка: Emscripten SDK (emcmake) не найден в PATH."
    echo "[*] Чтобы установить Emscripten на Linux:"
    echo "    git clone https://github.com/emscripten-core/emsdk.git ~/emsdk"
    echo "    cd ~/emsdk"
    echo "    ./emsdk install latest"
    echo "    ./emsdk activate latest"
    echo "    source ~/emsdk/emsdk_env.sh"
    echo ""
    echo "После выполнения команд запустите этот скрипт снова:"
    echo "    ./scripts/build_web.sh"
    exit 1
fi

echo "[+] Emscripten найден: $(which emcc)"

# 2. Configure CMake with Emscripten
echo "[*] Конфигурация CMake под WebAssembly..."
mkdir -p "${BUILD_DIR}"
emcmake cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DTEMPEST_BUILD_VULKAN=OFF \
    -DTEMPEST_BUILD_METAL=OFF \
    -DTEMPEST_BUILD_DIRECTX12=OFF \
    -DTEMPEST_BUILD_AUDIO=OFF \
    -DEMSCRIPTEN=ON \
    -DGLSLANGVALIDATOR="${ROOT_DIR}/tools/bin/glslangValidator"

# 3. Build project
echo "[*] Компиляция OpenGothic (WASM + JS + HTML)..."
ninja -C "${BUILD_DIR}"

# 4. Prepare distribution bundle
echo "[*] Подготовка дистрибутива в ${DIST_DIR}..."
mkdir -p "${DIST_DIR}"
cp "${BUILD_DIR}/opengothic/opengothic.html" "${DIST_DIR}/index.html" 2>/dev/null || true
cp "${BUILD_DIR}/opengothic/opengothic.js"   "${DIST_DIR}/"           2>/dev/null || true
cp "${BUILD_DIR}/opengothic/opengothic.wasm" "${DIST_DIR}/"           2>/dev/null || true
cp "${BUILD_DIR}/opengothic/opengothic.data" "${DIST_DIR}/"           2>/dev/null || true

echo ""
echo "=== Сборка успешно завершена! ==="
echo "Файлы для запуска в браузере находятся в: ${DIST_DIR}"
echo ""
echo "Для запуска локального сервера выполните:"
echo "    python3 -m http.server 8080 --directory ${DIST_DIR}"
echo "Затем откройте в браузере (Chrome / Firefox / Edge):"
echo "    http://localhost:8080"
