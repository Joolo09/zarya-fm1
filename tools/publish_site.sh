#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Build the project site and the editor, then publish web/dist to the
# gh-pages branch (GitHub Pages serves it from there). No firmware is involved.
set -e
cd "$(dirname "$0")/.."
make wasm schema
(cd web && npm ci && npm test && npm run build)
tmp=$(mktemp -d)
cp -R web/dist/. "$tmp"/
touch "$tmp/.nojekyll"
git -C "$tmp" init -q -b gh-pages
git -C "$tmp" add -A
git -C "$tmp" -c user.name="${GIT_AUTHOR_NAME:-ZARYA}" -c user.email="${GIT_AUTHOR_EMAIL:-noreply@example.com}" \
    commit -qm "Publish site from $(git rev-parse --short HEAD)"
git -C "$tmp" push -f "$(git remote get-url origin)" gh-pages
rm -rf "$tmp"
