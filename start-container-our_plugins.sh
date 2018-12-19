#!/usr/bin/env sh

sed -i "s/PLUGINS +=.*our_plugins$/PLUGINS +=/g" core/extra.mk
./container_build.py shell
