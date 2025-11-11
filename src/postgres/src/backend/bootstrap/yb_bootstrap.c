/*--------------------------------------------------------------------------------------------------
 *
 * yb_bootstrap.c
 *        YB commands for creating and altering table structures and settings
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/commands/yb_bootstrap.c
 *
 *------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "catalog/catalog.h"
#include "catalog/pg_attribute.h"
#include "catalog/pg_class.h"
#include "catalog/pg_database.h"
#include "catalog/pg_type.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/yb_cmds.h"
#include "executor/tuptable.h"
#include "executor/ybExpr.h"
#include "miscadmin.h"
#include "parser/parser.h"
#include "pg_yb_utils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "yb/yql/pggate/ybc_pggate.h"

static void
YBCAddSysCatalogColumn(YbcPgStatement yb_stmt,
					   IndexStmt *pkey_idx,
					   const char *attname,
					   int attnum,
					   Oid type_id,
					   int32 typmod,
					   bool key,
					   bool tserver_hosted)
{

	ListCell   *lc;
	bool		is_key = false;
	const YbcPgTypeEntity *col_type = YbDataTypeFromOidMod(attnum, type_id);

	bool is_hash = false;

	if (pkey_idx)
	{
		foreach(lc, pkey_idx->indexParams)
		{
			IndexElem  *elem = lfirst(lc);

			if (strcmp(elem->name, attname) == 0)
			{
				is_key = true;

				/*
				 * Check that hash ordering is not used for master hosted
				 * catalog relations.
				 */
				if (elem->ordering == SORTBY_HASH && !tserver_hosted)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("HASH ordering is not supported for "
									"master hosted catalog tables")));

				is_hash = tserver_hosted && (elem->ordering == SORTBY_HASH);
			}
		}
	}

	/*
	 * We will call this twice, first for key columns, then for regular
	 * columns to handle any re-ordering. So only adding the if matching the
	 * is_key property.
	 */
	if (key == is_key)
	{
		HandleYBStatus(YBCPgCreateTableAddColumn(yb_stmt,
												 attname,
												 attnum,
												 col_type,
												 is_hash,
												 is_key,
												 false /* is_desc */ ,
												 false /* is_nulls_first */ ));
	}
}

static void
YBCAddSysCatalogColumns(YbcPgStatement yb_stmt,
						TupleDesc tupdesc,
						IndexStmt *pkey_idx,
						const bool key,
						bool tserver_hosted)
{
	for (int attno = 0; attno < tupdesc->natts; attno++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, attno);

		YBCAddSysCatalogColumn(yb_stmt,
							   pkey_idx,
							   attr->attname.data,
							   attr->attnum,
							   attr->atttypid,
							   attr->atttypmod,
							   key,
							   tserver_hosted);
	}
}

void
YBCCreateSysCatalogTable(const char *table_name,
						 Oid table_oid,
						 TupleDesc tupdesc,
						 bool is_shared_relation,
						 IndexStmt *pkey_idx,
						 int tserver_hosted)
{
	/* Database and schema are fixed when running inidb. */
	Assert(IsBootstrapProcessingMode());
	char	   *db_name = "template1";
	char	   *schema_name = "pg_catalog";
	YbcPgStatement yb_stmt = NULL;
	YbcPgYbrowidMode ybrowid_mode = (pkey_idx == NULL
									 ? PG_YBROWID_MODE_RANGE
									 : PG_YBROWID_MODE_NONE);

	HandleYBStatus(YBCPgNewCreateTable(db_name,
									   schema_name,
									   table_name,
									   Template1DbOid,
									   table_oid,
									   is_shared_relation,
									   true /* is_sys_catalog_table */ ,
									   false,	/* if_not_exists */
									   ybrowid_mode,
									   !tserver_hosted,	/* is_colocated_via_database */
									   InvalidOid /* tablegroup_oid */ ,
									   InvalidOid /* colocation_id */ ,
									   InvalidOid /* tablespace_oid */ ,
									   false /* is_matview */ ,
									   InvalidOid /* pg_table_oid */ ,
									   InvalidOid /* old_relfilenode_oid */ ,
									   false /* is_truncate */ ,
									   tserver_hosted,
									   &yb_stmt));

	if (tserver_hosted)
	{
		/* Tserver hosted catalog tables have 1 tablet by default. */
		YBCPgCreateTableSetNumTablets(yb_stmt, 1);
	}

	/* Add all key columns first, then the regular columns */
	if (pkey_idx != NULL)
	{
		YBCAddSysCatalogColumns(yb_stmt, tupdesc, pkey_idx, /* key */ true, tserver_hosted);
	}
	YBCAddSysCatalogColumns(yb_stmt, tupdesc, pkey_idx, /* key */ false, tserver_hosted);

	HandleYBStatus(YBCPgExecCreateTable(yb_stmt));
}
