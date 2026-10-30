#!/bin/sh
set -eu
root=$(mktemp -d /tmp/nebula-upgrade.XXXXXX)
trap 'rm -rf "$root"' EXIT HUP INT TERM
chown nobody:nogroup "$root"
su -s /bin/sh nobody -c "/opt/package-fixture create '$root/workspace'"
dpkg -i /tmp/nebula.deb
su -s /bin/sh nobody -c "/opt/package-fixture verify '$root/workspace'"
dpkg --purge nebula
test -d "$root/workspace/attachments"
su -s /bin/sh nobody -c "/opt/package-fixture verify '$root/workspace'"
dpkg -i /tmp/nebula.deb
su -s /bin/sh nobody -c "/opt/package-fixture verify '$root/workspace'"
printf '%s\n' 'PASS: reinstall, removal and reinstall preserve a user-owned workspace'
