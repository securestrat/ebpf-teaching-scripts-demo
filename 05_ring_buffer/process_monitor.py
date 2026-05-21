#!/usr/bin/env python3
"""
Lesson 5: Performance Buffers (BPF_PERF_OUTPUT)
----------------------------------------------
This script monitors process start and exit lifecycles. It uses a hash map to
record a process's start time, correlates it on exit to compute total duration,
and sends structured records asynchronously to user space via a Perf Buffer.

Concepts introduced:
1. High-speed event ring buffers (`BPF_PERF_OUTPUT`).
2. Correlating entry and exit events using Maps.
3. Accessing parent process details from `task_struct`.
4. Pushing structured custom structs from kernel-space.
5. Asynchronous polling callback handlers in Python using `ctypes`.
"""

import sys
import time
from bcc import BPF
import ctypes

# ==============================================================================
# 1. KERNEL-SPACE C CODE
# ==============================================================================
bpf_program = """
#include <linux/sched.h> // Declarations of task_struct and parents

// Custom C struct passed from the kernel to user-space
struct process_event {
    u32 pid;            // Process ID (TGID)
    u32 ppid;           // Parent Process ID
    u64 duration_ns;    // Cumulative execution duration in nanoseconds
    char comm[16];      // Process name
    int exit_code;      // Process exit code
};

// Map to hold timestamps keyed by PID (tgid)
BPF_HASH(start_times, u32, u64);

// Declare the Perf Buffer named "events"
BPF_PERF_OUTPUT(events);

/**
 * trace_exec_start - Hooks execve start to record timestamp.
 */
TRACEPOINT_PROBE(syscalls, sys_enter_execve) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u64 start_ns = bpf_ktime_get_ns();
    
    // Save start time in our hash map
    start_times.update(&pid, &start_ns);
    return 0;
}

/**
 * trace_exit - Hooks exit to compute elapsed time and submit event.
 */
TRACEPOINT_PROBE(sched, sched_process_exit) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // 1. Retrieve the process start time
    u64 *start_ns = start_times.lookup(&pid);
    
    if (start_ns == NULL) {
        // Process started before we hooked in, or map failed
        return 0;
    }
    
    // 2. Calculate execution duration
    u64 duration_ns = bpf_ktime_get_ns() - *start_ns;
    
    // 3. Create the event instance
    struct process_event event = {
        .pid = pid,
        .duration_ns = duration_ns
    };
    
    // Get the task_struct to extract parent information and exit code
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    
    // real_parent is a pointer to the parent task.
    // tgid holds the thread group id, which is the user-space process ID (PID)
    event.ppid = task->real_parent->tgid;
    
    // Copy the command name
    bpf_get_current_comm(&event.comm, sizeof(event.comm));
    
    // The kernel stores the exit code shifted. Shift it down by 8 bits for the standard code
    event.exit_code = task->exit_code >> 8;
    
    // 4. Submit the struct to the Perf event buffer
    // args: Context pointer provided by the tracepoint macro
    // &event: Pointer to our custom struct
    // sizeof(event): Struct footprint size
    events.perf_submit(args, &event, sizeof(event));
    
    // 5. Clean up map to prevent leaks!
    start_times.delete(&pid);
    
    return 0;
}
"""

# ==============================================================================
# 2. USER-SPACE PYTHON LOADER & STRUCTURE PARSING
# ==============================================================================

# Define our ctypes struct equivalent to the C struct ProcessEvent.
# This maps the binary byte structure perfectly in Python memory.
class ProcessEvent(ctypes.Structure):
    _fields_ = [
        ("pid", ctypes.c_uint32),
        ("ppid", ctypes.c_uint32),
        ("duration_ns", ctypes.c_uint64),
        ("comm", ctypes.c_char * 16),
        ("exit_code", ctypes.c_int)
    ]

# Terminal styling
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    CYAN = '\033[96m'

print(f"{Colors.HEADER}{Colors.BOLD}=== eBPF Curriculum - Lesson 5: Perf buffers ==={Colors.ENDC}\n")
print(f"{Colors.BLUE}[*] Compiling C structures and verification...{Colors.ENDC}")

try:
    b = BPF(text=bpf_program)
except Exception as e:
    print(f"{Colors.FAIL}[✗] Verifier rejected structural mapping!{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] Success! Asymmetric structures verified.{Colors.ENDC}")
print(f"{Colors.BLUE}[*] Initializing event streams...{Colors.ENDC}")

# Print Table Header
print(f"\n{Colors.CYAN}{Colors.BOLD}========================================================================================{Colors.ENDC}")
print(f"               🏁 {Colors.BOLD}eBPF REAL-TIME PROCESS LIFECYCLE AUDITOR{Colors.ENDC}")
print(f"{Colors.CYAN}========================================================================================{Colors.ENDC}")
print(f" {Colors.BOLD}{'TIME':<8} {'PID':<8} {'PPID':<8} {'DURATION':<15} {'EXIT':<6} {'COMMAND':<20}{Colors.ENDC}")
print(f"{Colors.CYAN}----------------------------------------------------------------------------------------{Colors.ENDC}")

# Define the callback function that will run when events arrive.
# - cpu: the CPU index that triggered the event.
# - data: binary buffer pointer containing the C structure bytes.
# - size: the size of the buffer.
def print_event(cpu, data, size):
    # Cast the raw data pointer to our typed Python structure
    event = ctypes.cast(data, ctypes.POINTER(ProcessEvent)).contents
    
    # Format current wall clock time
    time_str = time.strftime("%H:%M:%S")
    
    # Human-friendly duration formatting
    dur_ms = event.duration_ns / 1000000.0 # Convert ns to ms
    if dur_ms < 1.0:
        duration_str = f"{event.duration_ns / 1000.0:.2f} μs"
    elif dur_ms < 1000.0:
        duration_str = f"{dur_ms:.2f} ms"
    else:
        duration_str = f"{dur_ms / 1000.0:.2f} s"
        
    comm_name = event.comm.decode('utf-8', 'replace')
    
    # Color-code based on exit code (success/fail)
    exit_color = Colors.GREEN if event.exit_code == 0 else Colors.FAIL
    
    print(f" {time_str:<8} {event.pid:<8} {event.ppid:<8} {duration_str:<15} {exit_color}{event.exit_code:<6}{Colors.ENDC} {Colors.BOLD}{comm_name}{Colors.ENDC}")

# Register our callback on the BPF object's "events" Perf Buffer
# - "events": the name of the BPF_PERF_OUTPUT map in the C code
# - print_event: the callback function
b["events"].open_perf_buffer(print_event)

print(f"{Colors.GREEN}[✓] Event listener attached. Press Ctrl+C to exit.{Colors.ENDC}\n")

try:
    while True:
        # Poll for new events from the circular ring buffers.
        # This blocks briefly and triggers print_event() whenever events are present.
        b.perf_buffer_poll()
except KeyboardInterrupt:
    print(f"\n{Colors.CYAN}========================================================================================{Colors.ENDC}")
    print(f"{Colors.GREEN}[*] Unloading BPF program and exiting. Goodbye!{Colors.ENDC}")
    sys.exit(0)
