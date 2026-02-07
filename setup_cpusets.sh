#!/bin/bash
#
# Script to configure cpuset cgroups for benchmark users
# Usage: sudo ./setup_cpusets.sh
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

# Configuration file
CONFIG_FILE="./cpuset_config.conf"

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo -e "${RED}Error: Configuration file $CONFIG_FILE not found${NC}"
    exit 1
fi

echo -e "${GREEN}=== Setting up Cpuset Cgroups ===${NC}\n"

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

# Function to setup cpuset for cgroup v1
setup_cpuset_v1() {
    local username=$1
    local cpus=$2
    local mems=$3
    
    local user_cgroup="$CGROUP_ROOT/$username"
    
    echo -e "  Creating cpuset for ${GREEN}$username${NC} (CPUs: $cpus, Memory nodes: $mems)"
    
    # Create cgroup directory
    mkdir -p "$user_cgroup"
    
    # Configure cpuset
    echo "$cpus" > "$user_cgroup/cpuset.cpus"
    echo "$mems" > "$user_cgroup/cpuset.mems"
    
    # Enable cpu_exclusive if desired (optional)
    # echo 1 > "$user_cgroup/cpuset.cpu_exclusive"
    
    echo -e "  ${GREEN}✓ Cpuset configured${NC}"
}

# Function to setup cpuset for cgroup v2
setup_cpuset_v2() {
    local username=$1
    local cpus=$2
    local mems=$3
    
    local user_cgroup="$CGROUP_ROOT/$username"
    
    echo -e "  Creating cpuset for ${GREEN}$username${NC} (CPUs: $cpus, Memory nodes: $mems)"
    
    # Create cgroup directory
    mkdir -p "$user_cgroup"
    
    # Enable cpuset controller in parent if needed
    if [[ -f "$CGROUP_ROOT/cgroup.subtree_control" ]]; then
        if ! grep -q cpuset "$CGROUP_ROOT/cgroup.subtree_control"; then
            echo "+cpuset" > "$CGROUP_ROOT/cgroup.subtree_control" 2>/dev/null || true
        fi
    fi
    
    # Configure cpuset
    echo "$cpus" > "$user_cgroup/cpuset.cpus"
    echo "$mems" > "$user_cgroup/cpuset.mems"
    
    echo -e "  ${GREEN}✓ Cpuset configured${NC}"
}

# Function to create systemd service for user
create_systemd_service() {
    local username=$1
    
    local service_file="/etc/systemd/system/cpuset-$username.service"
    
    cat > "$service_file" << EOF
[Unit]
Description=Apply cpuset restrictions for $username
After=multi-user.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/bin/apply_cpuset.sh $username

[Install]
WantedBy=multi-user.target
EOF

    # Enable the service
    systemctl daemon-reload
    systemctl enable "cpuset-$username.service" 2>/dev/null || true
    
    echo -e "  ${GREEN}✓ Systemd service created and enabled${NC}"
}

# Create helper script to apply cpuset to user processes
create_apply_script() {
    local script_file="/usr/local/bin/apply_cpuset.sh"
    
    cat > "$script_file" << 'EOF'
#!/bin/bash
# Helper script to move user processes to their cpuset cgroup

USERNAME=$1

if [[ -z "$USERNAME" ]]; then
    echo "Usage: $0 <username>"
    exit 1
fi

# Detect cgroup version and set appropriate path
if [[ -d /sys/fs/cgroup/cpuset ]]; then
    CGROUP_PATH="/sys/fs/cgroup/cpuset/$USERNAME"
    TASKS_FILE="$CGROUP_PATH/tasks"
elif [[ -f /sys/fs/cgroup/cgroup.controllers ]]; then
    CGROUP_PATH="/sys/fs/cgroup/$USERNAME"
    TASKS_FILE="$CGROUP_PATH/cgroup.procs"
else
    echo "Error: Unable to detect cgroup version"
    exit 1
fi

# Check if cgroup exists
if [[ ! -d "$CGROUP_PATH" ]]; then
    echo "Error: Cpuset cgroup for $USERNAME does not exist"
    exit 1
fi

# Move all user processes to the cpuset
for pid in $(pgrep -u "$USERNAME"); do
    echo $pid > "$TASKS_FILE" 2>/dev/null || true
done

exit 0
EOF

    chmod +x "$script_file"
    echo -e "${GREEN}✓ Created helper script: $script_file${NC}"
}

# Create PAM configuration to apply cpuset on login
setup_pam_exec() {
    local pam_file="/etc/pam.d/common-session"
    local pam_line="session optional pam_exec.so /usr/local/bin/apply_cpuset.sh"
    
    # Check if PAM file exists (Debian/Ubuntu)
    if [[ -f "$pam_file" ]]; then
        if ! grep -q "apply_cpuset.sh" "$pam_file"; then
            echo "$pam_line" >> "$pam_file"
            echo -e "${GREEN}✓ PAM configuration updated${NC}"
        else
            echo -e "${YELLOW}PAM already configured${NC}"
        fi
    else
        echo -e "${YELLOW}Warning: $pam_file not found, skipping PAM configuration${NC}"
        echo -e "${YELLOW}You may need to manually configure PAM for your distribution${NC}"
    fi
}

# Main setup loop
echo -e "${YELLOW}Reading configuration from $CONFIG_FILE...${NC}\n"

create_apply_script

while IFS=: read -r username cpus mems; do
    # Skip comments and empty lines
    [[ "$username" =~ ^#.*$ ]] && continue
    [[ -z "$username" ]] && continue
    
    # Trim whitespace
    username=$(echo "$username" | xargs)
    cpus=$(echo "$cpus" | xargs)
    mems=$(echo "$mems" | xargs)
    
    echo -e "${BLUE}Configuring cpuset for user: $username${NC}"
    
    # Check if user exists
    if ! id "$username" &>/dev/null; then
        echo -e "  ${RED}Warning: User $username does not exist, skipping...${NC}\n"
        continue
    fi
    
    # Setup cpuset based on cgroup version
    if [[ "$CGROUP_VERSION" == "v1" ]]; then
        setup_cpuset_v1 "$username" "$cpus" "$mems"
    else
        setup_cpuset_v2 "$username" "$cpus" "$mems"
    fi
    
    # Create systemd service
    create_systemd_service "$username"
    
    # Apply cpuset to existing processes
    /usr/local/bin/apply_cpuset.sh "$username" 2>/dev/null || true
    
    echo ""
done < "$CONFIG_FILE"

# Setup PAM for automatic cpuset application on login
echo -e "${YELLOW}Configuring PAM for automatic cpuset application...${NC}"
setup_pam_exec

echo -e "\n${GREEN}=== Cpuset Setup Complete ===${NC}\n"

# Display summary
echo -e "${GREEN}Summary:${NC}"
if [[ "$CGROUP_VERSION" == "v1" ]]; then
    for dir in "$CGROUP_ROOT"/benchuser*; do
        if [[ -d "$dir" ]]; then
            username=$(basename "$dir")
            cpus=$(cat "$dir/cpuset.cpus")
            mems=$(cat "$dir/cpuset.mems")
            echo -e "  ✓ $username: CPUs=$cpus, Memory nodes=$mems"
        fi
    done
else
    for dir in "$CGROUP_ROOT"/benchuser*; do
        if [[ -d "$dir" ]]; then
            username=$(basename "$dir")
            cpus=$(cat "$dir/cpuset.cpus" 2>/dev/null || echo "N/A")
            mems=$(cat "$dir/cpuset.mems" 2>/dev/null || echo "N/A")
            echo -e "  ✓ $username: CPUs=$cpus, Memory nodes=$mems"
        fi
    done
fi

echo -e "\n${YELLOW}Note: Users' processes will be restricted to their assigned CPUs on next login${NC}"
echo -e "${YELLOW}To apply immediately to existing sessions, run: /usr/local/bin/apply_cpuset.sh <username>${NC}"
