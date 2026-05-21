#!/usr/bin/env python3
"""
Lesson 2: Context and Helpers
-----------------------------
This script hooks into `sys_execve` (executing a program) and reads process-specific
information (PID, UID, comm/process name) using eBPF Helper Functions.

Concepts introduced:
1. Accessing register contexts (`pt_regs`).
2. Extracting process metadata (PID, UID).
3. The distinction between Linux Kernel PID/TGID vs User PID/TID.
4. Using standard helper functions: `bpf_get_current_pid_tgid()`, `bpf_get_current_uid_gid()`, `bpf_get_current_comm()`.
"""

import sys
from bcc import BPF

# ==============================================================================
# 1. KERNEL-SPACE C CODE
# ==============================================================================
bpf_program = """
#include <linux/ptrace.h>
#include <linux/sched.h> // Needed for task declarations if dereferencing fields

/**
 * log_exec - Hook function executed whenever sys_execve starts.
 * @ctx: The ptrace CPU register context struct.
 */
int log_exec(struct pt_regs *ctx) {
    // A. Retrieve PID & TGID
    // bpf_get_current_pid_tgid() returns a packed 64-bit integer.
    // - Top 32 bits = Thread Group ID (TGID) -> This is the user-space "PID"
    // - Bottom 32 bits = Process ID (PID) -> This is the user-space "TID" (Thread ID)
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32; // Bitwise shift right to extract the user-space PID
    u32 tid = pid_tgid;       // Truncate to 32 bits to get the thread ID (TID)
    
    // B. Retrieve UID & GID
    // bpf_get_current_uid_gid() returns a packed 64-bit integer.
    // - Bottom 32 bits = User ID (UID)
    // - Top 32 bits = Group ID (GID)
    u64 uid_gid = bpf_get_current_uid_gid();
    u32 uid = uid_gid;        // Truncate to extract the UID
    
    // C. Retrieve Process Name
    // Comm is the name of the executable (truncated to 16 bytes in the kernel task_struct).
    char comm[16];
    bpf_get_current_comm(&comm, sizeof(comm));
    
    // Write out the fields to the trace pipe.
    // Note that bpf_trace_printk only supports up to 3 arguments after the format string.
    bpf_trace_printk("EXEC: PID=%d, UID=%d, COMM=%s\\n", pid, uid, comm);
    
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

print(f"{Colors.HEADER}{Colors.BOLD}=== eBPF Curriculum - Lesson 2: Context & Helper Functions ==={Colors.ENDC}\n")

print(f"{Colors.BLUE}[*] Compiling eBPF program and checking with the Verifier...{Colors.ENDC}")
try:
    # Initialize the compiler and verifier
    b = BPF(text=bpf_program)
except Exception as e:
    print(f"{Colors.FAIL}[✗] Verifier rejected the code!{Colors.ENDC}")
    print(e)
    sys.exit(1)
print(f"{Colors.GREEN}[✓] eBPF program compiled and verified successfully.{Colors.ENDC}")

# Locate the hook point
execve_fn = b.get_syscall_fnname("execve")
print(f"{Colors.BLUE}[*] Attaching entry kprobe to '{execve_fn}'...{Colors.ENDC}")

try:
    b.attach_kprobe(event=execve_fn, fn_name="log_exec")
except Exception as e:
    print(f"{Colors.FAIL}[✗] Failed to attach kprobe! Are you running as root (sudo)?{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] eBPF program is successfully active!{Colors.ENDC}")
print(f"{Colors.WARNING}{Colors.BOLD}\n👉 RUN A COMMAND TO TEST: {Colors.ENDC}")
print(f"   Launch a new program in another terminal (e.g., 'curl ipinfo.io' or 'cat /etc/hosts').")
print(f"   You will see the process name (COMM), its PID, and the user (UID) who triggered it.")
print(f"\n{Colors.BLUE}[*] Reading from trace pipe (Press Ctrl+C to stop)...{Colors.ENDC}\n")

# Process lines from the trace pipe and make the display beautiful.
# Instead of raw trace_print(), we will format the raw lines ourselves.
try:
    while True:
        try:
            task, pid, cpu, flags, ts, msg = b.trace_fields()
        except ValueError:
            # Handle any unpacking/format mismatch gracefully
            continue
            
        # Decode fields (BCC yields bytes in Python 3)
        task_name = task.decode('utf-8', 'replace')
        msg_str = msg.decode('utf-8', 'replace').strip()
        
        # Only print our specific "EXEC:" lines
        if "EXEC:" in msg_str:
            # Parse the message we generated: "EXEC: PID=X, UID=Y, COMM=Z"
            # Format nicely for the terminal
            print(f"{Colors.GREEN}[EVENT]{Colors.ENDC} {Colors.BOLD}{task_name}{Colors.ENDC} (loader PID {pid}) triggered an execve:")
            print(f"   ↳ {Colors.BLUE}{msg_str}{Colors.ENDC}")
except KeyboardInterrupt:
    print(f"\n{Colors.GREEN}[*] Detaching kprobe and exiting. Goodbye!{Colors.ENDC}")
    sys.exit(0)
