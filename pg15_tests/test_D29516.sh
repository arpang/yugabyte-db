#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_index_backfill-test \
  --gtest_filter PgIndexBackfillTest.InsertsWhileCreatingIndex
