BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_notifications (
    sender_node uuid NOT NULL,
    sender_pid int NOT NULL,
    dbid oid NOT NULL,
    is_listen bool NOT NULL,
    data text COLLATE "C" NOT NULL,
    CONSTRAINT pg_yb_notifications_pkey PRIMARY KEY ((sender_node, sender_pid) HASH)
      WITH (table_oid = 8103)
  ) WITH (
    oids = false,
    table_oid = 8101,
    row_type_oid = 8102
  ) TABLESPACE pg_global SPLIT INTO 3 TABLETS;
COMMIT;
