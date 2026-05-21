# Lesson 4: Stable Interfaces (Tracepoints) ⚓

In Lessons 1, 2, and 3, we hooked into the kernel using **kprobes** (kernel dynamic probes). In this lesson, we will learn why dynamic probes can be fragile for production, and transition to a robust, stable mechanism called **Tracepoints**. We will also learn how to safely read user-space strings (like file paths) inside our eBPF program.

---

## 🧠 Key Concepts

### 1. Kprobes vs. Tracepoints
When writing eBPF programs, choosing where to hook in the kernel is a crucial architectural decision.

| Feature | Kprobes (Dynamic Probes) | Tracepoints (Static Probes) |
| :--- | :--- | :--- |
| **Stability** | 🔴 **Low**. Hooks into arbitrary internal kernel functions. If a function gets renamed, refactored, or optimized out in a kernel update, your eBPF program **breaks**. | 🟢 **High**. Predefined, stable event markers embedded directly in the kernel source code. They are part of the stable Kernel ABI. |
| **Argument Access** | 🟡 **Difficult**. Requires inspecting register states (`pt_regs *ctx`) which varies between CPU architectures (x86_64, ARM64, etc.). | 🟢 **Easy**. Exposes a typed, stable struct containing all hook arguments, independent of architecture. |
| **Overhead** | 🟡 **Medium**. Adds breakpoint instructions dynamically, which has minor overhead. | 🟢 **Low**. Negligible overhead when disabled; highly optimized compiled branch instructions when enabled. |
| **Availability** | 🟢 **Infinite**. Can probe virtually any non-inlined function in the entire kernel. | 🟡 **Limited**. Only available where kernel developers explicitly added them (though there are thousands!). |

---

### 2. Discovering Tracepoints
You can discover all available tracepoints on your Linux system by browsing `/sys/kernel/debug/tracing/events/`.

For example, to inspect the system call execution tracepoint `sys_enter_execve`:
```bash
# View the fields and arguments of the tracepoint
cat /sys/kernel/debug/tracing/events/syscalls/sys_enter_execve/format
```

This returns the structure layout:
```text
format:
        field:unsigned short common_type;
        ...
        field:int __syscall_nr;
        field:const char * filename;
        field:const char *const * argv;
        field:const char *const * envp;
```
This tells us that the tracepoint provides `filename` (executable path), `argv` (argument array), and `envp` (environment variables).

---

### 3. eBPF Tracepoints in BCC
BCC makes using tracepoints incredibly clean using the `TRACEPOINT_PROBE` macro. It automatically generates the argument struct and makes it accessible via an `args` pointer:

```c
TRACEPOINT_PROBE(syscalls, sys_enter_execve) {
    // args->filename points to the binary being run
    // args->argv points to the arguments array
    ...
}
```

---

### 4. Reading User-Space Memory safely
In eBPF, kernel-space memory and user-space memory are strictly isolated.
- The `args->filename` pointer holds a **user-space** memory address.
- You **cannot** dereference or copy this pointer directly in C (e.g. `char *name = args->filename`). Doing so will cause the eBPF Verifier to reject the program to prevent kernel page faults.
- Instead, we must safely copy the string from user space into our eBPF program stack using the helper:
  `bpf_probe_read_user_str(dest_buf, size, src_ptr)`

> [!IMPORTANT]
> **Safety Constraints:**
> Always check the return value of `bpf_probe_read_user_str()`. A negative return value indicates a read failure (e.g., if the memory was swapped out or invalid).

---

## 💻 Code Walkthrough

In this lesson, we will hook into the `sys_enter_execve` tracepoint, safely copy the executed file name from user memory, and log it to the trace pipe:

```c
TRACEPOINT_PROBE(syscalls, sys_enter_execve) {
    char filename[256];
    
    // Safely copy the user-space filename string into our local buffer
    long res = bpf_probe_read_user_str(&filename, sizeof(filename), args->filename);
    
    if (res >= 0) {
        bpf_trace_printk("TRACEPOINT_EXEC: %s\\n", filename);
    }
    return 0;
}
```

---

## 🏃 Running the Script

On your Linux VM:

```bash
sudo python3 trace_exec.py
```

Run commands in another terminal. You will see the **absolute path** of every binary run on the system:

```text
  [EVENT] process_monitor: Launched /usr/bin/git
  [EVENT] process_monitor: Launched /usr/bin/man
  [EVENT] process_monitor: Launched /bin/bash
```

Let's move on to **[Lesson 5: Event Rings - Perf Output](file:///Users/vinit/.gemini/antigravity/worktrees/ebpf/ebpf-teaching-scripts-demo/05_ring_buffer)** to stream high-frequency structured events asynchronously!
