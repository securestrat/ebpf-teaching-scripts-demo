# System Cpuset Setup - Summary

## Objective
Created a system cpuset restricted to CPU 0 on both VMs (172.16.205.129 and 172.16.205.130).

## Implementation

### Script Created
- **[setup_system_cpuset.sh](file:///Users/vinit/Documents/Unix/setup_system_cpuset.sh)** - Automated setup script

### Deployment Steps

1. Deployed script to both VMs
2. Enabled cpuset controller in root cgroup
3. Created `/sys/fs/cgroup/system` cgroup
4. Configured cpuset to CPU 0
5. Created systemd service for persistence
6. Created helper script at `/usr/local/bin/move_system_processes.sh`

## Final Configuration

### VM 172.16.205.129
```
system:     CPUs=0, Effective=0 ✓
benchuser1: CPUs=0, Effective=0 ✓
benchuser2: CPUs=0, Effective=0 ✓
benchuser3: CPUs=1, Effective=1 ✓
benchuser4: CPUs=1, Effective=1 ✓
```

### VM 172.16.205.130
```
system:     CPUs=0, Effective=0 ✓
benchuser1: CPUs=0, Effective=0 ✓
benchuser2: CPUs=0, Effective=0 ✓
benchuser3: CPUs=1, Effective=1 ✓
benchuser4: CPUs=1, Effective=1 ✓
```

## Verification

Process affinity tests confirmed:
- **VM 172.16.205.129**: Test process (PID 34717) restricted to CPU 0 ✓
- **VM 172.16.205.130**: Test process (PID 13395) restricted to CPU 0 ✓

## CPU Allocation Overview

| Cgroup | CPU | Purpose |
|--------|-----|---------|
| `system` | 0 | System processes |
| `benchuser1` | 0 | User 1 benchmarks |
| `benchuser2` | 0 | User 2 benchmarks |
| `benchuser3` | 1 | User 3 benchmarks |
| `benchuser4` | 1 | User 4 benchmarks |

## Usage

### Move Process to System Cpuset
```bash
echo <PID> > /sys/fs/cgroup/system/cgroup.procs
```

### Move System Processes
```bash
sudo /usr/local/bin/move_system_processes.sh
```

### Verify Configuration
```bash
# Check cpuset
cat /sys/fs/cgroup/system/cpuset.cpus.effective

# Check process affinity
taskset -cp <PID>
```

## Documentation

- **[SYSTEM_CPUSET_README.md](file:///Users/vinit/Documents/Unix/SYSTEM_CPUSET_README.md)** - Detailed usage guide
- **[USER_SETUP_README.md](file:///Users/vinit/Documents/Unix/USER_SETUP_README.md)** - User cpuset documentation

## Status

✅ System cpuset created and verified on both VMs  
✅ All user cpusets reconfigured and verified  
✅ Process affinity tests passed  
✅ Documentation complete
