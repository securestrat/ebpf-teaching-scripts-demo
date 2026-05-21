#!/usr/bin/env python3
"""
Lesson 4: Tracepoints & User-Space Reads
----------------------------------------
This script hooks into the `sys_enter_execve` tracepoint to audit launched executables.
Instead of fragile dynamic kprobes, we use stable kernel tracepoints.

Concepts introduced:
1. Stable tracepoint hooks (`TRACEPOINT_PROBE`).
2. Discovering tracepoint structures in /sys/kernel/debug/tracing/events/.
3. Safe retrieval of user-space strings via `bpf_probe_read_user_str()`.
4. High-performance tracepoint arg matching.
"""

import sys
from bcc import BPF

# ==============================================================================
# 1. KERNEL-SPACE C CODE
# ==============================================================================
bpf_program = """
/**
 * TRACEPOINT_PROBE - Macro to define tracepoint handlers.
 * @category: The tracepoint category (found in /sys/kernel/debug/tracing/events/<category>/)
 * @event: The event name.
 * 
 * This macro automatically declares a structured `args` pointer containing the
 * stable fields defined by this specific kernel event.
 */
TRACEPOINT_PROBE(syscalls, sys_enter_execve) {
    // Declare a buffer on the eBPF stack to store the filename string.
    // eBPF stack limit is strict (512 bytes total), so keep local arrays small!
    char filename[256];
    
    // args->filename is a pointer containing a user-space address.
    // We cannot read it directly. We MUST use bpf_probe_read_user_str to safely
    // copy the null-terminated string from user space into our stack buffer.
    // 
    // Returns the string length on success, or a negative error code on failure.
    long ret = bpf_probe_read_user_str(&filename, sizeof(filename), args->filename);
    
    // Check if the read succeeded
    if (ret >= 0) {
        // Output the read file name to the trace pipe
        bpf_trace_printk("TP_EXEC: %s\\n", filename);
    } else {
        bpf_trace_printk("TP_EXEC: [Error reading filename]\\n");
    }
    
    return 0;
}
"""

# ==============================================================================
# 2. USER-SPACE LOADER LOGIC
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

print(f"{Colors.HEADER}{Colors.BOLD}=== eBPF Curriculum - Lesson 4: Tracepoints ==={Colors.ENDC}\n")
print(f"{Colors.BLUE}[*] Compiling eBPF program with TRACEPOINT macros...{Colors.ENDC}")

try:
    # Compile tracepoint probe
    b = BPF(text=bpf_program)
except Exception as e:
    print(f"{Colors.FAIL}[✗] Verifier rejected the code!{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] eBPF program verified. Tracepoint registered in kernel.{Colors.ENDC}")
print(f"{Colors.BLUE}[*] Listening for 'syscalls:sys_enter_execve' events...{Colors.ENDC}")
print(f"{Colors.GREEN}[✓] Success! Dynamic kprobes bypassed; now using stable kernel API.{Colors.ENDC}")
print(f"{Colors.WARNING}{Colors.BOLD}\n👉 TEST THE STABILITY: {Colors.ENDC}")
print(f"   Launch commands in another terminal (e.g., 'grep', 'mkdir test', 'rmdir test').")
print(f"   Notice that we extract the *exact* binary path, even if it is a long user-space string!")
print(f"\n{Colors.BLUE}[*] Reading from trace pipe (Press Ctrl+C to stop)...{Colors.ENDC}\n")

try:
    # Parse events from the trace pipe and format
    while True:
        try:
            task, pid, cpu, flags, ts, msg = b.trace_fields()
        except ValueError:
            # Handle any unpacking/format mismatch gracefully
            continue
            
        task_name = task.decode('utf-8', 'replace')
        msg_str = msg.decode('utf-8', 'replace').strip()
        
        # Look for our custom prefix
        if "TP_EXEC:" in msg_str:
            filename = msg_str.replace("TP_EXEC: ", "")
            
            # Print beautiful execution logs
            print(f"{Colors.CYAN}[AUDIT]{Colors.ENDC} Process {Colors.BOLD}{task_name}{Colors.ENDC} (PID {pid}) executed binary:")
            print(f"   ↳ {Colors.GREEN}{Colors.BOLD}{filename}{Colors.ENDC}")
except KeyboardInterrupt:
    print(f"\n{Colors.GREEN}[*] Detaching tracepoint and exiting. Goodbye!{Colors.ENDC}")
    sys.exit(0)
