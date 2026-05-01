#!/bin/sh

INSTR="$1"

VERSION=0.7.16

case "$INSTR" in
    get-version)
        echo "${VERSION}"
        ;;
    get-vcs)
        VCS_VERSION="$(git -C "$MESON_SOURCE_ROOT" describe --always --dirty)"
        if [ -n "$VCS_VERSION" ]; then
            echo "${VERSION} (vcs=${VCS_VERSION})"
        else
            echo "${VERSION}"
        fi
        ;;
    get-gh-release)
        TAG="v${VERSION}"
        echo "https://github.com/bazaar-org/bazaar/releases/tag/${TAG}"
        ;;
    *)
        echo invalid arguments 1>&2
        exit 1
        ;;
esac
