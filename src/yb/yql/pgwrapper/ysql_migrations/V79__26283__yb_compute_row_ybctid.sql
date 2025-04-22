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
    -- implementation of yb_compute_row_ybctid
    (8096, 'yb_compute_row_ybctid', 11, 10, 12,
     1, 0, 0, '-', 'f',
     false, false, false, false, 'i',
     's', 3, 1, '17', '26 2249 17',
     NULL, NULL, '{relid,key_atts,ybbasectid}',
     '({CONST :consttype 17 :consttypmod -1 :constcollid 0 :constlen -1 :constbyval false :constisnull true :location 102 :constvalue <>})',
     NULL, 'yb_compute_row_ybctid', NULL, NULL, NULL)
  ON CONFLICT DO NOTHING;

  INSERT INTO pg_catalog.pg_description (
    objoid, classoid, objsubid, description
  ) VALUES (
    8095, 1255, 0, 'returns the ybctid given a relation and its key attributes'
  ) ON CONFLICT DO NOTHING;
COMMIT;
