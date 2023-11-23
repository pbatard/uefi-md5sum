#!/bin/env bash
echo "001122334455	6778899aabbccddeeff This hash contains a NUL" | tr '\11' '\0'  > image/md5sum.txt
