#!/bin/sh

find ./ -print | grep -v '.DAV' | xargs -I {} chown daemon {}
find ./ -print | grep -v '.DAV' | xargs -I {} chgrp daemon {}

find ./ -print | grep '.DAV' | xargs -I {} chown root {}
find ./ -print | grep '.DAV' | xargs -I {} chgrp root {}

find ./ -print | grep 'dist' | xargs -I {} chown root {}
find ./ -print | grep 'dist' | xargs -I {} chgrp root {}

find ./ -print | grep '.DAV' | xargs -I {} rm -f {}
#find ./ -print | grep '.DAV' | xargs -I {} rm -f {}/*
