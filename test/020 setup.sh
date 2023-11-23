#!/bin/env bash
echo "00112233445566778899aabbccddeeff file1" > image/md5sum.txt
echo "# Comment in the middle" >> image/md5sum.txt
echo "0123456789abcdef0123456789abcdef file2" >> image/md5sum.txt
