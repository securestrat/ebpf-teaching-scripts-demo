#!/bin/bash
#
# Comprehensive verification script for cpuset configuration
# Tests CPU isolation by running stress tests for each user
#

VM=$1

if [[ -z "$VM" ]]; then
    echo "Usage: $0 <VM_IP>"
    exit 1
fi

echo "=========================================="
echo "Cpuset Verification for VM: $VM"
echo "=========================================="
echo

# Test 1: Verify users exist
echo "=== Test 1: User Verification ==="
for user in benchuser1 benchuser2 benchuser3 benchuser4; do
    ssh root@$VM "id $user" 2>/dev/null && echo "✓ $user exists" || echo "✗ $user missing"
done
echo

# Test 2: Verify cpuset configuration
echo "=== Test 2: Cpuset Configuration ==="
ssh root@$VM "for user in benchuser1 benchuser2 benchuser3 benchuser4; do echo -n \"\$user: CPUs=\" && cat /sys/fs/cgroup/\$user/cpuset.cpus && echo -n \"         Effective=\" && cat /sys/fs/cgroup/\$user/cpuset.cpus.effective; done"
echo

# Test 3: CPU Affinity Test with stress-ng
echo "=== Test 3: CPU Isolation Stress Test ==="
echo "Running 10-second CPU stress test for each user..."
echo

for user in benchuser1 benchuser2 benchuser3 benchuser4; do
    echo "Testing $user:"
    
    # Start stress-ng as the user and move to cgroup
    ssh root@$VM "sudo -u $user stress-ng --cpu 1 --timeout 10s &>/dev/null & \
                  sleep 1; \
                  pid=\$(pgrep -u $user stress-ng | head -1); \
                  if [[ -n \"\$pid\" ]]; then \
                      echo \$pid > /sys/fs/cgroup/$user/cgroup.procs; \
                      echo \"  PID: \$pid\"; \
                      taskset -cp \$pid; \
                      cat /proc/\$pid/status | grep 'Cpus_allowed_list'; \
                  else \
                      echo '  Error: Could not start stress-ng'; \
                  fi; \
                  pkill -u $user stress-ng 2>/dev/null"
    echo
done

# Test 4: Concurrent stress test
echo "=== Test 4: Concurrent CPU Stress Test ==="
echo "Running all users simultaneously for 5 seconds..."
echo

ssh root@$VM "
    # Start stress for all users
    for user in benchuser1 benchuser2 benchuser3 benchuser4; do
        sudo -u \$user stress-ng --cpu 1 --timeout 5s &>/dev/null &
        sleep 0.5
        pid=\$(pgrep -u \$user stress-ng | head -1)
        if [[ -n \"\$pid\" ]]; then
            echo \$pid > /sys/fs/cgroup/\$user/cgroup.procs
            echo \"\$user (PID \$pid): \$(taskset -cp \$pid 2>&1 | grep 'affinity list')\"
        fi
    done
    
    # Wait for completion
    sleep 6
    
    # Cleanup
    for user in benchuser1 benchuser2 benchuser3 benchuser4; do
        pkill -u \$user stress-ng 2>/dev/null
    done
"

echo
echo "=========================================="
echo "Verification Complete!"
echo "=========================================="
