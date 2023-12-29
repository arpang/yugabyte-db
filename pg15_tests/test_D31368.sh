#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressProc'
grep_in_java_test \
  'failed tests: [yb_hash_code, yb_lock_status]' \
  'TestPgRegressProc'
