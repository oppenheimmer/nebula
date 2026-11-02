#!/usr/bin/env bash
set -euo pipefail
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
format=${1:-deb}
if (( $# > 1 )) || [[ "$format" != deb && "$format" != rpm ]]; then
    printf 'Usage: ./scripts/package-check.sh [deb|rpm]\n' >&2
    exit 2
fi
case "$format" in
    deb) build_dir="$project_dir/build-container-gcc"; image=nebula-dev:debian13; runtime=nebula-package-check:debian13 ;;
    rpm) build_dir="$project_dir/build-container-rpm"; image=nebula-dev:el9; runtime=nebula-package-check:el9 ;;
esac
[[ $(uname -m) == x86_64 ]] || { printf 'Package verification currently targets Linux x86_64.\n' >&2; exit 2; }
mkdir -p "$build_dir"
mount_spec() {
    local source_path=${1//\"/\"\"}
    local target_path=${2//\"/\"\"}
    printf 'type=bind,"source=%s","target=%s"' "$source_path" "$target_path"
    [[ ${3:-} != readonly ]] || printf ',readonly'
    return 0
}
# All builds and fixture operations stay inside project-scoped containers.
docker run --rm --init --network=none --cap-drop=ALL --security-opt=no-new-privileges \
    --user "$(id -u):$(id -g)" --read-only --tmpfs /tmp:rw,nosuid,nodev,size=256m,mode=1777 \
    --mount "$(mount_spec "$project_dir" /workspace readonly)" \
    --mount "$(mount_spec "$build_dir" /build)" --workdir /workspace \
    --env "NEBULA_PACKAGE_GENERATOR=${format^^}" "$image" sh -ec '
        cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
            -DNEBULA_WERROR=ON -DNEBULA_BUILD_EDITOR=ON -DBUILD_TESTING=ON \
            -DNEBULA_SANITIZE=OFF -DNEBULA_LIBFUZZER=OFF \
            -DNEBULA_PACKAGE_GENERATOR="$NEBULA_PACKAGE_GENERATOR"
        if [ "$NEBULA_PACKAGE_GENERATOR" = RPM ]; then
            cmake --build /build --parallel 4
            ctest --test-dir /build --output-on-failure
            rpm -qa --qf "%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\n" > /build/package-environment.txt
        else
            cmake --build /build --parallel 4 --target nebula_prototype nebula_workspace nebula_package_fixture desktop_tests preview_tests
        fi
        cpack --config /build/CPackConfig.cmake -B /build/packages'
context=$(mktemp -d "$build_dir/package-verify.XXXXXX")
trap 'rm -rf "$context"' EXIT
if [[ "$format" == deb ]]; then
    cp "$project_dir/packaging/Dockerfile.verify" "$context/Dockerfile"
    cp "$project_dir/packaging/debian.sources" "$context/debian.sources"
    cp "$project_dir/packaging/check-upgrade.sh" "$context/check-upgrade.sh"
else
    cp "$project_dir/packaging/Dockerfile.rpm-verify" "$context/Dockerfile"
    cp "$project_dir/packaging/check-rpm-upgrade.sh" "$context/check-upgrade.sh"
fi
cp "$build_dir/nebula_package_fixture" "$context/package-fixture"
IFS= read -r package_file < "$build_dir/package-filename.txt"
cp "$build_dir/packages/$package_file" "$context/nebula.$format"
docker build --tag "$runtime" "$context"
docker run --rm --init --network=none --cap-drop=ALL --security-opt=no-new-privileges \
    --user "$(id -u):$(id -g)" --read-only --tmpfs /tmp:rw,nosuid,nodev,size=256m,mode=1777 \
    --mount "$(mount_spec "$build_dir" /build readonly)" \
    --mount "$(mount_spec "$project_dir/tests/fixtures" /workspace/tests/fixtures readonly)" \
    --mount "$(mount_spec "$project_dir/packaging/check-installed.sh" /verify.sh readonly)" \
    "$runtime" sh /verify.sh "$format"
sha256sum "$build_dir/packages/$package_file"
