#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl wipe_restart
sleep 2 # Work around connection refused
bin/ysqlsh -X -v "ON_ERROR_STOP=1" <<EOT
CREATE TABLE prt1 (a int) PARTITION BY RANGE(a);
CREATE TABLE prt1_p1 PARTITION OF prt1 FOR VALUES FROM (0) TO (200);
SELECT t1.a FROM prt1 t1 WHERE t1.a < 450;
EOT
