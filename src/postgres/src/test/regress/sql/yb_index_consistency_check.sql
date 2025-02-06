-- Basic test
SET yb_explain_hide_non_deterministic_fields = true;

SET yb_bnl_batch_size = 3;
CREATE TABLE abcd(a int primary key, b int, c int, d int);
CREATE INDEX ON abcd (b ASC) INCLUDE (c, d);
INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, 10) i;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF) SELECT yb_lsm_index_check('abcd_b_c_d_idx'::regclass::oid);
SELECT yb_lsm_index_check('abcd_b_c_d_idx'::regclass::oid);
RESET yb_bnl_batch_size;
