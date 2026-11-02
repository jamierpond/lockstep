#!/bin/sh
# Writes the Homebrew formula and the Scoop manifest for a release from the
# templates in packaging/ and pushes them to the tap and bucket repositories.
# Usage: scripts/update-packaging.sh <version> <SHA256SUMS file>
# PACKAGING_TOKEN, when set, is the token the pushes use; otherwise git's own
# credentials do.
set -eu
version="$1"
sums="$2"
tap="${TAP_REPOSITORY:-jamierpond/homebrew-tap}"
bucket="${BUCKET_REPOSITORY:-jamierpond/scoop-bucket}"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"

hash_of() {
    grep -E " lockstep-$version-$1(\.exe)?$" "$sums" | cut -d' ' -f1
}

render() {
    sed -e "s/@VERSION@/$version/g" \
        -e "s/@SHA256_MACOS_ARM64@/$(hash_of macos-arm64)/g" \
        -e "s/@SHA256_LINUX_X64@/$(hash_of linux-x64)/g" \
        -e "s/@SHA256_LINUX_ARM64@/$(hash_of linux-arm64)/g" \
        -e "s/@SHA256_WINDOWS_X64@/$(hash_of windows-x64)/g" \
        -e "s/@SHA256_WINDOWS_ARM64@/$(hash_of windows-arm64)/g" \
        "$1"
}

repository_url() {
    if [ -n "${PACKAGING_TOKEN:-}" ]; then
        echo "https://x-access-token:$PACKAGING_TOKEN@github.com/$1.git"
    else
        echo "https://github.com/$1.git"
    fi
}

publish() {
    repository="$1"
    template="$2"
    destination="$3"
    checkout="$work/$(basename "$repository")"

    git clone --quiet "$(repository_url "$repository")" "$checkout"
    mkdir -p "$(dirname "$checkout/$destination")"
    render "$root/packaging/$template" > "$checkout/$destination"
    git -C "$checkout" add "$destination"

    if git -C "$checkout" diff --cached --quiet; then
        echo "update-packaging: $repository already has lockstep $version"
        return
    fi

    git -C "$checkout" -c user.name="${GIT_AUTHOR_NAME:-github-actions}" \
        -c user.email="${GIT_AUTHOR_EMAIL:-github-actions@users.noreply.github.com}" \
        commit --quiet -m "lockstep $version"
    git -C "$checkout" push --quiet origin HEAD
    echo "update-packaging: pushed lockstep $version to $repository"
}

publish "$tap" lockstep.rb.in Formula/lockstep.rb
publish "$bucket" lockstep.json.in bucket/lockstep.json
rm -rf "$work"
