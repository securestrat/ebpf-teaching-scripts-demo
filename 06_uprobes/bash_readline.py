#!/usr/bin/env python3
"""
Lesson 6: User-Space Tracing (Uprobes/Uretprobes)
-------------------------------------------------
This script attaches a dynamic uretprobe to `/bin/bash`'s `readline` symbol.
Whenever a user finishes typing a command and presses Enter in any bash shell,
our eBPF program intercepts the return value pointer and logs the command string.

Concepts introduced:
1. Dynamic User-space Probes (Uprobes/Uretprobes).
2. Intercepting function exit return values using `PT_REGS_RC(ctx)`.
3. Reading memory from user-space processes safely.
4. Instrumenting shared binary symbols.
"""

import sys
import ctypes
from bcc import BPF

# ==============================================================================
# 1. KERNEL-SPACE C CODE
# ==============================================================================
bpf_program = """
#include <linux/ptrace.h>

// Perf Event Buffer to send command strings to user space
BPF_PERF_OUTPUT(events);

/**
 * capture_input - Hook function called when readline() in bash returns.
 * @ctx: Pointer to register context containing function state.
 */
int capture_input(struct pt_regs *ctx) {
    // PT_REGS_RC(ctx) retrieves the CPU register holding the function return value.
    // In the C ABI of readline(), the return value is a character pointer (char *)
    // pointing to the heap buffer holding the typed line.
    char *user_ptr = (char *)PT_REGS_RC(ctx);
    
    if (user_ptr == NULL) {
        return 0; // Guard against empty or NULL returns
    }
    
    // Declare our stack buffer. Keystrokes/commands are typically short,
    // so 128 bytes is plenty and satisfies the 512-byte eBPF stack limit.
    char command[128];
    
    // Copy the command string safely from bash's user-space memory address
    // space into our eBPF program stack.
    long res = bpf_probe_read_user_str(&command, sizeof(command), user_ptr);
    
    if (res >= 0) {
        // Send the raw byte string to user-space
        events.perf_submit(ctx, &command, sizeof(command));
    }
    
    return 0;
}
"""

# ==============================================================================
# 2. USER-SPACE PYTHON LOADER & PARSING LOGIC
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

print(f"{Colors.HEADER}{Colors.BOLD}=== eBPF Curriculum - Lesson 6: Uprobes & Uretprobes ==={Colors.ENDC}\n")
print(f"{Colors.BLUE}[*] Compiling user-space instrumentations and registers...{Colors.ENDC}")

try:
    b = BPF(text=bpf_program)
except Exception as e:
    print(f"{Colors.FAIL}[✗] Verifier rejected registers parsing!{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] Assembly verified.")
print(f"{Colors.BLUE}[*] Attaching uretprobe to /bin/bash:readline...{Colors.ENDC}")

try:
    # Attach a return probe (uretprobe) to intercept readline() inside the /bin/bash binary
    b.attach_uretprobe(name="/bin/bash", sym="readline", fn_name="capture_input")
except Exception as e:
    print(f"{Colors.FAIL}[✗] Hook failed! Are you running as root (sudo)?{Colors.ENDC}")
    print(f"    Also ensure /bin/bash exists and is compiled with readline.{Colors.ENDC}")
    print(e)
    sys.exit(1)

print(f"{Colors.GREEN}[✓] Keystroke hook is active!{Colors.ENDC}")
print(f"{Colors.WARNING}{Colors.BOLD}\n👉 HOW TO TRIGGER AUDIT LOGS: {Colors.ENDC}")
print(f"   1. Open another terminal shell session.")
print(f"   2. Ensure the shell is bash (run: 'bash' if in zsh/sh).")
print(f"   3. Type commands (e.g., 'whoami', 'pwd', 'ls') and press Enter.")
print(f"   4. Watch this terminal output to see the typed characters intercepted in real-time!")
print(f"\n{Colors.BLUE}[*] Listening for bash commands (Press Ctrl+C to stop)...{Colors.ENDC}\n")

# Define performance callback
def print_event(cpu, data, size):
    # The C code submitted raw bytes representing the character buffer.
    # We can decode it directly as string.
    try:
        # Copy raw memory bytes from the pointer address
        raw_bytes = ctypes.string_at(data, size)
        # Strip trailing null characters and decode
        command = raw_bytes.split(b'\x00')[0].decode('utf-8', 'replace').strip()
        
        # Don't log empty enters
        if len(command) > 0:
            print(f"{Colors.CYAN}[KEYSTROKE AUDIT]{Colors.ENDC} {Colors.GREEN}{Colors.BOLD}bash typed:{Colors.ENDC} '{Colors.BOLD}{command}{Colors.ENDC}'")
    except Exception as e:
        print(f"{Colors.FAIL}[✗] Error parsing raw keyboard event: {e}{Colors.ENDC}")

# Attach callback handler to "events" perf stream
b["events"].open_perf_buffer(print_event)

try:
    while True:
        # Loop and pull event streams asynchronously
        b.perf_buffer_poll()
except KeyboardInterrupt:
    print(f"\n{Colors.GREEN}[*] Dynamic uprobes detached successfully. Goodbye!{Colors.ENDC}")
    sys.exit(0)
