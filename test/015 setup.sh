#!/bin/env bash
echo "00112233445566778899aabbccddeeff This	path contains a NUL" | tr '\11' '\0'  > image/md5sum.txt
