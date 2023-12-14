#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

java_test 'org.yb.pgsql.TestPgRegressSecondaryIndexScan#testPgRegressSecondaryIndexScan'
java_test 'org.yb.pgsql.TestPgRegressGin#testPgRegressGin'
java_test 'org.yb.pgsql.TestPgRegressJoin' false
grep_in_java_test \
  "failed tests: [yb_join_batching, yb_join_batching_plans]" \
  TestPgRegressJoin
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTempTest.DropTempTable
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTempTest.DiscardTempTable
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
--gtest_filter PgLibPqTempTest.DropTempSequence
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
--gtest_filter PgLibPqTempTest.DiscardTempSequence