#!/bin/bash
set -e
cd "$(dirname "$0")/../.."
scons -f plugin/build/SConstruct platform=windows arch=x86_64 target=template_release embed_resources=yes
