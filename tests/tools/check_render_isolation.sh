#!/usr/bin/env bash
# Phase 3 rule: OpenGL is only used under GameOS/gameos/render/gl/.
# Fails if any other source file calls gl*() or includes a GL header.
set -u
repo=${1:?usage: check_render_isolation.sh <repo_dir>}
cd "$repo" || exit 2
bad=$(grep -rnE '^[^/]*\bgl[A-Z][A-Za-z0-9]*\(|#include *[<"](GL/|render/gl/)' \
        GameOS code mclib gui Viewer --include='*.cpp' --include='*.h' --include='*.hpp' \
      | grep -v '^GameOS/gameos/render/gl/' \
      | grep -vE ':[0-9]+:\s*//' \
      | grep -v 'glToD3DProjectionMatrix')  # math helper, not an OpenGL call
if [ -n "$bad" ]; then
    echo "OpenGL used outside GameOS/gameos/render/gl/:"
    echo "$bad"
    exit 1
fi
echo "OK: OpenGL confined to GameOS/gameos/render/gl/"
