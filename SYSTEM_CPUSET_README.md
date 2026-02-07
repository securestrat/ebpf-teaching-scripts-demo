# System Cpuset Configuration

## Overview

A system cpuset has been created and configured on both VMs to restrict system processes to **CPU 0**. This provides CPU isolation, allowing CPU 1 to be dedicated to user workloads.

## Configuration

### Both VMs (172.16.205.129 and 172.16.205.130)

- **Cgroup path**: `/sys/fs/cgroup/system`
- **Assigned CPU**: CPU 0
- **Memory nodes**: 0
- **Effective CPUs**: 0 ✓

## Verification Results

### VM 172.16.205.129
```
Cpuset configuration:
  CPUs: 0
  Effective CPUs: 0
  Memory nodes: 0

Process affinity test:
  pid 34717's current affinity list: 0 ✓
```

### VM 172.16.205.130
```
Cpuset configuration:
  CPUs: 0
  Effective CPUs: 0
  Memory nodes: 0

Process affinity test:
  pid 13395's current affinity list: 0 ✓
```

## Usage

### Moving a Process to System Cpuset

```bash
# Get the PID of the process
PID=12345

# Move to system cpuset
echo $PID > /sys/fs/cgroup/system/cgroup.procs

# Verify affinity
taskset -cp $PID
```

### Using the Helper Script

A helper script is available at `/usr/local/bin/move_system_processes.sh` to automatically move common system processes:

```bash
sudo /usr/local/bin/move_system_processes.sh
```

This will move processes like:
- systemd
- sshd
- rsyslogd
- chronyd
- dbus
- NetworkManager

### Checking Current Processes in System Cpuset

```bash
# View PIDs in the system cgroup
cat /sys/fs/cgroup/system/cgroup.procs

# View process details
for pid in $(cat /sys/fs/cgroup/system/cgroup.procs); do
    ps -p $pid -o pid,comm,args
done
```

## CPU Allocation Summary

With the system cpuset in place, the complete CPU allocation is:

| Cgroup | CPUs | Purpose |
|--------|------|---------|
| `/sys/fs/cgroup/system` | 0 | System processes |
| `/sys/fs/cgroup/benchuser1` | 0 | User 1 workloads |
| `/sys/fs/cgroup/benchuser2` | 0 | User 2 workloads |
| `/sys/fs/cgroup/benchuser3` | 1 | User 3 workloads |
| `/sys/fs/cgroup/benchuser4` | 1 | User 4 workloads |

**Note**: System and users 1-2 share CPU 0, while users 3-4 have dedicated access to CPU 1.

## Persistence

The system cpuset is configured via systemd service (`cpuset-system.service`) which runs on boot. However, the cpuset controller must be enabled in the root cgroup for the configuration to work.

### Manual Reconfiguration After Reboot

If the system cpuset is not active after reboot:

```bash
# Enable cpuset controller
echo +cpuset > /sys/fs/cgroup/cgroup.subtree_control

# Recreate system cgroup
mkdir -p /sys/fs/cgroup/system
echo 0 > /sys/fs/cgroup/system/cpuset.cpus
echo 0 > /sys/fs/cgroup/system/cpuset.mems

# Verify
cat /sys/fs/cgroup/system/cpuset.cpus.effective
```

## Files

- **[setup_system_cpuset.sh](file:///Users/vinit/Documents/Unix/setup_system_cpuset.sh)** - Setup script
- **`/usr/local/bin/move_system_processes.sh`** - Helper script (on VMs)
- **`/etc/systemd/system/cpuset-system.service`** - Systemd service (on VMs)

## Related Documentation

- [USER_SETUP_README.md](file:///Users/vinit/Documents/Unix/USER_SETUP_README.md) - User cpuset documentation
- [cpuset_config.conf](file:///Users/vinit/Documents/Unix/cpuset_config.conf) - User cpuset configuration
