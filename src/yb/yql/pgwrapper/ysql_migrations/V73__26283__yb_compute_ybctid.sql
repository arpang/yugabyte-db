BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

  INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang,
    procost, prorows, provariadic, prosupport, prokind,
    prosecdef, proleakproof, proisstrict, proretset, provolatile,
    proparallel, pronargs, pronargdefaults, prorettype, proargtypes,
    proallargtypes, proargmodes, proargnames, proargdefaults, protrftypes,
    prosrc, probin, proconfig, proacl
  ) VALUES
    -- implementation of yb_index_check
    (8091, 'yb_compute_ybctid', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, true, false, 'i',
     's', 3, 0, '17', '26 2277 17',
     NULL, NULL, NULL, NULL, NULL,
     'yb_compute_ybctid', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8091, 1255, 0, 'given index key attributes, returns index row ybctid'
  ) ON CONFLICT DO NOTHING;
COMMIT;
