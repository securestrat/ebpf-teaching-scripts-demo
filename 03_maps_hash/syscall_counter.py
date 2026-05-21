#!/usr/bin/env python3
"""
Lesson 3: eBPF Maps (Hash Maps)
-------------------------------
This script hooks into the `sys_write` system call, aggregating statistics on
the number of calls and total bytes written per process. The kernel records
the telemetry inside an eBPF Hash Map, which the Python script periodically
polls to display a gorgeous real-time CLI dashboard.

Concepts introduced:
1. eBPF Maps (specifically `BPF_HASH`).
2. Map declarations and structures in C.
3. Checking pointers against NULL (Verifier safety).
4. Periodic polling of maps from user-space Python.
5. Printing live summaries.
"""

import sys
import time
import os
from bcc import BPF

# ==============================================================================
# 1. KERNEL-SPACE C CODE
# ==============================================================================
bpf_program = """
#include <linux/ptrace.h>

// A custom structure to store multiple stats for a single process
struct process_stats {
    u64 write_count;    // Cumulative count of write calls
    u64 bytes_written;  // Cumulative bytes written
    char comm[16];      // Process command name
};

// Declare our eBPF Hash Map.
// - Name: stats_map
# // - Key type: u32 (Process ID)
// - Value type: struct process_stats
BPF_HASH(stats_map, u32, struct process_stats);

/**
 * count_writes - Triggered on write syscall entry.
 * @ctx: Register context.
 * @fd: File descriptor being written to.
 * @buf: User space buffer pointing to data.
 * @count: Number of bytes requested to write.
 */
int count_writes(struct pt_regs *ctx, int fd, const void *buf, size_t count) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // Look up the process in our map
    struct process_stats *stats = stats_map.lookup(&pid);
    
    if (stats != NULL) {
        // Safe execution: the verifier knows stats is NOT null.
        // Increment the stats atomically/safely.
        stats->write_count++;
        stats->bytes_written += count;
    } else {
        // First time seeing this PID. Create and initialize a new struct.
        struct process_stats new_stats = {
            .write_count = 1,
            .bytes_written = count
        };
        // Fetch the name of the process and write it to our structure
        bpf_get_current_comm(&new_stats.comm, sizeof(new_stats.comm));
        
        // Add to our hash map: stats_map[pid] = new_stats
        stats_map.update(&pid, &new_stats);
    }
    
    return 0;
}
"""

# ==============================================================================
# 2. USER-SPACE LOADER & RENDERING LOGIC
# ==============================================================================
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    CYAN = '\033[96m'

def format_bytes(b_count):
    """Formats bytes to human readable sizes."""
    if b_count < 1024:
        return f"{b_count} B"
    elif b_count < 1024 * 1024:
        return f"{b_count / 1024:.2f} KB"
    else:
        return f"{b_count / (1024 * 1024):.2f} MB"

print(f"{Colors.HEADER}{Colors.BOLD}=== eBPF Curriculum - Lesson 3: eBPF Maps ==={Colors.ENDC}\n")
print(f"{Colors.BLUE}[*] Compiling and validating with the Verifier...{Colors.ENDC}")

try:
    # Compile the code
    b = BPF(text=bpf_program)
except Exception as e:
    print(f"{Colors.FAIL}[✗] Verifier rejected the code!{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] Success! eBPF Map 'stats_map' allocated in kernel memory.{Colors.ENDC}")

# Attach to the sys_write system call
write_syscall = b.get_syscall_fnname("write")
print(f"{Colors.BLUE}[*] Attaching kprobe to '{write_syscall}'...{Colors.ENDC}")

try:
    b.attach_kprobe(event=write_syscall, fn_name="count_writes")
except Exception as e:
    print(f"{Colors.FAIL}[✗] Failed to attach kprobe! Are you running as root (sudo)?{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] Monitoring active. Drawing dashboard every 1s...{Colors.ENDC}")
time.sleep(1.5)

# Retrieve a handle to our map. BPF objects act like standard Python dictionaries!
stats_map = b["stats_map"]

try:
    while True:
        # Clear the terminal screen dynamically
        os.system('clear' if os.name == 'posix' else 'cls')
        
        print(f"{Colors.CYAN}{Colors.BOLD}========================================================================{Colors.ENDC}")
        print(f"             📊 {Colors.BOLD}eBPF LIVE WRITE TELEMETRY DASHBOARD{Colors.ENDC} (Ctrl+C to Exit)")
        print(f"{Colors.CYAN}========================================================================{Colors.ENDC}")
        print(f" {Colors.BOLD}{'PID':<10} {'PROCESS':<20} {'WRITE CALLS':<15} {'BYTES WRITTEN':<20}{Colors.ENDC}")
        print(f"{Colors.CYAN}------------------------------------------------------------------------{Colors.ENDC}")
        
        # Pull map entries. Note: we read directly from the dict!
        # In python, stats_map.items() gives (key, value) where key is a CTypes u32,
        # and value is a CTypes process_stats struct.
        entries = []
        for pid, stats in stats_map.items():
            entries.append((
                pid.value,
                stats.comm.decode('utf-8', 'replace'),
                stats.write_count,
                stats.bytes_written
            ))
        
        # Sort by total write count descending
        entries.sort(key=lambda x: x[2], reverse=True)
        
        # Print top 15 entries
        for pid, comm, count, raw_bytes in entries[:15]:
            print(f" {pid:<10} {comm:<20} {count:<15,} {format_bytes(raw_bytes):<20}")
            
        if len(entries) == 0:
            print(f"\n      {Colors.WARNING}No writes detected yet. Run disk IO or shell commands!{Colors.ENDC}\n")
            
        print(f"{Colors.CYAN}========================================================================{Colors.ENDC}")
        time.sleep(1)

except KeyboardInterrupt:
    print(f"\n{Colors.GREEN}[*] Stopping and cleaning up...{Colors.ENDC}")
    
    # Detach and show a final report
    print(f"\n{Colors.BOLD}📋 FINAL CONSOLIDATED STATISTICS REPORT:{Colors.ENDC}")
    print(f"{Colors.CYAN}------------------------------------------------------------------------{Colors.ENDC}")
    print(f" {'PID':<10} {'PROCESS':<20} {'WRITE CALLS':<15} {'BYTES WRITTEN':<20}")
    print(f"{Colors.CYAN}------------------------------------------------------------------------{Colors.ENDC}")
    
    final_entries = []
    for pid, stats in stats_map.items():
        final_entries.append((
            pid.value,
            stats.comm.decode('utf-8', 'replace'),
            stats.write_count,
            stats.bytes_written
        ))
    
    final_entries.sort(key=lambda x: x[3], reverse=True) # Sort final by bytes written
    
    for pid, comm, count, raw_bytes in final_entries[:20]:
        print(f" {pid:<10} {comm:<20} {count:<15,} {format_bytes(raw_bytes):<20}")
        
    print(f"{Colors.CYAN}------------------------------------------------------------------------{Colors.ENDC}")
    print(f"{Colors.GREEN}[*] eBPF program unloaded successfully. Goodbye!{Colors.ENDC}")
    sys.exit(0)
