#!/bin/env bash
echo "00112233445566778899aabbccddeeff file1" > image/md5sum.txt
echo "# This	comment contains a NUL" | tr '\11' '\0' >> image/md5sum.txt
echo "abcdef12345678900987654321fedcba file2" >> image/md5sum.txt
