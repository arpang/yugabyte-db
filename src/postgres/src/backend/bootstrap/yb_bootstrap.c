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
					   bool tserver_hosted,
					   bool *is_hash_sharded)
{

	ListCell   *lc;
	bool		is_key = false;
	const YbcPgTypeEntity *col_type = YbDataTypeFromOidMod(attnum, type_id);

	bool		is_hash = false;

	if (pkey_idx)
	{
		foreach(lc, pkey_idx->indexParams)
		{
			IndexElem  *elem = lfirst(lc);

			if (strcmp(elem->name, attname) == 0)
			{
				is_key = true;

				/*
				 * Check that hash sharding is only used for tserver-hosted
				 * catalog relations.
				 */
				if (elem->ordering == SORTBY_HASH && !tserver_hosted)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("HASH sharding is only supported for "
									"tserver hosted catalog tables.")));

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

	if (is_hash_sharded)
		*is_hash_sharded = *is_hash_sharded || is_hash;
}

static void
YBCAddSysCatalogColumns(YbcPgStatement yb_stmt,
						TupleDesc tupdesc,
						IndexStmt *pkey_idx,
						const bool key,
						bool tserver_hosted,
						bool *is_hash_sharded)
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
							   tserver_hosted,
							   is_hash_sharded);
	}
}

static void
YBCAddSplitOptionsForCatalogTable(YbOptSplit *split_options,
								  bool is_hash_sharded,
								  YbcPgStatement yb_stmt)
{
	Assert(split_options);

	if (!is_hash_sharded)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("split options is only supported for hash sharded "
						"tserver-hosted catalog tables")));

	Assert(split_options->split_type == NUM_TABLETS);

	if (split_options->num_tablets <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("num_tablets must be > 0")));

	YBCPgCreateTableSetNumTablets(yb_stmt, split_options->num_tablets);
}

void
YBCCreateSysCatalogTable(const char *table_name,
						 Oid table_oid,
						 TupleDesc tupdesc,
						 bool is_shared_relation,
						 IndexStmt *pkey_idx,
						 bool tserver_hosted)
{
	/* Database and schema are fixed when running inidb. */
	Assert(IsBootstrapProcessingMode());
	char	   *db_name = "template1";
	char	   *schema_name = "pg_catalog";
	YbcPgStatement yb_stmt = NULL;
	YbcPgYbrowidMode ybrowid_mode = (pkey_idx == NULL
									 ? PG_YBROWID_MODE_RANGE
									 : PG_YBROWID_MODE_NONE);

	if (tserver_hosted != YbIsTserverHostedCatalogRel(table_oid))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("tserver_hosted flag for \"%s\" does not match "
						"YbIsTserverHostedCatalogRel(%u)", table_name, table_oid)));

	HandleYBStatus(YBCPgNewCreateTable(db_name,
									   schema_name,
									   table_name,
									   Template1DbOid,
									   table_oid,
									   is_shared_relation,
									   true /* is_sys_catalog_table */ ,
									   false,	/* if_not_exists */
									   ybrowid_mode,
									   !tserver_hosted, /* is_colocated_via_database */
									   InvalidOid /* tablegroup_oid */ ,
									   InvalidOid /* colocation_id */ ,
									   InvalidOid /* tablespace_oid */ ,
									   false /* is_matview */ ,
									   InvalidOid /* pg_table_oid */ ,
									   InvalidOid /* old_relfilenode_oid */ ,
									   false /* is_truncate */ ,
									   tserver_hosted,
									   &yb_stmt));

	bool		is_hash_sharded = false;

	/* Add all key columns first, then the regular columns */
	if (pkey_idx != NULL)
	{
		YBCAddSysCatalogColumns(yb_stmt, tupdesc, pkey_idx, /* key */ true, tserver_hosted, &is_hash_sharded);
	}
	YBCAddSysCatalogColumns(yb_stmt, tupdesc, pkey_idx, /* key */ false, tserver_hosted, /* is_hash_sharded */ NULL);

	if (pkey_idx && pkey_idx->split_options)
		YBCAddSplitOptionsForCatalogTable(pkey_idx->split_options, is_hash_sharded, yb_stmt);

	HandleYBStatus(YBCPgExecCreateTable(yb_stmt));
}
