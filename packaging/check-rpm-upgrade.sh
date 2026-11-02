#!/bin/sh
set -eu
root=$(mktemp -d /tmp/nebula-upgrade.XXXXXX)
trap 'rm -rf "$root"' EXIT HUP INT TERM
chown nobody "$root"
su -s /bin/sh nobody -c "/opt/package-fixture create '$root/workspace'"
rpm -Uvh --replacepkgs /tmp/nebula.rpm
su -s /bin/sh nobody -c "/opt/package-fixture verify '$root/workspace'"
rpm -e nebula
test -d "$root/workspace/attachments"
su -s /bin/sh nobody -c "/opt/package-fixture verify '$root/workspace'"
rpm -Uvh /tmp/nebula.rpm
su -s /bin/sh nobody -c "/opt/package-fixture verify '$root/workspace'"
printf '%s\n' 'PASS: RPM reinstall, removal and reinstall preserve a user-owned workspace'
