#!/usr/bin/env bash

_my_echo() {
    prefix="$1"
    shift
    echo -e "\e[0;38;5;209m[$prefix]\e[0m" "$@"
}

# Exit on error
set -e

# Backup the original sources.list
_my_echo "INFO" "Backing up /etc/apt/sources.list to /etc/apt/sources.list.backup"
sudo cp /etc/apt/sources.list /etc/apt/sources.list.backup

# Remove the newest, non-compatible version of QEMU
_my_echo "INFO" "Removing incompatible QEMU version..."
sudo apt-get remove -y qemu-system-misc

# Append custom package sources to /etc/apt/sources.list
_my_echo "INFO" "Adding custom package sources..."
sudo tee -a /etc/apt/sources.list > /dev/null <<EOF
deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ focal main restricted universe multiverse
deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ focal-updates main restricted universe multiverse
deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ focal-backports main restricted universe multiverse
EOF
# see <https://stackoverflow.com/questions/66718225/qemu-system-riscv64-is-not-found-in-package-qemu-system-misc>

# Update package index
_my_echo "INFO" "Updating package index..."
sudo apt-get update

# Install the old, stable version of QEMU
_my_echo "INFO" "Installing stable QEMU version 1:4.2-3ubuntu6..."
sudo apt-get install -y qemu-system-misc=1:4.2-3ubuntu6

# Restore the original sources.list
_my_echo "INFO" "Restoring original package sources..."
sudo mv /etc/apt/sources.list.backup /etc/apt/sources.list

# Update package index
_my_echo "INFO" "Updating package index..."
sudo apt-get update

_my_echo "INFO" "QEMU installation has been fixed and sources restored. ✔️"

unset -f _my_echo
