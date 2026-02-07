# User and Cpuset Setup Guide

This guide explains how to manage the benchmark users and their cpuset configurations on the VMs.

## Overview

Four benchmark users (`benchuser1` through `benchuser4`) have been created with dedicated CPU assignments using Linux cpusets. This ensures CPU isolation for each user's processes.

### CPU Allocation

With 2 CPUs available on the VM, the allocation is:
- **benchuser1**: CPU 0 (shared with benchuser2)
- **benchuser2**: CPU 0 (shared with benchuser1)
- **benchuser3**: CPU 1 (shared with benchuser4)
- **benchuser4**: CPU 1 (shared with benchuser3)

## Files

- **`cpuset_config.conf`**: Configuration file defining CPU assignments
- **`create_users.sh`**: Script to create the 4 users
- **`setup_cpusets.sh`**: Script to configure cpuset cgroups
- **`/usr/local/bin/apply_cpuset.sh`**: Helper script to move processes to cpusets (created by setup script)

## Initial Setup

### 1. Create Users

```bash
# Copy scripts to VM
scp cpuset_config.conf create_users.sh setup_cpusets.sh 172.16.205.129:/tmp/

# SSH to VM
ssh 172.16.205.129

# Run user creation script
cd /tmp
sudo bash create_users.sh
```

This will:
- Create 4 users with home directories
- Generate random passwords (saved to a temporary file)
- Configure SSH directories
- Force password change on first login

**Important**: Save the generated passwords before deleting the temporary password file!

### 2. Configure Cpusets

```bash
# Run cpuset setup script (still on VM)
sudo bash setup_cpusets.sh
```

This will:
- Detect cgroup version (v1 or v2)
- Create cpuset cgroups for each user
- Assign CPUs and memory nodes
- Create systemd services for each user
- Configure PAM for automatic cpuset application on login
- Create helper script at `/usr/local/bin/apply_cpuset.sh`

## Verification

### Check User Creation

```bash
# Verify users exist
id benchuser1
id benchuser2
id benchuser3
id benchuser4

# List all benchmark users
getent passwd | grep benchuser
```

### Check Cpuset Configuration

For **cgroup v1**:
```bash
# View cpuset assignments
cat /sys/fs/cgroup/cpuset/benchuser1/cpuset.cpus
cat /sys/fs/cgroup/cpuset/benchuser2/cpuset.cpus
cat /sys/fs/cgroup/cpuset/benchuser3/cpuset.cpus
cat /sys/fs/cgroup/cpuset/benchuser4/cpuset.cpus
```

For **cgroup v2**:
```bash
# View cpuset assignments
cat /sys/fs/cgroup/benchuser1/cpuset.cpus
cat /sys/fs/cgroup/benchuser2/cpuset.cpus
cat /sys/fs/cgroup/benchuser3/cpuset.cpus
cat /sys/fs/cgroup/benchuser4/cpuset.cpus
```

### Test CPU Affinity

```bash
# Login as a user
su - benchuser1

# Check allowed CPUs for current shell
cat /proc/self/status | grep Cpus_allowed_list

# Run a CPU-bound process and check affinity
stress-ng --cpu 1 --timeout 30s &
PID=$!
taskset -cp $PID

# Monitor with htop (in another terminal)
htop
```

### Check Systemd Services

```bash
# Check service status
systemctl status cpuset-benchuser1.service
systemctl status cpuset-benchuser2.service
systemctl status cpuset-benchuser3.service
systemctl status cpuset-benchuser4.service

# View all cpuset services
systemctl list-units | grep cpuset
```

## Management Tasks

### Manually Apply Cpuset to Running Processes

```bash
# Apply cpuset to all processes of a user
sudo /usr/local/bin/apply_cpuset.sh benchuser1
```

### Modify CPU Allocation

1. Edit `cpuset_config.conf`:
   ```bash
   sudo nano cpuset_config.conf
   ```

2. Update the CPU assignments (format: `username:cpu_list:memory_nodes`)

3. Re-run the setup script:
   ```bash
   sudo bash setup_cpusets.sh
   ```

4. Apply to existing processes:
   ```bash
   sudo /usr/local/bin/apply_cpuset.sh benchuser1
   sudo /usr/local/bin/apply_cpuset.sh benchuser2
   sudo /usr/local/bin/apply_cpuset.sh benchuser3
   sudo /usr/local/bin/apply_cpuset.sh benchuser4
   ```

### Add a New User

1. Add entry to `cpuset_config.conf`:
   ```
   benchuser5:0:0
   ```

2. Create the user:
   ```bash
   sudo useradd -m -s /bin/bash benchuser5
   sudo passwd benchuser5
   ```

3. Run cpuset setup:
   ```bash
   sudo bash setup_cpusets.sh
   ```

### Remove a User

1. Stop user processes:
   ```bash
   sudo pkill -u benchuser4
   ```

2. Remove cpuset cgroup:
   ```bash
   # For cgroup v1
   sudo rmdir /sys/fs/cgroup/cpuset/benchuser4
   
   # For cgroup v2
   sudo rmdir /sys/fs/cgroup/benchuser4
   ```

3. Disable and remove systemd service:
   ```bash
   sudo systemctl disable cpuset-benchuser4.service
   sudo rm /etc/systemd/system/cpuset-benchuser4.service
   sudo systemctl daemon-reload
   ```

4. Delete user:
   ```bash
   sudo userdel -r benchuser4
   ```

5. Remove from `cpuset_config.conf`

## Troubleshooting

### Cpuset Not Applied After Login

**Symptom**: User processes run on all CPUs instead of assigned ones.

**Solutions**:
1. Check if PAM is configured:
   ```bash
   grep apply_cpuset /etc/pam.d/common-session
   ```

2. Manually apply cpuset:
   ```bash
   sudo /usr/local/bin/apply_cpuset.sh <username>
   ```

3. Restart systemd service:
   ```bash
   sudo systemctl restart cpuset-<username>.service
   ```

### Permission Denied When Writing to Cgroup

**Symptom**: `setup_cpusets.sh` fails with permission errors.

**Solution**: Ensure you're running as root:
```bash
sudo bash setup_cpusets.sh
```

### Cgroup Controllers Not Available

**Symptom**: Cannot enable cpuset controller in cgroup v2.

**Solution**: Check kernel boot parameters and enable cpuset:
```bash
# Check current controllers
cat /sys/fs/cgroup/cgroup.controllers

# Enable cpuset in root cgroup
echo "+cpuset" | sudo tee /sys/fs/cgroup/cgroup.subtree_control
```

### User Cannot Login

**Symptom**: User account locked or password issues.

**Solution**:
```bash
# Unlock account
sudo passwd -u <username>

# Reset password
sudo passwd <username>

# Remove password expiry
sudo chage -d -1 <username>
```

## Advanced Usage

### CPU Exclusive Mode

To prevent other processes from using a user's CPUs (requires cgroup v1):

```bash
# Enable cpu_exclusive
echo 1 | sudo tee /sys/fs/cgroup/cpuset/benchuser1/cpuset.cpu_exclusive
```

### Memory Pressure Monitoring

Monitor memory pressure for a user's cgroup:

```bash
# For cgroup v2
cat /sys/fs/cgroup/benchuser1/memory.pressure
```

### Real-time Process Priority

Combine cpusets with real-time scheduling:

```bash
# Run process with real-time priority on assigned CPUs
sudo -u benchuser1 chrt -f 50 ./my_benchmark
```

## References

- [Linux Cpusets Documentation](https://www.kernel.org/doc/Documentation/cgroup-v1/cpusets.txt)
- [Cgroup v2 Documentation](https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html)
- [PAM Configuration Guide](http://www.linux-pam.org/Linux-PAM-html/)
