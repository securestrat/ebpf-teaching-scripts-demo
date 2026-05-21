# Lesson 2: Context & Helper Functions 🔍

In Lesson 1, we ran a simple program that fired on process clone events. In this lesson, we will hook into **`sys_execve`**—the system call invoked when a program executes another binary. We will capture details of *who* is executing *what*, using eBPF **Helper Functions** to read the event context.

---

## 🧠 Key Concepts

### 1. The Context Pointer (`ctx`)
Every eBPF program is passed a pointer as its first argument (usually called `ctx` or `regs`). 
- For kprobes, this points to a `struct pt_regs`, which holds the state of the CPU registers at the exact moment the probe was triggered.
- For tracepoints, this points to a specific tracepoint argument structure.
- Because kernel memory is strictly isolated from eBPF space, you **cannot** dereference pointers within `ctx` directly like normal C code (e.g. `ctx->ax`). The eBPF Verifier will reject the program! Instead, we must use safe, audited **Helper Functions**.

---

### 2. eBPF Helper Functions
eBPF programs cannot call arbitrary kernel functions because doing so could crash the kernel. Instead, the kernel exposes a stable, audited API of **Helper Functions**.

In this lesson, we will use three critical helper functions:

#### A. `bpf_get_current_pid_tgid()`
Returns a 64-bit integer representing the ID of the calling task:
- **Upper 32 bits**: The Thread Group ID (TGID). In user-space terminology, this is the actual **Process ID (PID)**.
- **Lower 32 bits**: The Process ID (PID). In user-space terminology, this is the **Thread ID (TID)**.

> [!IMPORTANT]
> **Kernel vs. User Terminology Collision:**
> In the Linux Kernel, a thread is called a "process" (represented by `task_struct`), and a group of threads sharing an address space is called a "Thread Group".
> - **Kernel PID** = User TID (Thread ID)
> - **Kernel TGID** = User PID (Process ID)
> E.g., to get the standard process ID, we must shift the returned value right by 32 bits: `pid_tgid >> 32`.

#### B. `bpf_get_current_uid_gid()`
Returns a 64-bit integer representing the user credentials:
- **Lower 32 bits**: The User ID (**UID**) of the process.
- **Upper 32 bits**: The Group ID (**GID**) of the process.

#### C. `bpf_get_current_comm(char *buf, size_t size)`
Fills the character buffer `buf` with the name of the calling process executable (known as the "comm" field in the task struct). The buffer must be at least 16 bytes (the maximum length of a task's comm in Linux, including the null terminator `\0`).

---

### 3. Tracepoint/Probe Hook: `sys_execve`
We are hooking into `sys_execve`. This is the system call that replaces the current process image with a new process image. Almost every shell command run by a user triggers `sys_execve`.

---

## 💻 Code Walkthrough

### Kernel C Code
```c
int log_exec(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 uid_gid = bpf_get_current_uid_gid();
    
    // Extract PID (upper 32-bits) and UID (lower 32-bits)
    u32 pid = pid_tgid >> 32;
    u32 uid = uid_gid;
    
    char comm[16];
    // Safely copy process name into our comm buffer
    bpf_get_current_comm(&comm, sizeof(comm));
    
    // Log the gathered fields to the trace pipe
    bpf_trace_printk("EXEC: PID=%d, UID=%d, COMM=%s\\n", pid, uid, comm);
    return 0;
}
```

---

## 🏃 Running the Script

On your Linux VM:

```bash
# Run with root privileges
sudo python3 sys_execve_details.py
```

Now execute some programs (e.g. run `ls`, `whoami`, `ping -c 1 localhost`) in another terminal. In your eBPF output, you will see exactly which PID and UID ran those commands:

```text
      bpf_helper-345388 [001] d... 12910.158421: bpf_trace_printk: EXEC: PID=345392, UID=1000, COMM=whoami
```

Let's proceed to **[Lesson 3: eBPF Maps](file:///Users/vinit/.gemini/antigravity/worktrees/ebpf/ebpf-teaching-scripts-demo/03_maps_hash)** to build a live statistics dashboard without using the debug trace pipe!
