#!/usr/bin/env python3
"""
Lesson 1: Hello World in eBPF
-----------------------------
This script demonstrates how to compile and load an eBPF program into the kernel
and attach it to the `sys_clone` system call entry using a kprobe.

Concepts introduced:
1. Dynamic compilation (BCC).
2. kprobes (kernel probes).
3. The debug trace pipe (`bpf_trace_printk`).
4. Userspace loader basics.
"""

import sys
from bcc import BPF

# ==============================================================================
# 1. KERNEL-SPACE C CODE
# ==============================================================================
# This C code runs directly inside the Linux kernel context.
# We embed it as a multi-line string in our Python script. BCC takes this string,
# compiles it using Clang/LLVM, verifies its safety, and loads it into memory.
bpf_program = """
#include <linux/ptrace.h>

/**
 * hello - The eBPF function triggered on system call entry.
 * @ctx: Pointer to registers containing function arguments (registers state).
 * 
 * Note: eBPF functions must have a return value (usually 0).
 */
int hello(void *ctx) {
    // bpf_trace_printk is a kernel helper that prints a string to the
    # // global debug trace pipe: /sys/kernel/debug/tracing/trace_pipe
    bpf_trace_printk("Hello World! A new process/thread is spawning!\\n");
    return 0;
}
"""

# ==============================================================================
# 2. USER-SPACE LOADER LOGIC
# ==============================================================================
# The Python script runs in user-space. It manages compilation, loading,
# hooking, and reading back outputs from the kernel.

# Terminal styling helper
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

print(f"{Colors.HEADER}{Colors.BOLD}=== eBPF Curriculum - Lesson 1: Hello World ==={Colors.ENDC}\n")

print(f"{Colors.BLUE}[*] Compiling eBPF program and loading into the kernel...{Colors.ENDC}")
try:
    # Initialize the BPF object. This triggers the Clang/LLVM inline compilation.
    # The BPF verifier runs at this step, validating bytecode instruction paths,
    # ensuring no out-of-bounds memory accesses or infinite loops.
    b = BPF(text=bpf_program)
except Exception as e:
    print(f"{Colors.FAIL}[✗] Compilation/Verification failed!{Colors.ENDC}")
    print(e)
    sys.exit(1)
print(f"{Colors.GREEN}[✓] Bytecode compiled and verified successfully.{Colors.ENDC}")

# Find the architecture-specific function name of the "clone" system call.
# E.g. on x86_64, "clone" maps to "__x64_sys_clone".
syscall_fn = b.get_syscall_fnname("clone")
print(f"{Colors.BLUE}[*] Attaching kprobe to kernel function '{syscall_fn}'...{Colors.ENDC}")

try:
    # Attach our eBPF C function "hello" to the "clone" syscall entry hook.
    b.attach_kprobe(event=syscall_fn, fn_name="hello")
except Exception as e:
    print(f"{Colors.FAIL}[✗] Failed to attach kprobe! Are you running as root (sudo)?{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] eBPF program is fully loaded and attached!{Colors.ENDC}")
print(f"{Colors.WARNING}{Colors.BOLD}\n👉 HOW TO TRIGGER EVENTS: {Colors.ENDC}")
print(f"   Open another terminal and run some commands (e.g., 'ls', 'curl google.com', 'ps').")
print(f"   Every time a command is executed, a new process/thread clones, triggering our script.")
print(f"\n{Colors.BLUE}[*] Reading from trace pipe (Press Ctrl+C to stop)...{Colors.ENDC}\n")

try:
    # This helper function reads the kernel's trace_pipe (/sys/kernel/debug/tracing/trace_pipe)
    # and outputs the lines to standard output.
    # Output format is: TASK-PID [CPU#] Flags TIMESTAMP: FUNCTION: MESSAGE
    b.trace_print()
except KeyboardInterrupt:
    print(f"\n{Colors.GREEN}[*] Detaching kprobe and exiting. Goodbye!{Colors.ENDC}")
    sys.exit(0)
