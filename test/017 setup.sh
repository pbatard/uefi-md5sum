#!/bin/env bash
echo -n "00112233445566778899aabbccddeeff " > image/md5sum.txt
cat /dev/zero | tr '\0' 'a' | head -c 512 - >> image/md5sum.txt
