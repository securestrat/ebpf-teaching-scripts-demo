# Verification and Deployment Guide 🛠️

Since eBPF is a Linux-native kernel technology, you cannot run these scripts directly on macOS or Windows hosts. This guide outlines how to transfer the entire curriculum onto your target Linux VM/host and run verification tests on each lesson.

---

## 📦 Step 1: Deploy to Your Linux VM

If you have a Linux VM running (for instance, the one at IP `172.16.205.129` or another host):

```bash
# 1. Zip or tar the ebpf lessons locally on your host (run in workspace root)
tar -czf ebpf-lessons.tar.gz 01_helloworld 02_context_helpers 03_maps_hash 04_tracepoints 05_ring_buffer 06_uprobes README.md

# 2. Securely copy the archive to your target Linux VM
scp ebpf-lessons.tar.gz root@<VM_IP>:/tmp/

# 3. SSH into your VM
ssh root@<VM_IP>

# 4. Extract the curriculum
cd /tmp
tar -xzf ebpf-lessons.tar.gz
cd /tmp
```

---

## 🛠️ Step 2: Install VM Prerequisites

Make sure the Linux system has BCC and its dependencies installed:

```bash
# Install BCC, Python bindings, kernel headers, and build tools
sudo apt-get update
sudo apt-get install -y python3-bpfcc bpfcc-tools linux-headers-$(uname -r) clang llvm stress-ng

# Confirm debugfs is mounted (necessary for reading tracing events)
mount | grep debugfs
# If empty, run:
sudo mount -t debugfs debugfs /sys/kernel/debug
```

---

## 🧪 Step 3: Run Progressive Verification Tests

### Test 1: Verify Lesson 1 (`01_helloworld/hello.py`)
Run the script to verify dynamic kprobes and trace pipe compilation:
```bash
sudo python3 01_helloworld/hello.py
```
**Verification Trigger**: 
In another SSH window to the VM, run `ls` or `whoami`.
**Expected Output**:
You should see printouts in your hello.py terminal showing `Hello World! A new process/thread is spawning!`.

---

### Test 2: Verify Lesson 2 (`02_context_helpers/sys_execve_details.py`)
Verify that helper functions correctly capture the telemetry context (PID, TID, UID, Process name):
```bash
sudo python3 02_context_helpers/sys_execve_details.py
```
**Verification Trigger**:
Run `curl ipinfo.io` or standard commands.
**Expected Output**:
The terminal should output:
`[EVENT] curl (loader PID <pid>) triggered an execve:`
`   ↳ EXEC: PID=<pid>, TID=<tid>, UID=0, COMM=curl`

---

### Test 3: Verify Lesson 3 (`03_maps_hash/syscall_counter.py`)
Verify state preservation inside high-speed maps and live terminal rendering:
```bash
sudo python3 03_maps_hash/syscall_counter.py
```
**Verification Trigger**:
Run disk-heavy operations or benchmark commands (e.g. `dd if=/dev/zero of=/tmp/testfile bs=1M count=100` or `stress-ng --cpu 2 --timeout 10s`).
**Expected Output**:
The CLI screen will clear and display a beautiful updating list of process metrics. Press `Ctrl+C` to confirm that the final sorted summary prints out successfully!

---

### Test 4: Verify Lesson 4 (`04_tracepoints/trace_exec.py`)
Verify transition from dynamic kprobes to stable kernel tracepoint ABI structures:
```bash
sudo python3 04_tracepoints/trace_exec.py
```
**Verification Trigger**:
Run any command line statement.
**Expected Output**:
`[AUDIT] Process bash (PID <pid>) executed binary:`
`   ↳ /usr/bin/git`

---

### Test 5: Verify Lesson 5 (`05_ring_buffer/process_monitor.py`)
Verify state correlation across separate event boundaries and async Perf buffer callbacks:
```bash
sudo python3 05_ring_buffer/process_monitor.py
```
**Verification Trigger**:
Run commands that take time, like `sleep 0.5` or `sleep 2`.
**Expected Output**:
You will see process starts, their exit codes, parent PIDs, and high-precision execution times:
` 14:02:10 348235   348102   504.12 ms    0      sleep`
Verify that failing commands (e.g., trying to run a nonexistent command) result in an exit code of `1` or `127` in red text.

---

### Test 6: Verify Lesson 6 (`06_uprobes/bash_readline.py`)
Verify user-space dynamic instrumentation and return value tracking:
```bash
sudo python3 06_uprobes/bash_readline.py
```
**Verification Trigger**:
1. Open a new SSH/terminal session on the VM.
2. Ensure you are running bash (if not, type `bash`).
3. Type some commands in that bash session (e.g., `echo hello`, `ls -la`) and hit Enter.
**Expected Output**:
In the running `bash_readline.py` console, you will immediately see:
`[KEYSTROKE AUDIT] bash typed: 'echo hello'`
`[KEYSTROKE AUDIT] bash typed: 'ls -la'`

---

## 🔍 Troubleshooting Tips

1. **Missing Kernel Headers**: 
   If compilation fails with `linux/ptrace.h` not found, ensure you installed the headers for your *exact* running kernel version: `sudo apt-get install linux-headers-$(uname -r)`.
2. **Permission Denied**: 
   Ensure you run all loader scripts with `sudo` or as the `root` user. BPF calls require `CAP_BPF` / `CAP_SYS_ADMIN` privileges.
3. **VM Kernel Version**: 
   Ensure your VM runs a Linux kernel version of 4.15 or newer (Ubuntu 20.04 runs 5.4+, Ubuntu 22.04 runs 5.15+, both are perfect).

Happy eBPF learning! 🚀
