#!/usr/bin/env bash
set -euo pipefail
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/build-container-gcc"
image=nebula-dev:debian13
[[ $(uname -m) == x86_64 ]] || { printf 'Package verification currently targets Linux amd64.\n' >&2; exit 2; }
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
    --mount "$(mount_spec "$build_dir" /build)" --workdir /workspace "$image" sh -ec '
        cmake -S /workspace -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
            -DNEBULA_WERROR=ON -DNEBULA_BUILD_EDITOR=ON -DBUILD_TESTING=ON \
            -DNEBULA_SANITIZE=OFF -DNEBULA_LIBFUZZER=OFF
        cmake --build /build --parallel 4 --target nebula_prototype nebula_workspace nebula_package_fixture desktop_tests preview_tests
        cpack --config /build/CPackConfig.cmake -B /build/packages'
context=$(mktemp -d "$build_dir/package-verify.XXXXXX")
trap 'rm -rf "$context"' EXIT
cp "$project_dir/packaging/Dockerfile.verify" "$context/Dockerfile"
cp "$project_dir/packaging/debian.sources" "$context/debian.sources"
cp "$project_dir/packaging/check-upgrade.sh" "$context/check-upgrade.sh"
cp "$build_dir/nebula_package_fixture" "$context/package-fixture"
cp "$build_dir/packages/nebula-0.1.0~dev1-x86_64.deb" "$context/nebula.deb"
docker build --tag nebula-package-check:debian13 "$context"
docker run --rm --init --network=none --cap-drop=ALL --security-opt=no-new-privileges \
    --user "$(id -u):$(id -g)" --read-only --tmpfs /tmp:rw,nosuid,nodev,size=256m,mode=1777 \
    --mount "$(mount_spec "$build_dir" /build readonly)" \
    --mount "$(mount_spec "$project_dir/tests/fixtures" /workspace/tests/fixtures readonly)" \
    --mount "$(mount_spec "$project_dir/packaging/check-installed.sh" /verify.sh readonly)" \
    nebula-package-check:debian13 sh /verify.sh
