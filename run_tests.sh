#!/bin/env bash
TEST_DIR=./test
TIMEOUT=2m
NUM_PASS=0
NUM_FAIL=0
NUM_ERROR=0

if [[ -z "$QEMU_CMD" ]]; then
  echo '$QEMU_CMD is not set'
  exit 1
fi

for t in $TEST_DIR/*.txt; do

  base=$(basename "${t%.txt}")
  test_number=${base:0:3}
  test_name=${base:4}
  nb_lines=$(wc -l < "$t")

  # Run pre test script if required
  if [[ -x "$TEST_DIR/$test_number before.sh" ]]; then
    . "$TEST_DIR/$test_number before.sh"
  fi

  echo -n "Test #$test_number: ${test_name}... "
  # Run qemu with a timeout
  rm -f output.txt error.txt
  timeout --foreground $TIMEOUT bash -c "${QEMU_CMD} 1>output.txt 2>error.txt"

  err=$?
  if [[ $err -eq 124 ]]; then
    echo "[ERROR] (Time out)"
    NUM_ERROR=$((NUM_ERROR + 1))
  elif [[ $err -ne 0 ]]; then
    echo "[ERROR]"
    cat error.txt
    NUM_ERROR=$((NUM_ERROR + 1))
  else
    tail -n $nb_lines output.txt | diff --strip-trailing-cr -q "$t" - >/dev/null 2>&1
    if [[ $? -eq 0 ]]; then
      echo "[PASS]"
      NUM_PASS=$((NUM_PASS + 1))
    else
      echo "[FAIL]"
      NUM_FAIL=$((NUM_FAIL + 1))
    fi
  fi

  # Run post test script if required
  if [[ -x "$TEST_DIR/$test_number after.sh" ]]; then
    . "$TEST_DIR/$test_number after.sh"
  fi

done

echo
echo "============================================================================"
echo "# Test summary for uefi-md5sum"
echo "============================================================================"
echo "# TOTAL: $((NUM_PASS + NUM_FAIL + NUM_ERROR))"
echo "# PASS:  $NUM_PASS"
echo "# FAIL:  $NUM_FAIL"
echo "# ERROR: $NUM_ERROR"
echo "============================================================================"

if [[ $NUM_FAIL -ne 0 || $NUM_ERROR -ne 0 ]]; then
  exit 1
fi
