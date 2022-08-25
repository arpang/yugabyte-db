CREATE DATABASE test_tablegroup_partitioned_tables;
\c test_tablegroup_partitioned_tables
CREATE TABLEGROUP tg1;
CREATE TABLE prt (id int PRIMARY KEY, v int) PARTITION BY RANGE (id) TABLEGROUP tg1;
CREATE TABLE prt_p1 PARTITION OF prt FOR VALUES FROM (1) TO (4);
\dgrt
CREATE TABLE prt_p2 PARTITION OF prt FOR VALUES FROM (5) TO (9) TABLEGROUP tg1;