BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.yb_notifications (
    node uuid NOT NULL,
    pid int NOT NULL,
    db oid NOT NULL,
    channel text COLLATE "C" NOT NULL,
    payload text COLLATE "C",
    CONSTRAINT yb_notifications_pkey PRIMARY KEY ((node, pid) HASH)
      WITH (table_oid = 8102)
  ) WITH (
    oids = false,
    table_oid = 8100,
    row_type_oid = 8101
  ) TABLESPACE pg_global SPLIT INTO 3 TABLETS;
COMMIT;
