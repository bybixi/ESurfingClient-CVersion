#!/bin/bash
# 一键构建 ESurfingClient OpenWrt ipk 包
# 适用于 mediatek_filogic (ARM64) 架构
# 用法: bash build_ipk.sh

set -e

OPENWRT_VERSION="24.10.0"
SDK_URL="https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/mediatek/filogic/openwrt-sdk-${OPENWRT_VERSION}-mediatek-filogic_gcc-13.3.0_musl.Linux-x86_64.tar.zst"
WORK_DIR="/tmp/esurfing-build"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== ESurfingClient OpenWrt ipk 构建脚本 ==="
echo "目标架构: mediatek_filogic (ARM64)"
echo ""

# 安装依赖
echo "[1/6] 安装构建依赖..."
if command -v apt-get &>/dev/null; then
    sudo apt-get update -qq
    sudo apt-get install -y -qq build-essential cmake libzstd1 zstd wget tar 2>/dev/null
elif command -v apk &>/dev/null; then
    sudo apk add build-base cmake zstd wget tar 2>/dev/null
fi

# 下载 SDK
echo "[2/6] 下载 OpenWrt SDK..."
mkdir -p "$WORK_DIR"
if [ ! -f "$WORK_DIR/sdk.tar.zst" ]; then
    wget -q --show-progress -O "$WORK_DIR/sdk.tar.zst" "$SDK_URL"
else
    echo "  SDK 已存在, 跳过下载"
fi

# 解压 SDK
echo "[3/6] 解压 SDK..."
SDK_DIR="$WORK_DIR/openwrt-sdk"
if [ ! -d "$SDK_DIR" ]; then
    zstd -d "$WORK_DIR/sdk.tar.zst" -o "$WORK_DIR/sdk.tar" 2>/dev/null || true
    tar xf "$WORK_DIR/sdk.tar" -C "$WORK_DIR"
    mv "$WORK_DIR"/openwrt-sdk-* "$SDK_DIR"
else
    echo "  SDK 已解压, 跳过"
fi

# 配置 feed
echo "[4/6] 配置 SDK feed..."
cd "$SDK_DIR"
./scripts/feeds update -a 2>/dev/null || true
./scripts/feeds install -a 2>/dev/null || true

# 复制包源码
echo "[5/6] 复制 ESurfingClient 源码..."
PACKAGE_DIR="$SDK_DIR/package/esurfingclient"
rm -rf "$PACKAGE_DIR"
cp -r "$SCRIPT_DIR/esurfingclient" "$PACKAGE_DIR"

# 构建
echo "[6/6] 构建 ipk 包 (这可能需要几分钟)..."
make package/esurfingclient/compile V=s
make package/esurfingclient/install V=s
make package/index V=s

# 找到生成的 ipk 文件
echo ""
echo "=== 构建完成 ==="
echo "生成的 ipk 文件:"
find "$SDK_DIR/bin" -name "esurfingclient*.ipk" -type f 2>/dev/null | while read f; do
    echo "  $f"
    ls -lh "$f"
done

echo ""
echo "安装方法:"
echo "  scp <ipk文件> root@<路由器IP>:/tmp/"
echo "  ssh root@<路由器IP> 'opkg install /tmp/esurfingclient*.ipk'"
