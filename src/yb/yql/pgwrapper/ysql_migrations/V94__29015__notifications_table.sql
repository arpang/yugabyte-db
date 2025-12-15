BEGIN;
  CREATE TABLE IF NOT EXISTS pg_catalog.pg_yb_notifications (
    notif_uuid uuid NOT NULL,
    sender_node_uuid uuid NOT NULL,
    sender_pid int NOT NULL,
    db_oid oid NOT NULL,
    data text COLLATE "C" NOT NULL,
    CONSTRAINT pg_yb_notifications_pkey PRIMARY KEY ((notif_uuid))
      WITH (table_oid = 8103)
  ) WITH (
    oids = false,
    table_oid = 8101,
    row_type_oid = 8102
  ) TABLESPACE pg_global;
COMMIT;
