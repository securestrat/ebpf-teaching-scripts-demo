#!/bin/bash
#
# Script to create a system cpuset restricted to CPU 0
# This isolates system processes to CPU 0
# Usage: sudo ./setup_system_cpuset.sh
#

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo -e "${RED}Error: This script must be run as root${NC}"
   exit 1
fi

echo -e "${GREEN}=== Setting up System Cpuset ===${NC}\n"

# Detect cgroup version
if [[ -d /sys/fs/cgroup/cpuset ]]; then
    CGROUP_VERSION="v1"
    CGROUP_ROOT="/sys/fs/cgroup/cpuset"
    echo -e "${BLUE}Detected cgroup v1${NC}"
elif [[ -f /sys/fs/cgroup/cgroup.controllers ]]; then
    CGROUP_VERSION="v2"
    CGROUP_ROOT="/sys/fs/cgroup"
    echo -e "${BLUE}Detected cgroup v2${NC}"
else
    echo -e "${RED}Error: Unable to detect cgroup version${NC}"
    exit 1
fi

SYSTEM_CGROUP="$CGROUP_ROOT/system"
CPU_ASSIGNMENT="0"
MEM_ASSIGNMENT="0"

echo -e "${YELLOW}Creating system cpuset (CPU: $CPU_ASSIGNMENT, Memory nodes: $MEM_ASSIGNMENT)...${NC}\n"

# Create system cgroup directory
mkdir -p "$SYSTEM_CGROUP"
echo -e "${GREEN}✓ Created cgroup directory: $SYSTEM_CGROUP${NC}"

# For cgroup v2, ensure cpuset controller is enabled
if [[ "$CGROUP_VERSION" == "v2" ]]; then
    if [[ -f "$CGROUP_ROOT/cgroup.subtree_control" ]]; then
        if ! grep -q cpuset "$CGROUP_ROOT/cgroup.subtree_control"; then
            echo "+cpuset" > "$CGROUP_ROOT/cgroup.subtree_control" 2>/dev/null || true
            echo -e "${GREEN}✓ Enabled cpuset controller${NC}"
        else
            echo -e "${YELLOW}Cpuset controller already enabled${NC}"
        fi
    fi
fi

# Configure cpuset
echo "$CPU_ASSIGNMENT" > "$SYSTEM_CGROUP/cpuset.cpus"
echo "$MEM_ASSIGNMENT" > "$SYSTEM_CGROUP/cpuset.mems"
echo -e "${GREEN}✓ Configured cpuset: CPUs=$CPU_ASSIGNMENT, Memory nodes=$MEM_ASSIGNMENT${NC}"

# Verify configuration
if [[ "$CGROUP_VERSION" == "v2" ]]; then
    EFFECTIVE_CPUS=$(cat "$SYSTEM_CGROUP/cpuset.cpus.effective" 2>/dev/null || echo "N/A")
    echo -e "${GREEN}✓ Effective CPUs: $EFFECTIVE_CPUS${NC}"
fi

# Create systemd service to persist the configuration
SERVICE_FILE="/etc/systemd/system/cpuset-system.service"

cat > "$SERVICE_FILE" << 'EOF'
[Unit]
Description=System cpuset configuration (CPU 0)
After=multi-user.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/bash -c 'echo 0 > /sys/fs/cgroup/system/cpuset.cpus && echo 0 > /sys/fs/cgroup/system/cpuset.mems'

[Install]
WantedBy=multi-user.target
EOF

echo -e "${GREEN}✓ Created systemd service: $SERVICE_FILE${NC}"

# Enable the service
systemctl daemon-reload
systemctl enable cpuset-system.service 2>/dev/null || true
systemctl start cpuset-system.service 2>/dev/null || true
echo -e "${GREEN}✓ Systemd service enabled and started${NC}"

# Create helper script to move system processes
HELPER_SCRIPT="/usr/local/bin/move_system_processes.sh"

cat > "$HELPER_SCRIPT" << 'EOF'
#!/bin/bash
#
# Move system processes to the system cpuset
# Usage: sudo /usr/local/bin/move_system_processes.sh
#

# Detect cgroup version
if [[ -d /sys/fs/cgroup/cpuset ]]; then
    CGROUP_PATH="/sys/fs/cgroup/cpuset/system"
    TASKS_FILE="$CGROUP_PATH/tasks"
elif [[ -f /sys/fs/cgroup/cgroup.controllers ]]; then
    CGROUP_PATH="/sys/fs/cgroup/system"
    TASKS_FILE="$CGROUP_PATH/cgroup.procs"
else
    echo "Error: Unable to detect cgroup version"
    exit 1
fi

# Check if cgroup exists
if [[ ! -d "$CGROUP_PATH" ]]; then
    echo "Error: System cpuset cgroup does not exist"
    exit 1
fi

# List of system processes to move (by name pattern)
SYSTEM_PROCESSES=(
    "systemd"
    "sshd"
    "rsyslogd"
    "chronyd"
    "dbus"
    "NetworkManager"
)

echo "Moving system processes to system cpuset (CPU 0)..."

MOVED_COUNT=0
for proc_name in "${SYSTEM_PROCESSES[@]}"; do
    for pid in $(pgrep "$proc_name" 2>/dev/null); do
        if echo $pid > "$TASKS_FILE" 2>/dev/null; then
            echo "  ✓ Moved $proc_name (PID: $pid)"
            ((MOVED_COUNT++))
        fi
    done
done

echo "Moved $MOVED_COUNT processes to system cpuset"
EOF

chmod +x "$HELPER_SCRIPT"
echo -e "${GREEN}✓ Created helper script: $HELPER_SCRIPT${NC}"

echo -e "\n${GREEN}=== System Cpuset Setup Complete ===${NC}\n"

# Display summary
echo -e "${GREEN}Summary:${NC}"
echo -e "  Cgroup path: $SYSTEM_CGROUP"
echo -e "  CPUs: $CPU_ASSIGNMENT"
echo -e "  Memory nodes: $MEM_ASSIGNMENT"

if [[ "$CGROUP_VERSION" == "v2" ]]; then
    echo -e "  Effective CPUs: $(cat $SYSTEM_CGROUP/cpuset.cpus.effective 2>/dev/null || echo 'N/A')"
fi

echo -e "\n${YELLOW}To move system processes to this cpuset, run:${NC}"
echo -e "  ${BLUE}sudo $HELPER_SCRIPT${NC}"

echo -e "\n${YELLOW}To manually move a process:${NC}"
if [[ "$CGROUP_VERSION" == "v1" ]]; then
    echo -e "  ${BLUE}echo <PID> > /sys/fs/cgroup/cpuset/system/tasks${NC}"
else
    echo -e "  ${BLUE}echo <PID> > /sys/fs/cgroup/system/cgroup.procs${NC}"
fi
