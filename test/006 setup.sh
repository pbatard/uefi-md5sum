#!/bin/env bash
tr '\0' '\n' < /dev/zero | head -c 100001 > image/md5sum.txt
