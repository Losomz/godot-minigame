#!/bin/bash
set -e
scons platform=linux arch=x86_64 target=template_release embed_resources=yes
scons platform=linux arch=arm64 target=template_release embed_resources=yes
