#!/bin/env bash

## @file
#  Test generation script for MD5Sum.
#
#  Copyright (c) 2023, Pete Batard <pete@akeo.ie>
#
#  SPDX-License-Identifier: GPL-2.0-or-later
#
##

TEST_DIR=test

rm -rf $TEST_DIR
mkdir $TEST_DIR

test_number=0

while IFS=$'\r\n' read -r line; do
  if [[ ${line:0:1} == "#" ]]; then
    test_number=$((test_number + 1))
    data_file=$(printf "%s/%03d %s.dat" $TEST_DIR $test_number "${line:2}")
    setup_file=$(printf "%s/%03d setup.sh" $TEST_DIR $test_number)
    teardown_file=$(printf "%s/%03d teardown.sh" $TEST_DIR $test_number)
  elif [[ ${line:0:1} == ">" ]]; then
    if [[ ! -f "$setup_file" ]]; then
      echo "#!/bin/env bash" > "$setup_file"
      chmod 755 "$setup_file"
    fi
    echo "${line:2}" >> "$setup_file"
  elif [[ ${line:0:1} == "<" ]]; then
    if [[ ! -f "$teardown_file" ]]; then
      echo "#!/bin/env bash" > "$teardown_file"
      chmod 755 "$teardown_file"
    fi
    echo "${line:2}" >> "$teardown_file"
  elif [[ ! -z "$line" ]]; then
    echo "${line}" >> "$data_file"
  fi
done < gen_tests.txt
