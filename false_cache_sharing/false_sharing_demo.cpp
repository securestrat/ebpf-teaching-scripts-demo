/**
 * =================================================================================================
 *                          CPU FALSE CACHE SHARING BENCHMARK & DEMO
 * =================================================================================================
 *
 * 1. WHAT IS A CPU CACHE LINE?
 * ----------------------------
 * Modern CPUs do not read or write memory byte-by-byte. Instead, memory is transferred
 * between physical DRAM and the CPU cache hierarchy (L3 -> L2 -> L1) in fixed-size blocks
 * known as "Cache Lines".
 *
 * On almost all modern x86_64 and ARM64 processors, the cache line size is exactly 64 BYTES.
 *
 * 2. WHAT IS FALSE SHARING (CACHE LINE BOUNCING)?
 * -----------------------------------------------
 * False sharing is a silent performance-destroying concurrency anti-pattern that occurs when:
 *   a) Multiple threads run concurrently on different CPU cores.
 *   b) Each thread writes to its OWN logically distinct and independent variable.
 *   c) BUT both variables happen to reside within the SAME 64-byte physical cache line!
 *
 * 3. THE HARDWARE MESI / MOESI COHERENCY PROTOCOL:
 * ------------------------------------------------
 * To maintain a coherent memory view across multiple CPU cores, hardware implements cache
 * coherency protocols (e.g., MESI: Modified, Exclusive, Shared, Invalid):
 *
 *   [Step 1] Core 0 loads variable A (inside Cache Line X). Cache Line X is marked "Shared" (S).
 *   [Step 2] Core 1 loads variable B (also inside Cache Line X). Line X is marked "Shared" (S) in Core 1.
 *   [Step 3] Core 0 writes to variable A. Core 0 must gain "Exclusive/Modified" (M) ownership.
 *            The hardware issues an "Invalidation Bus Request", forcing Core 1's copy of Line X
 *            to transition to "Invalid" (I).
 *   [Step 4] Core 1 now attempts to write to variable B. Because Line X is now "Invalid" in Core 1's
 *            L1 cache, Core 1 suffers a cache miss!
 *   [Step 5] Core 1 must stall its execution pipeline, fetch the updated cache line from Core 0
 *            over the inter-core interconnect bus (QPI/UPI/Infinity Fabric), and invalidate Core 0's copy.
 *
 * This cyclic ping-ponging is called "Cache Line Bouncing". Even though the threads share NO
 * data logically, the hardware treats the entire 64-byte block as shared, degrading performance
 * by 300% to 1000%!
 *
 * 4. THE SOLUTION: CACHE LINE PADDING & ALIGNMENT
 * -----------------------------------------------
 * By enforcing 64-byte alignment (`alignas(64)`) and adding explicit padding bytes
 * (`uint8_t pad[56]`), we guarantee that each thread's variable occupies its OWN dedicated
 * cache line. No core ever invalidates another core's cache line during writes.
 * =================================================================================================
 */

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <string>
#include <cstdint>

// Standard CPU L1/L2 cache line size on x86_64 and ARM64
constexpr size_t CACHE_LINE_SIZE = 64;

// =================================================================================================
// DATA STRUCTURE 1: UNPADDED (False Sharing Vulnerable)
// =================================================================================================
// - sizeof(UnpaddedCounter) = 8 bytes.
// - An array `UnpaddedCounter counters[8]` occupies exactly 64 bytes (8 x 8 bytes).
// - Therefore, up to 8 threads writing to `counters[0]` through `counters[7]` will all write
//   to the EXACT SAME physical cache line, causing relentless cache invalidation storms!
struct UnpaddedCounter {
    // 'volatile' prevents the compiler optimizer from caching this variable in a CPU register
    // (e.g. %rax) and skipping memory stores. This forces an actual L1 cache store on every iteration.
    volatile uint64_t val{0};
};

// =================================================================================================
// DATA STRUCTURE 2: PADDED & ALIGNED (Fixed - Isolated Cache Line)
// =================================================================================================
// - `alignas(64)`: Enforces that the starting memory address of every instance is a multiple of 64.
// - `val`: 8-byte payload.
// - `pad`: 56-byte dummy buffer (64 - 8 = 56).
// - Total size = 64 bytes. Each instance occupies its own dedicated cache line.
// - Result: Zero cache invalidations across CPU cores!
struct alignas(CACHE_LINE_SIZE) PaddedCounter {
    volatile uint64_t val{0};
    uint8_t pad[CACHE_LINE_SIZE - sizeof(uint64_t)];
};

// =================================================================================================
// DATA STRUCTURE 3: LOCAL THREAD ACCUMULATOR (Optimal Theoretical Baseline)
// =================================================================================================
// - Each thread modifies a local register / stack variable throughout its execution loop.
// - It writes to shared memory only once at thread termination.
// - Provides the absolute ceiling for independent parallel computation speed.
struct alignas(CACHE_LINE_SIZE) LocalResult {
    uint64_t val{0};
};

// =================================================================================================
// BENCHMARK CONFIGURATION & METRICS
// =================================================================================================
enum class BenchmarkMode {
    FIXED_ITERATIONS, // Each thread executes exactly N iterations
    FIXED_DURATION    // Threads loop continuously for a specified duration in seconds
};

struct BenchmarkConfig {
    int num_threads = 4;
    BenchmarkMode mode = BenchmarkMode::FIXED_ITERATIONS;
    uint64_t iterations = 300'000'000ULL; // Default: 300 Million ops per thread
    double duration_seconds = 3.0;        // Default: 3.0 seconds for duration mode
    std::string test_name = "all";        // 'false', 'padded', 'local', or 'all'
};

struct BenchmarkResult {
    std::string name;
    double elapsed_ms;
    uint64_t total_operations;
    double mops_per_sec; // Million Operations Per Second throughput
};

// =================================================================================================
// MEMORY LAYOUT & CACHE LINE DIAGNOSTIC
// =================================================================================================
// Calculates and prints the exact virtual address, 64-byte cache line block index,
// and byte offset within the cache line for each thread's variable.
void print_memory_layout(int num_threads) {
    std::vector<UnpaddedCounter> unpadded(num_threads);
    std::vector<PaddedCounter> padded(num_threads);

    std::cout << "\n" << std::string(85, '=') << "\n";
    std::cout << "  HARDWARE MEMORY LAYOUT & CACHE LINE ANALYSIS\n";
    std::cout << "  Threads: " << num_threads << " | System Cache Line Size: " << CACHE_LINE_SIZE << " bytes\n";
    std::cout << std::string(85, '=') << "\n";

    // --- Diagnostic 1: Unpadded Layout ---
    std::cout << "\n[1] UNPADDED ARRAY (False Sharing Contention - Struct Size: " << sizeof(UnpaddedCounter) << " bytes):\n";
    std::cout << "Thread | Memory Address     | Cache Line Index   | Byte Offset  | Sharing Status\n";
    std::cout << "-------+--------------------+--------------------+--------------+---------------------------------\n";
    for (int i = 0; i < num_threads; ++i) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(&unpadded[i].val);
        uintptr_t line_idx = addr / CACHE_LINE_SIZE;
        uintptr_t offset = addr % CACHE_LINE_SIZE;
        bool shares = (i > 0 && (addr / CACHE_LINE_SIZE == reinterpret_cast<uintptr_t>(&unpadded[i - 1].val) / CACHE_LINE_SIZE));
        
        std::cout << "  T" << std::setw(3) << std::left << i 
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << addr << std::dec << std::setfill(' ')
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << line_idx << std::dec << std::setfill(' ')
                  << " | " << std::right << std::setw(12) << offset 
                  << " | " << (shares ? "❌ SHARES cache line with T" + std::to_string(i - 1) : "⚡ First in cache line")
                  << "\n";
    }

    // --- Diagnostic 2: Padded Layout ---
    std::cout << "\n[2] PADDED & ALIGNED ARRAY (Fixed - Struct Size: " << sizeof(PaddedCounter) << " bytes):\n";
    std::cout << "Thread | Memory Address     | Cache Line Index   | Byte Offset  | Sharing Status\n";
    std::cout << "-------+--------------------+--------------------+--------------+---------------------------------\n";
    for (int i = 0; i < num_threads; ++i) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(&padded[i].val);
        uintptr_t line_idx = addr / CACHE_LINE_SIZE;
        uintptr_t offset = addr % CACHE_LINE_SIZE;

        std::cout << "  T" << std::setw(3) << std::left << i 
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << addr << std::dec << std::setfill(' ')
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << line_idx << std::dec << std::setfill(' ')
                  << " | " << std::right << std::setw(12) << offset 
                  << " | " << "✅ ISOLATED (Dedicated cache line)"
                  << "\n";
    }
    std::cout << std::string(85, '=') << "\n\n";
}

// =================================================================================================
// TEST 1: FALSE SHARING (Unpadded Contention)
// =================================================================================================
BenchmarkResult run_unpadded_benchmark(const BenchmarkConfig& config) {
    std::vector<UnpaddedCounter> counters(config.num_threads);
    std::vector<std::thread> threads;
    threads.reserve(config.num_threads);

    // Atomic synchronization latches to ensure all threads start simultaneously
    std::atomic<bool> start_signal{false};
    std::atomic<bool> stop_signal{false};
    std::atomic<int> ready_count{0};

    // Worker function for Fixed Iteration Mode
    auto worker_iterations = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {
            // Spin wait until parent signals all threads to start
        }
        for (uint64_t i = 0; i < config.iterations; ++i) {
            counters[id].val++; // Direct write to shared cache line!
        }
    };

    // Worker function for Fixed Duration Mode
    auto worker_duration = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {
            // Spin wait until parent signals all threads to start
        }
        uint64_t count = 0;
        while (!stop_signal.load(std::memory_order_relaxed)) {
            counters[id].val++;
            count++;
        }
        counters[id].val = count;
    };

    // Spawn worker threads
    for (int i = 0; i < config.num_threads; ++i) {
        if (config.mode == BenchmarkMode::FIXED_ITERATIONS) {
            threads.emplace_back(worker_iterations, i);
        } else {
            threads.emplace_back(worker_duration, i);
        }
    }

    // Wait until all worker threads are created and spinning at the start gate
    while (ready_count.load() < config.num_threads) {
        std::this_thread::yield();
    }

    // Start benchmark timer and release all threads simultaneously
    auto start_time = std::chrono::high_resolution_clock::now();
    start_signal.store(true, std::memory_order_release);

    if (config.mode == BenchmarkMode::FIXED_DURATION) {
        std::this_thread::sleep_for(std::chrono::duration<double>(config.duration_seconds));
        stop_signal.store(true, std::memory_order_relaxed);
    }

    // Wait for all threads to finish
    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();

    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    uint64_t total_ops = 0;
    for (int i = 0; i < config.num_threads; ++i) {
        total_ops += counters[i].val;
    }

    // Calculate throughput in Million Operations per Second (MOps/s)
    double mops = (static_cast<double>(total_ops) / (elapsed_ms / 1000.0)) / 1'000'000.0;

    return BenchmarkResult{
        "1. False Sharing (Unpadded / Contended)",
        elapsed_ms,
        total_ops,
        mops
    };
}

// =================================================================================================
// TEST 2: FIXED WITH PADDING & ALIGNMENT
// =================================================================================================
BenchmarkResult run_padded_benchmark(const BenchmarkConfig& config) {
    std::vector<PaddedCounter> counters(config.num_threads);
    std::vector<std::thread> threads;
    threads.reserve(config.num_threads);

    std::atomic<bool> start_signal{false};
    std::atomic<bool> stop_signal{false};
    std::atomic<int> ready_count{0};

    auto worker_iterations = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {}
        for (uint64_t i = 0; i < config.iterations; ++i) {
            counters[id].val++; // Write to thread's dedicated cache line!
        }
    };

    auto worker_duration = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {}
        uint64_t count = 0;
        while (!stop_signal.load(std::memory_order_relaxed)) {
            counters[id].val++;
            count++;
        }
        counters[id].val = count;
    };

    for (int i = 0; i < config.num_threads; ++i) {
        if (config.mode == BenchmarkMode::FIXED_ITERATIONS) {
            threads.emplace_back(worker_iterations, i);
        } else {
            threads.emplace_back(worker_duration, i);
        }
    }

    while (ready_count.load() < config.num_threads) {
        std::this_thread::yield();
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    start_signal.store(true, std::memory_order_release);

    if (config.mode == BenchmarkMode::FIXED_DURATION) {
        std::this_thread::sleep_for(std::chrono::duration<double>(config.duration_seconds));
        stop_signal.store(true, std::memory_order_relaxed);
    }

    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();

    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    uint64_t total_ops = 0;
    for (int i = 0; i < config.num_threads; ++i) {
        total_ops += counters[i].val;
    }

    double mops = (static_cast<double>(total_ops) / (elapsed_ms / 1000.0)) / 1'000'000.0;

    return BenchmarkResult{
        "2. Padded & Aligned (Cache Line Isolated)",
        elapsed_ms,
        total_ops,
        mops
    };
}

// =================================================================================================
// TEST 3: LOCAL REGISTER / STACK ACCUMULATION BASELINE
// =================================================================================================
BenchmarkResult run_local_benchmark(const BenchmarkConfig& config) {
    std::vector<LocalResult> results(config.num_threads);
    std::vector<std::thread> threads;
    threads.reserve(config.num_threads);

    std::atomic<bool> start_signal{false};
    std::atomic<bool> stop_signal{false};
    std::atomic<int> ready_count{0};

    auto worker_iterations = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {}
        
        // Volatile local counter ensures compiler generates loop instructions without optimizing away
        volatile uint64_t local_accum = 0;
        for (uint64_t i = 0; i < config.iterations; ++i) {
            local_accum++;
        }
        results[id].val = local_accum; // Single write at end
    };

    auto worker_duration = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {}
        
        volatile uint64_t local_accum = 0;
        while (!stop_signal.load(std::memory_order_relaxed)) {
            local_accum++;
        }
        results[id].val = local_accum;
    };

    for (int i = 0; i < config.num_threads; ++i) {
        if (config.mode == BenchmarkMode::FIXED_ITERATIONS) {
            threads.emplace_back(worker_iterations, i);
        } else {
            threads.emplace_back(worker_duration, i);
        }
    }

    while (ready_count.load() < config.num_threads) {
        std::this_thread::yield();
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    start_signal.store(true, std::memory_order_release);

    if (config.mode == BenchmarkMode::FIXED_DURATION) {
        std::this_thread::sleep_for(std::chrono::duration<double>(config.duration_seconds));
        stop_signal.store(true, std::memory_order_relaxed);
    }

    for (auto& t : threads) {
        t.join();
    }
    auto end_time = std::chrono::high_resolution_clock::now();

    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    uint64_t total_ops = 0;
    for (int i = 0; i < config.num_threads; ++i) {
        total_ops += results[i].val;
    }

    double mops = (static_cast<double>(total_ops) / (elapsed_ms / 1000.0)) / 1'000'000.0;

    return BenchmarkResult{
        "3. Local Thread Variable (Register / Stack)",
        elapsed_ms,
        total_ops,
        mops
    };
}

// =================================================================================================
// PRESENTATION, REPORTING & SPEEDUP ANALYSIS
// =================================================================================================
void print_results_table(const std::vector<BenchmarkResult>& results, const BenchmarkConfig& config) {
    std::cout << "\n" << std::string(85, '=') << "\n";
    std::cout << "  BENCHMARK RESULTS SUMMARY\n";
    std::cout << "  Threads: " << config.num_threads << " | ";
    if (config.mode == BenchmarkMode::FIXED_ITERATIONS) {
        std::cout << "Mode: Fixed Iterations (" << config.iterations << " ops/thread)\n";
    } else {
        std::cout << "Mode: Fixed Duration (" << config.duration_seconds << " seconds)\n";
    }
    std::cout << std::string(85, '=') << "\n";

    std::cout << std::left << std::setw(46) << "Test Implementation"
              << std::right << std::setw(13) << "Time (ms)"
              << std::setw(13) << "Total Ops"
              << std::setw(13) << "MOps/sec"
              << "\n";
    std::cout << std::string(85, '-') << "\n";

    double unpadded_time = 0.0;
    double unpadded_mops = 0.0;

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& res = results[i];
        if (i == 0) {
            unpadded_time = res.elapsed_ms;
            unpadded_mops = res.mops_per_sec;
        }

        std::cout << std::left << std::setw(46) << res.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(13) << res.elapsed_ms
                  << std::setw(13) << res.total_operations
                  << std::setw(13) << res.mops_per_sec
                  << "\n";
    }
    std::cout << std::string(85, '=') << "\n";

    // Speedup Comparison
    if (results.size() >= 2) {
        double padded_time = results[1].elapsed_ms;
        double padded_mops = results[1].mops_per_sec;

        double speedup = (config.mode == BenchmarkMode::FIXED_ITERATIONS)
            ? (unpadded_time / padded_time)
            : (padded_mops / unpadded_mops);

        std::cout << "\n🚀 PERFORMANCE GAIN ANALYSIS:\n";
        std::cout << "  • False Sharing (Contended) : " << std::fixed << std::setprecision(2) << unpadded_time << " ms (" << unpadded_mops << " MOps/s)\n";
        std::cout << "  • Padded & Aligned (Fixed)  : " << std::fixed << std::setprecision(2) << padded_time << " ms (" << padded_mops << " MOps/s)\n";
        std::cout << "  • Performance Speedup       : " << std::fixed << std::setprecision(2) << speedup << "x FASTER! (";
        if (speedup >= 1.0) {
            std::cout << "+" << (speedup - 1.0) * 100.0 << "% higher throughput)\n";
        } else {
            std::cout << "N/A)\n";
        }
        std::cout << "  • Architectural Explanation : By padding each thread's counter to 64 bytes (1 cache line),\n";
        std::cout << "                                each core operates on independent L1 cache lines, eliminating\n";
        std::cout << "                                MESI cache invalidation requests and inter-core bus traffic!\n\n";
    }
}

// =================================================================================================
// CLI ARGUMENT PARSER & HELP
// =================================================================================================
void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -t, --test <name>        Test to run: 'false', 'padded', 'local', or 'all' (default: 'all')\n"
              << "  -n, --threads <N>        Number of threads to run (default: 4, detected CPU cores: " << std::thread::hardware_concurrency() << ")\n"
              << "  -i, --iterations <N>     Number of increments per thread (default: 300000000)\n"
              << "  -d, --duration <sec>     Run time-based test for <sec> seconds (e.g. --duration 3.0)\n"
              << "  -h, --help               Show this help message\n\n"
              << "Examples:\n"
              << "  " << prog_name << " --test all --threads 4 --iterations 300000000\n"
              << "  " << prog_name << " -t all -n 8 -d 3.0\n"
              << "  " << prog_name << " -t false -n 4 -i 100000000\n"
              << "  " << prog_name << " -t padded -n 4 -i 100000000\n";
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config;
    unsigned int hw_threads = std::thread::hardware_concurrency();
    config.num_threads = hw_threads > 0 ? (hw_threads >= 4 ? 4 : hw_threads) : 4;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if ((arg == "--threads" || arg == "-n") && i + 1 < argc) {
            config.num_threads = std::stoi(argv[++i]);
        } else if ((arg == "--iterations" || arg == "-i") && i + 1 < argc) {
            config.iterations = std::stoull(argv[++i]);
            config.mode = BenchmarkMode::FIXED_ITERATIONS;
        } else if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            config.duration_seconds = std::stod(argv[++i]);
            config.mode = BenchmarkMode::FIXED_DURATION;
        } else if ((arg == "--test" || arg == "-t") && i + 1 < argc) {
            config.test_name = argv[++i];
        }
    }

    if (config.num_threads < 1) {
        std::cerr << "Error: Thread count must be at least 1.\n";
        return 1;
    }

    std::cout << "\n" << std::string(85, '#') << "\n";
    std::cout << "       CPU FALSE SHARING & CACHE LINE PADDING BENCHMARK SUITE\n";
    std::cout << std::string(85, '#') << "\n";
    std::cout << "Hardware Concurrency : " << hw_threads << " Logical CPU Cores\n";
    std::cout << "Configured Threads   : " << config.num_threads << "\n";
    std::cout << "Selected Test Suite  : " << config.test_name << "\n";

    // 1. Output Memory Layout & Alignment Diagnostics
    print_memory_layout(config.num_threads);

    // 2. Execute Selected Benchmarks
    std::vector<BenchmarkResult> results;

    if (config.test_name == "false" || config.test_name == "all") {
        std::cout << ">> [1/3] Running Test 1: False Sharing (Unpadded / Contended)..." << std::flush;
        auto res = run_unpadded_benchmark(config);
        std::cout << " Done. (" << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms)\n";
        results.push_back(res);
    }

    if (config.test_name == "padded" || config.test_name == "all") {
        std::cout << ">> [2/3] Running Test 2: Padded & Aligned (Cache Line Isolated)..." << std::flush;
        auto res = run_padded_benchmark(config);
        std::cout << " Done. (" << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms)\n";
        results.push_back(res);
    }

    if (config.test_name == "local" || config.test_name == "all") {
        std::cout << ">> [3/3] Running Test 3: Local Thread Accumulation (Baseline)..." << std::flush;
        auto res = run_local_benchmark(config);
        std::cout << " Done. (" << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms)\n";
        results.push_back(res);
    }

    // 3. Print Results Summary & Speedup Analysis
    print_results_table(results, config);

    return 0;
}
