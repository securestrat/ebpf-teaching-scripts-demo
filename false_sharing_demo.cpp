/**
 * ============================================================================
 * CPU FALSE CACHE SHARING DEMO & BENCHMARK
 * ============================================================================
 *
 * What is False Sharing?
 * ----------------------
 * In modern multicore CPUs, memory is cached in fixed-size blocks called
 * "Cache Lines" (typically 64 bytes).
 *
 * When multiple CPU cores access or modify variables that reside on the SAME
 * 64-byte cache line:
 * 1. Even if Core 0 writes to Variable A and Core 1 writes to Variable B (logically independent),
 * 2. The CPU hardware cache coherency protocol (MESI / MOESI) must invalidate
 *    the ENTIRE cache line in Core 0's L1/L2 cache whenever Core 1 writes to it.
 * 3. This causes "Cache Line Bouncing" between CPU cores, stalling the CPU pipelines
 *    and destroying multithreaded scalability.
 *
 * The Fix:
 * --------
 * Align and pad each thread's data structure to 64 bytes (`alignas(64)`),
 * ensuring each core's counter resides in its own isolated cache line.
 * ============================================================================
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

// Cache line size on x86_64 / ARM64 (typically 64 bytes)
constexpr size_t CACHE_LINE_SIZE = 64;

// ----------------------------------------------------------------------------
// 1. DATA STRUCTURE WITH FALSE SHARING (Unpadded)
// ----------------------------------------------------------------------------
// sizeof(UnpaddedCounter) == 8 bytes.
// Up to 8 counters fit in a single 64-byte cache line!
// When multiple threads write to adjacent array elements, they thrash the cache line.
struct UnpaddedCounter {
    volatile uint64_t val{0};
};

// ----------------------------------------------------------------------------
// 2. DATA STRUCTURE FIXED WITH PADDING & ALIGNMENT
// ----------------------------------------------------------------------------
// sizeof(PaddedCounter) == 64 bytes, aligned on a 64-byte boundary.
// Every thread's counter sits in its OWN dedicated cache line.
// No cache invalidations or line bouncing occurs across cores.
struct alignas(CACHE_LINE_SIZE) PaddedCounter {
    volatile uint64_t val{0};
    // Explicit padding bytes to fill the 64-byte cache line
    uint8_t pad[CACHE_LINE_SIZE - sizeof(uint64_t)];
};

// ----------------------------------------------------------------------------
// 3. BASELINE: LOCAL ACCUMULATION (No shared memory writes during loop)
// ----------------------------------------------------------------------------
struct alignas(CACHE_LINE_SIZE) LocalResult {
    uint64_t val{0};
};

// ----------------------------------------------------------------------------
// BENCHMARK CONFIGURATION & RESULT STRUCTURES
// ----------------------------------------------------------------------------
enum class BenchmarkMode {
    FIXED_ITERATIONS,
    FIXED_DURATION
};

struct BenchmarkConfig {
    int num_threads = 4;
    BenchmarkMode mode = BenchmarkMode::FIXED_ITERATIONS;
    uint64_t iterations = 300'000'000ULL; // 300 million per thread
    double duration_seconds = 3.0;        // 3 seconds if time-based
    std::string test_name = "all";        // false, padded, local, all
};

struct BenchmarkResult {
    std::string name;
    double elapsed_ms;
    uint64_t total_operations;
    double mops_per_sec; // Million ops per second
};

// ----------------------------------------------------------------------------
// MEMORY LAYOUT DIAGNOSTIC
// ----------------------------------------------------------------------------
void print_memory_layout(int num_threads) {
    std::vector<UnpaddedCounter> unpadded(num_threads);
    std::vector<PaddedCounter> padded(num_threads);

    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "  MEMORY LAYOUT & CACHE LINE ANALYSIS (" << num_threads << " Threads, Cache Line = " << CACHE_LINE_SIZE << " bytes)\n";
    std::cout << std::string(80, '=') << "\n";

    std::cout << "\n[1] UNPADDED ARRAY (False Sharing Vulnerable - Struct Size: " << sizeof(UnpaddedCounter) << " bytes):\n";
    std::cout << "Thread | Memory Address     | Cache Line Index  | Offset in Line | Status\n";
    std::cout << "-------+--------------------+-------------------+----------------+-------------------------\n";
    for (int i = 0; i < num_threads; ++i) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(&unpadded[i].val);
        uintptr_t line_idx = addr / CACHE_LINE_SIZE;
        uintptr_t offset = addr % CACHE_LINE_SIZE;
        bool shares = (i > 0 && (addr / CACHE_LINE_SIZE == reinterpret_cast<uintptr_t>(&unpadded[i - 1].val) / CACHE_LINE_SIZE));
        
        std::cout << "  T" << std::setw(3) << std::left << i 
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << addr << std::dec << std::setfill(' ')
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << line_idx << std::dec << std::setfill(' ')
                  << " | " << std::right << std::setw(14) << offset 
                  << " | " << (shares ? "❌ SHARES cache line with T" + std::to_string(i - 1) : "⚡ First in cache line")
                  << "\n";
    }

    std::cout << "\n[2] PADDED & ALIGNED ARRAY (Fixed - Struct Size: " << sizeof(PaddedCounter) << " bytes):\n";
    std::cout << "Thread | Memory Address     | Cache Line Index   | Offset in Line | Status\n";
    std::cout << "-------+--------------------+--------------------+----------------+-------------------------\n";
    for (int i = 0; i < num_threads; ++i) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(&padded[i].val);
        uintptr_t line_idx = addr / CACHE_LINE_SIZE;
        uintptr_t offset = addr % CACHE_LINE_SIZE;

        std::cout << "  T" << std::setw(3) << std::left << i 
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << addr << std::dec << std::setfill(' ')
                  << " | 0x" << std::hex << std::right << std::setw(16) << std::setfill('0') << line_idx << std::dec << std::setfill(' ')
                  << " | " << std::right << std::setw(14) << offset 
                  << " | " << "✅ ISOLATED (Dedicated cache line)"
                  << "\n";
    }
    std::cout << std::string(80, '=') << "\n\n";
}

// ----------------------------------------------------------------------------
// TEST 1: FALSE SHARING (Unpadded)
// ----------------------------------------------------------------------------
BenchmarkResult run_unpadded_benchmark(const BenchmarkConfig& config) {
    std::vector<UnpaddedCounter> counters(config.num_threads);
    std::vector<std::thread> threads;
    threads.reserve(config.num_threads);

    std::atomic<bool> start_signal{false};
    std::atomic<bool> stop_signal{false};
    std::atomic<int> ready_count{0};

    auto worker_iterations = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {
            // Spin wait for synchronized start
        }
        for (uint64_t i = 0; i < config.iterations; ++i) {
            counters[id].val++;
        }
    };

    auto worker_duration = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {
            // Spin wait for synchronized start
        }
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

    // Wait for all threads to be ready
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
        "1. False Sharing (Unpadded / Contended)",
        elapsed_ms,
        total_ops,
        mops
    };
}

// ----------------------------------------------------------------------------
// TEST 2: FIXED WITH PADDING & ALIGNMENT
// ----------------------------------------------------------------------------
BenchmarkResult run_padded_benchmark(const BenchmarkConfig& config) {
    std::vector<PaddedCounter> counters(config.num_threads);
    std::vector<std::thread> threads;
    threads.reserve(config.num_threads);

    std::atomic<bool> start_signal{false};
    std::atomic<bool> stop_signal{false};
    std::atomic<int> ready_count{0};

    auto worker_iterations = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {
            // Spin wait for synchronized start
        }
        for (uint64_t i = 0; i < config.iterations; ++i) {
            counters[id].val++;
        }
    };

    auto worker_duration = [&](int id) {
        ready_count.fetch_add(1);
        while (!start_signal.load(std::memory_order_acquire)) {
            // Spin wait for synchronized start
        }
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

    // Wait for all threads to be ready
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

// ----------------------------------------------------------------------------
// TEST 3: LOCAL ACCUMULATION BASELINE (Optimal Local Register/Stack)
// ----------------------------------------------------------------------------
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
        
        // Prevent compiler from completely optimizing away the loop with volatile
        volatile uint64_t local_accum = 0;
        for (uint64_t i = 0; i < config.iterations; ++i) {
            local_accum++;
        }
        results[id].val = local_accum;
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

// ----------------------------------------------------------------------------
// PRESENTATION AND REPORTING
// ----------------------------------------------------------------------------
void print_results_table(const std::vector<BenchmarkResult>& results, const BenchmarkConfig& config) {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "  BENCHMARK RESULTS SUMMARY\n";
    std::cout << "  Threads: " << config.num_threads << " | ";
    if (config.mode == BenchmarkMode::FIXED_ITERATIONS) {
        std::cout << "Mode: Fixed Iterations (" << config.iterations << " ops/thread)\n";
    } else {
        std::cout << "Mode: Fixed Duration (" << config.duration_seconds << " seconds)\n";
    }
    std::cout << std::string(80, '=') << "\n";

    std::cout << std::left << std::setw(45) << "Test Implementation"
              << std::right << std::setw(12) << "Time (ms)"
              << std::setw(12) << "Total Ops"
              << std::setw(11) << "MOps/sec"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    double unpadded_time = 0.0;
    double unpadded_mops = 0.0;

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& res = results[i];
        if (i == 0) {
            unpadded_time = res.elapsed_ms;
            unpadded_mops = res.mops_per_sec;
        }

        std::cout << std::left << std::setw(45) << res.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(12) << res.elapsed_ms
                  << std::setw(12) << res.total_operations
                  << std::setw(11) << res.mops_per_sec
                  << "\n";
    }
    std::cout << std::string(80, '=') << "\n";

    // Speedup Comparison
    if (results.size() >= 2) {
        double padded_time = results[1].elapsed_ms;
        double padded_mops = results[1].mops_per_sec;

        double speedup = (config.mode == BenchmarkMode::FIXED_ITERATIONS)
            ? (unpadded_time / padded_time)
            : (padded_mops / unpadded_mops);

        std::cout << "\n🚀 PERFORMANCE GAIN ANALYSIS:\n";
        std::cout << "  • False Sharing Time : " << std::fixed << std::setprecision(2) << unpadded_time << " ms (" << unpadded_mops << " MOps/s)\n";
        std::cout << "  • Padded (Fixed) Time: " << std::fixed << std::setprecision(2) << padded_time << " ms (" << padded_mops << " MOps/s)\n";
        std::cout << "  • Performance Speedup: " << std::fixed << std::setprecision(2) << speedup << "x FASTER! (";
        if (speedup >= 1.0) {
            std::cout << "+" << (speedup - 1.0) * 100.0 << "% higher throughput)\n";
        } else {
            std::cout << "N/A)\n";
        }
        std::cout << "  • Explanation        : By padding each counter to 64 bytes (1 cache line),\n";
        std::cout << "                         we eliminated CPU L1/L2 cache coherency invalidations\n";
        std::cout << "                         and inter-core cache line bouncing!\n\n";
    }
}

// ----------------------------------------------------------------------------
// CLI PARSING & MAIN ENTRY POINT
// ----------------------------------------------------------------------------
void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  --test <name>        Test to run: 'false', 'padded', 'local', or 'all' (default: 'all')\n"
              << "  --threads <N>        Number of threads to run (default: 4, detected CPU cores: " << std::thread::hardware_concurrency() << ")\n"
              << "  --iterations <N>     Number of increments per thread (default: 300000000)\n"
              << "  --duration <sec>     Run time-based test for <sec> seconds (e.g. --duration 3.0)\n"
              << "  --help               Show this help message\n\n"
              << "Examples:\n"
              << "  " << prog_name << " --test all --threads 4 --iterations 300000000\n"
              << "  " << prog_name << " --test all --threads 8 --duration 3.0\n"
              << "  " << prog_name << " --test false --threads 4\n"
              << "  " << prog_name << " --test padded --threads 4\n";
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
        } else if (arg == "--threads" && i + 1 < argc) {
            config.num_threads = std::stoi(argv[++i]);
        } else if (arg == "--iterations" && i + 1 < argc) {
            config.iterations = std::stoull(argv[++i]);
            config.mode = BenchmarkMode::FIXED_ITERATIONS;
        } else if (arg == "--duration" && i + 1 < argc) {
            config.duration_seconds = std::stod(argv[++i]);
            config.mode = BenchmarkMode::FIXED_DURATION;
        } else if (arg == "--test" && i + 1 < argc) {
            config.test_name = argv[++i];
        }
    }

    if (config.num_threads < 1) {
        std::cerr << "Error: Thread count must be at least 1.\n";
        return 1;
    }

    std::cout << "\n" << std::string(80, '#') << "\n";
    std::cout << "       CPU FALSE SHARING & CACHE LINE PADDING BENCHMARK\n";
    std::cout << std::string(80, '#') << "\n";
    std::cout << "Hardware Concurrency: " << hw_threads << " Logical CPU cores\n";
    std::cout << "Configured Threads  : " << config.num_threads << "\n";
    std::cout << "Test Selection      : " << config.test_name << "\n";

    // 1. Always display the memory layout diagram
    print_memory_layout(config.num_threads);

    // 2. Run selected benchmarks
    std::vector<BenchmarkResult> results;

    if (config.test_name == "false" || config.test_name == "all") {
        std::cout << ">> Running Test 1: False Sharing (Unpadded / Contended)..." << std::flush;
        auto res = run_unpadded_benchmark(config);
        std::cout << " Done. (" << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms)\n";
        results.push_back(res);
    }

    if (config.test_name == "padded" || config.test_name == "all") {
        std::cout << ">> Running Test 2: Padded & Aligned (Cache Line Isolated)..." << std::flush;
        auto res = run_padded_benchmark(config);
        std::cout << " Done. (" << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms)\n";
        results.push_back(res);
    }

    if (config.test_name == "local" || config.test_name == "all") {
        std::cout << ">> Running Test 3: Local Thread Accumulation (Baseline)..." << std::flush;
        auto res = run_local_benchmark(config);
        std::cout << " Done. (" << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms)\n";
        results.push_back(res);
    }

    // 3. Print Results Summary & Speedup Analysis
    print_results_table(results, config);

    return 0;
}
