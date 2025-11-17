BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_notifications (
    sender_node uuid NOT NULL,
    sender_pid int NOT NULL,
    dbid oid NOT NULL,
    data text COLLATE "C" NOT NULL,
    CONSTRAINT pg_yb_notifications_pkey PRIMARY KEY ((sender_node, sender_pid) HASH)
      WITH (table_oid = 8102)
  ) WITH (
    oids = false,
    table_oid = 8100,
    row_type_oid = 8101,
    tserver_hosted = true
  ) TABLESPACE pg_global SPLIT INTO 3 TABLETS;
COMMIT;
