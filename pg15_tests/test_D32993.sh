#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressParallel
grep_in_java_test \
  "Failed tests: [yb_parallel_colocated]" \
  TestPgRegressParallel

failing_java_test TestPgRegressPlanner
grep_in_java_test \
  "Failed tests: [yb_planner_joins, yb_planner_size_estimates]" \
  TestPgRegressPlanner
  