#!/bin/bash -e
# Install Linux kernel headers for on-device module compilation
on_chroot << CHEOF
apt-get install -y linux-headers-\$(ls /lib/modules/ | head -1) || true
CHEOF
