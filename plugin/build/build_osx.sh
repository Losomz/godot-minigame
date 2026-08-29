#!/bin/bash
set -e
cd "$(dirname "$0")/../.."

: "${OSXCROSS_ROOT:=/opt/osxcross}"
if [ -d "$OSXCROSS_ROOT/target/bin" ]; then
  export PATH="$OSXCROSS_ROOT/target/bin:$PATH"
fi

scons -f plugin/build/SConstruct platform=macos arch=universal target=template_release embed_resources=yes
