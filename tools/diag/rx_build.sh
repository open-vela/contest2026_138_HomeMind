#!/bin/bash
# RX 归一化补丁的构建封装：构建失败绝不进入刷写阶段。
cd /home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind || exit 1
export PATH="/usr/bin:/bin:$PATH"

JOBS=2 ./scripts/build.sh build > /tmp/rxnorm_build.log 2>&1
rc=$?
echo "BUILD_RC=$rc"
echo "==== build.log 尾部 ===="
tail -45 /tmp/rxnorm_build.log

if [ $rc -ne 0 ]; then
  echo "!!! 构建失败，绝不刷写（旧 artifacts/nuttx.bin 会被误刷）!!!"
  exit 1
fi
echo "==== 产物 ===="
ls -l artifacts/nuttx.bin artifacts/nuttx.elf
sha256sum artifacts/nuttx.bin
echo "OK_BUILD"
