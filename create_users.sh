#!/bin/bash
#
# Script to create 4 benchmark users on the VM
# Usage: sudo ./create_users.sh
#

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo -e "${RED}Error: This script must be run as root${NC}"
   exit 1
fi

echo -e "${GREEN}=== Creating Benchmark Users ===${NC}\n"

# Array of usernames
USERS=("benchuser1" "benchuser2" "benchuser3" "benchuser4")

# Password file (will be created temporarily)
PASSWORD_FILE="/tmp/user_passwords_$(date +%s).txt"
touch "$PASSWORD_FILE"
chmod 600 "$PASSWORD_FILE"

echo -e "${YELLOW}Creating users and setting passwords...${NC}\n"

for username in "${USERS[@]}"; do
    echo -e "Processing user: ${GREEN}${username}${NC}"
    
    # Check if user already exists
    if id "$username" &>/dev/null; then
        echo -e "  ${YELLOW}Warning: User $username already exists, skipping...${NC}"
        continue
    fi
    
    # Create user with home directory
    useradd -m -s /bin/bash "$username"
    
    # Generate random password
    password=$(openssl rand -base64 12)
    
    # Set password
    echo "$username:$password" | chpasswd
    
    # Force password change on first login
    chage -d 0 "$username"
    
    # Save password to file
    echo "$username:$password" >> "$PASSWORD_FILE"
    
    # Create .ssh directory for future key-based auth
    mkdir -p "/home/$username/.ssh"
    chmod 700 "/home/$username/.ssh"
    chown "$username:$username" "/home/$username/.ssh"
    
    echo -e "  ${GREEN}✓ User created successfully${NC}"
done

echo -e "\n${GREEN}=== User Creation Complete ===${NC}\n"
echo -e "${YELLOW}Passwords saved to: $PASSWORD_FILE${NC}"
echo -e "${YELLOW}Users will be required to change password on first login${NC}\n"

# Display created users
echo -e "${GREEN}Created users:${NC}"
for username in "${USERS[@]}"; do
    if id "$username" &>/dev/null; then
        echo -e "  ✓ $username (UID: $(id -u $username))"
    fi
done

echo -e "\n${YELLOW}Password file contents:${NC}"
cat "$PASSWORD_FILE"

echo -e "\n${RED}IMPORTANT: Save these passwords and delete $PASSWORD_FILE when done!${NC}"
