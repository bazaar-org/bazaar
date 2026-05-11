#!/usr/bin/env bash

DEPS=(
    meson
    ninja
    cmake
    blueprint-compiler
    desktop-file-utils
    python3-babel

    gtk4-devel
    libadwaita-devel
    libdex-devel
    flatpak-devel
    libxmlb-devel
    appstream-devel
    glycin-devel
    glycin-gtk4-devel
    libyaml-devel
    libsoup3-devel
    json-glib-devel
    md4c-devel
    gtksourceview5-devel
    webkitgtk6.0-devel
    libsecret-devel
    libproxy-devel
    malcontent-devel

    # lsp
    clangd
)

dnf install ${DEPS[@]}
