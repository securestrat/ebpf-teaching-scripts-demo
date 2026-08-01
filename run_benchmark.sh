#!/usr/bin/env bash
set -e

# Compile if not already compiled or source changed
make -s

echo "=================================================================="
echo "      CPU FALSE CACHE SHARING BENCHMARK RUNNER"
echo "=================================================================="
echo "Detected CPU Cache Line Size: $(sysctl -n hw.cachelinesize 2>/dev/null || echo 64) bytes"
echo "Logical CPU Cores: $(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
echo "=================================================================="

# Pass all CLI args through to false_sharing_demo
./false_sharing_demo "$@"
