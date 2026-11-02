#!/bin/sh
set -eu
test -x /usr/bin/nebula
test -x /usr/bin/nebula_workspace
test -f /usr/share/applications/nebula.desktop
test -f /usr/share/doc/nebula/copyright
for name in README.md LICENSE AGENTS.md ROADMAP.md docs/INSTALL.md docs/RELEASE_CHECKLIST.md; do
    test -f "/usr/share/doc/nebula/$name"
done
for name in README.md 01-block-editor.png 02-journal-views.png 03-workspace-search.png; do
    test -f "/usr/share/doc/nebula/assets/screenshots/0.1-alpha/$name"
done
if command -v c++ >/dev/null 2>&1 || command -v cmake >/dev/null 2>&1; then
    printf '%s\n' 'Package verification must use a runtime environment without a build toolchain.' >&2
    exit 1
fi
case "${1:-deb}" in
    deb)
        cmp /usr/bin/nebula /build/nebula_prototype
        cmp /usr/bin/nebula_workspace /build/nebula_workspace
        version=$(dpkg-query -W -f='${Version}' nebula)
        ;;
    rpm)
        # rpmbuild may strip ELF symbols; verify the installed package payload.
        rpm -V nebula
        version=$(rpm -q --qf '%{VERSION}' nebula)
        ;;
    *) printf '%s\n' 'Expected deb or rpm package verification.' >&2; exit 2 ;;
esac
test "$(nebula --version)" = "Nebula $version"
test "$(nebula_workspace --version)" = "nebula_workspace $version"
root=$(mktemp -d /tmp/nebula-package.XXXXXX)
trap 'rm -rf "$root"' EXIT HUP INT TERM
/build/nebula_package_fixture create "$root/workspace"
nebula --smoke-test --workspace "$root/new-workspace"
nebula --smoke-test --workspace "$root/workspace"
nebula_workspace backup "$root/workspace" "$root/backup"
nebula_workspace restore "$root/backup" "$root/restored"
/build/nebula_package_fixture verify "$root/restored"
nebula_workspace export-json "$root/workspace" "$root/json"
nebula_workspace import-json "$root/json" "$root/imported"
/build/nebula_package_fixture verify "$root/imported"
nebula_workspace export-md "$root/workspace" "$root/markdown"
nebula --smoke-test --workspace "$root/restored"
/build/desktop_tests
/build/preview_tests
printf '%s\n' 'PASS: installed runtime, window editing, search, managed files, export, backup and restore offline'
