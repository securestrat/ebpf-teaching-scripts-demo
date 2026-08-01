#!/usr/bin/env bash
# ==============================================================================
#           CPU FALSE CACHE SHARING BENCHMARK INTERACTIVE RUNNER
# ==============================================================================
# This script compiles the C++ benchmark binary (if needed) and either:
#   1. Interactively queries the user for benchmark parameters and translates
#      them into CLI switches, OR
#   2. Parses direct command-line arguments and switches passed by the user.
# ==============================================================================

set -e

# Base directory where script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_PATH="${SCRIPT_DIR}/false_sharing_demo"

# Auto-compile if binary doesn't exist or source is newer
if [[ ! -f "$BIN_PATH" || "${SCRIPT_DIR}/false_sharing_demo.cpp" -nt "$BIN_PATH" ]]; then
    echo "⚙️  Compiling false_sharing_demo with Clang/GCC (-O3, C++17, pthreads)..."
    make -s -C "${SCRIPT_DIR}"
    echo "✅ Compilation successful!"
    echo ""
fi

# Detect system CPU architecture details
DETECTED_CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
CACHE_LINE_SIZE=$(sysctl -n hw.cachelinesize 2>/dev/null || echo 64)

# Print Help Banner
print_help() {
    echo "=================================================================="
    echo "     CPU FALSE CACHE SHARING BENCHMARK RUNNER"
    echo "=================================================================="
    echo "Usage: ./run_benchmark.sh [options]"
    echo ""
    echo "If run without options, an interactive wizard will prompt for parameters."
    echo ""
    echo "Switches / Options:"
    echo "  -t, --test <name>        Test to run: 'all', 'false', 'padded', 'local'"
    echo "  -n, --threads <N>        Number of threads (default: 4, detected cores: ${DETECTED_CORES})"
    echo "  -i, --iterations <N>     Number of increments per thread (default: 300000000)"
    echo "  -d, --duration <sec>     Run time-based test for <sec> seconds (e.g. 3.0)"
    echo "  -s, --sweep              Run multi-thread scaling sweep (1, 2, 4, 8, 16 threads)"
    echo "  -I, --interactive        Force interactive parameter prompt wizard"
    echo "  -h, --help               Display this help message"
    echo ""
    echo "Examples:"
    echo "  ./run_benchmark.sh -t all -n 4 -i 300000000"
    echo "  ./run_benchmark.sh --test all --threads 8 --duration 5.0"
    echo "  ./run_benchmark.sh --sweep"
    echo "=================================================================="
}

# ------------------------------------------------------------------------------
# INTERACTIVE PARAMETER QUERY WIZARD
# ------------------------------------------------------------------------------
run_interactive_wizard() {
    echo "=================================================================="
    echo "    ⚡ CPU FALSE CACHE SHARING BENCHMARK - PARAMETER WIZARD ⚡"
    echo "=================================================================="
    echo "System Info: ${DETECTED_CORES} CPU Cores | ${CACHE_LINE_SIZE}-Byte L1/L2 Cache Line Size"
    echo "=================================================================="
    echo ""

    # 1. Query Test Suite Selection
    echo "📌 STEP 1: Select Test Suite to Run"
    echo "  [1] Full Comparison: False Sharing vs. Padded vs. Local Baseline (Recommended)"
    echo "  [2] False Sharing Only (Unpadded Contended Cache Line)"
    echo "  [3] Padded & Aligned Only (Fixed Dedicated Cache Line)"
    echo "  [4] Local Variable Accumulation Only (Optimal Register Ceiling)"
    echo "  [5] Multi-Thread Scaling Sweep (1, 2, 4, 8, 16 Threads)"
    read -r -p "Enter choice [1-5] (default: 1): " test_choice
    test_choice=${test_choice:-1}

    if [[ "$test_choice" == "5" ]]; then
        echo ""
        echo "🚀 Running Multi-Thread Scaling Sweep..."
        make -s -C "${SCRIPT_DIR}" sweep
        exit 0
    fi

    case "$test_choice" in
        2) TEST_ARG="false" ;;
        3) TEST_ARG="padded" ;;
        4) TEST_ARG="local" ;;
        *) TEST_ARG="all" ;;
    esac
    echo "➡️ Selected Test: --test ${TEST_ARG}"
    echo ""

    # 2. Query Workload Mode (Iterations vs Duration)
    echo "📌 STEP 2: Select Workload Mode"
    echo "  [1] Fixed Iteration Count per thread (Recommended for exact comparison)"
    echo "  [2] Fixed Duration in seconds (Timed stress test)"
    read -r -p "Enter choice [1-2] (default: 1): " mode_choice
    mode_choice=${mode_choice:-1}
    echo ""

    # 3. Query Iteration Count or Duration
    if [[ "$mode_choice" == "2" ]]; then
        echo "📌 STEP 3: Enter Test Duration (Seconds)"
        echo "  Examples: 2.0 (Quick), 5.0 (Standard), 10.0 (Stress)"
        read -r -p "Duration in seconds (default: 3.0): " duration_val
        duration_val=${duration_val:-3.0}
        WORKLOAD_FLAG="--duration"
        WORKLOAD_VAL="${duration_val}"
        echo "➡️ Selected Duration: --duration ${WORKLOAD_VAL}"
    else
        echo "📌 STEP 3: Select Iteration Workload per thread"
        echo "  [1] Quick Smoke Test       : 50,000,000 ops/thread   (~0.5 sec)"
        echo "  [2] Standard Benchmark     : 300,000,000 ops/thread  (~2-3 sec) [Recommended]"
        echo "  [3] High-Intensity Stress  : 1,000,000,000 ops/thread (~8-10 sec)"
        echo "  [4] Custom iteration count"
        read -r -p "Enter choice [1-4] (default: 2): " iter_choice
        iter_choice=${iter_choice:-2}

        case "$iter_choice" in
            1) iter_val="50000000" ;;
            3) iter_val="1000000000" ;;
            4) 
                read -r -p "Enter custom iteration count (e.g. 500000000): " custom_iter
                iter_val=${custom_iter:-300000000}
                ;;
            *) iter_val="300000000" ;;
        esac
        WORKLOAD_FLAG="--iterations"
        WORKLOAD_VAL="${iter_val}"
        echo "➡️ Selected Workload: --iterations ${WORKLOAD_VAL}"
    fi
    echo ""

    # 4. Query Thread Count
    echo "📌 STEP 4: Enter Number of Concurrent Threads"
    default_threads=4
    if [[ "$DETECTED_CORES" -ge 8 ]]; then
        default_threads=8
    fi
    echo "  (Detected ${DETECTED_CORES} logical cores. Recommended: ${default_threads})"
    read -r -p "Enter thread count (default: ${default_threads}): " thread_val
    thread_val=${thread_val:-$default_threads}
    THREAD_FLAG="--threads"
    THREAD_VAL="${thread_val}"
    echo "➡️ Selected Threads: --threads ${THREAD_VAL}"
    echo ""

    # Construct and display final generated command line switches
    echo "=================================================================="
    echo "🚀 EXECUTING GENERATED COMMAND:"
    echo "   ./false_sharing_demo --test ${TEST_ARG} ${THREAD_FLAG} ${THREAD_VAL} ${WORKLOAD_FLAG} ${WORKLOAD_VAL}"
    echo "=================================================================="
    echo ""

    # Execute binary with queried parameters passed as switches
    "${BIN_PATH}" --test "${TEST_ARG}" "${THREAD_FLAG}" "${THREAD_VAL}" "${WORKLOAD_FLAG}" "${WORKLOAD_VAL}"
}

# ------------------------------------------------------------------------------
# PARSE COMMAND LINE ARGUMENTS & SWITCHES
# ------------------------------------------------------------------------------
TEST_ARG=""
THREAD_ARG=""
ITER_ARG=""
DUR_ARG=""
FORCE_INTERACTIVE=false
SWEEP_MODE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--test)
            TEST_ARG="$2"
            shift 2
            ;;
        -n|--threads)
            THREAD_ARG="$2"
            shift 2
            ;;
        -i|--iterations)
            ITER_ARG="$2"
            shift 2
            ;;
        -d|--duration)
            DUR_ARG="$2"
            shift 2
            ;;
        -s|--sweep)
            SWEEP_MODE=true
            shift
            ;;
        -I|--interactive)
            FORCE_INTERACTIVE=true
            shift
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            echo "❌ Unknown option: $1"
            print_help
            exit 1
            ;;
    esac
done

# If --sweep switch was provided
if [[ "$SWEEP_MODE" == true ]]; then
    make -s -C "${SCRIPT_DIR}" sweep
    exit 0
fi

# If no arguments provided or interactive explicitly requested, run wizard
if [[ "$FORCE_INTERACTIVE" == true || ( -z "$TEST_ARG" && -z "$THREAD_ARG" && -z "$ITER_ARG" && -z "$DUR_ARG" ) ]]; then
    run_interactive_wizard
    exit 0
fi

# Otherwise, construct CLI switches and run directly
CLI_FLAGS=()
if [[ -n "$TEST_ARG" ]]; then
    CLI_FLAGS+=("--test" "$TEST_ARG")
fi
if [[ -n "$THREAD_ARG" ]]; then
    CLI_FLAGS+=("--threads" "$THREAD_ARG")
fi
if [[ -n "$ITER_ARG" ]]; then
    CLI_FLAGS+=("--iterations" "$ITER_ARG")
fi
if [[ -n "$DUR_ARG" ]]; then
    CLI_FLAGS+=("--duration" "$DUR_ARG")
fi

echo "=================================================================="
echo "🚀 EXECUTING: ./false_sharing_demo ${CLI_FLAGS[*]}"
echo "=================================================================="
"${BIN_PATH}" "${CLI_FLAGS[@]}"
