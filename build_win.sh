#!/bin/bash
set -e
scons platform=windows arch=x86_64 target=template_release embed_resources=yes
scons platform=windows arch=x86_32 target=template_release embed_resources=yes
