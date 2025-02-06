SET yb_explain_hide_non_deterministic_fields = true;

-- Basic test
SET yb_bnl_batch_size = 3;
CREATE TABLE abcd(a int primary key, b int, c int, d int);
CREATE INDEX abcd_b_c_d_idx ON abcd (b ASC) INCLUDE (c, d);
INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, 10) i;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF) SELECT yb_lsm_index_check('abcd_b_c_d_idx'::regclass::oid);
SELECT yb_lsm_index_check('abcd_b_c_d_idx'::regclass::oid);
RESET yb_bnl_batch_size;

INSERT INTO abcd SELECT i, i, i, i FROM generate_series(11, 2000) i;
INSERT INTO abcd values (2001, NULL, NULL, NULL);

-- Partial index
CREATE INDEX abcd_b_c_idx ON abcd(b) INCLUDE (c) WHERE d > 50;
SELECT yb_lsm_index_check('abcd_b_c_idx'::regclass::oid);

CREATE OR REPLACE FUNCTION double_value(input_value NUMERIC)
RETURNS NUMERIC AS $$
BEGIN
    RETURN input_value * 2;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

CREATE INDEX abcd_b_c_idx1 ON abcd(b) INCLUDE (c) WHERE double_value(d) > 50;
SELECT yb_lsm_index_check('abcd_b_c_idx1'::regclass::oid);

-- Expression index
CREATE INDEX abcd_expr_expr1_d_idx ON abcd ((2*c) ASC, (2*b) ASC) INCLUDE (d);
SELECT yb_lsm_index_check('abcd_expr_expr1_d_idx'::regclass::oid);
CREATE INDEX abcd_double_value_d_idx ON abcd (double_value(c) ASC) INCLUDE (d);
SELECT yb_lsm_index_check('abcd_double_value_d_idx'::regclass::oid);

-- Unique index
CREATE UNIQUE INDEX abcd_b_c_d_idx1 ON abcd (b ASC) INCLUDE (c, d);
SELECT yb_lsm_index_check('abcd_b_c_d_idx1'::regclass::oid);

-- NULLS NOT DISTINCT
CREATE UNIQUE INDEX abcd_b_c_d_idx2 ON abcd (b ASC) INCLUDE (c, d) NULLS NOT DISTINCT;
SELECT yb_lsm_index_check('abcd_b_c_d_idx2'::regclass::oid);

-- Index does not exist
SELECT yb_lsm_index_check('nonexisting_idx'::regclass::oid);

-- PK index
SELECT yb_lsm_index_check('abcd_pkey'::regclass::oid);

-- Non-LSM index
CREATE TEMP TABLE mytemp(a int unique);
SELECT yb_lsm_index_check('mytemp_a_key'::regclass::oid);

-- Non index relation oid
SELECT yb_lsm_index_check('abcd'::regclass::oid);

-- Types without equality operator
CREATE TABLE json_table (a TEXT, b JSON, c INT);
CREATE INDEX json_table_a_b_idx ON json_table(a) INCLUDE (b);
INSERT INTO json_table VALUES ('test', '{"test1":"test2", "test3": 4}', 1);
SELECT yb_lsm_index_check('json_table_a_b_idx'::regclass::oid);
