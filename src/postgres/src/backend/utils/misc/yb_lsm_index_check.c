/*-------------------------------------------------------------------------
 *
 * yb_lsm_index_check.c
 * Utiity to check if an LSM index is consistent with its base relation
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/yb_lsm_index_check.c.c
 *
 *-------------------------------------------------------------------------
 */

#include <assert.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "postgres.h"
#include "access/heaptoast.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/relation.h"
#include "access/sysattr.h"
#include "access/table.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "c.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_cast.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_partitioned_table.h"
#include "catalog/pg_policy.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_range_d.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_statistic_d.h"
#include "catalog/pg_statistic_ext_d.h"
#include "catalog/pg_statistic_ext_data_d.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_trigger.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/pg_yb_logical_client_version.h"
#include "catalog/pg_yb_profile.h"
#include "catalog/pg_yb_role_profile.h"
#include "catalog/yb_catalog_version.h"
#include "catalog/yb_logical_client_version.h"
#include "catalog/yb_type.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/variable.h"
#include "commands/yb_cmds.h"
#include "common/ip.h"
#include "common/pg_yb_common.h"
#include "executor/nodeIndexonlyscan.h"
#include "executor/spi.h"
#include "executor/ybExpr.h"
#include "fmgr.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "libpq/hba.h"
#include "libpq/libpq-be.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/cost.h"
#include "optimizer/planmain.h"
#include "parser/parse_utilcmd.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/pg_locale.h"
#include "utils/rel.h"
#include "utils/snapshot.h"
#include "utils/spccache.h"
#include "utils/syscache.h"
#include "utils/uuid.h"

static void yb_lsm_index_check_internal(Oid indexoid);

static void
yb_lsm_index_row_check(TupleTableSlot *slot, List *equalProcOids,
					   Relation indexrel)
{
	bool indisunique = indexrel->rd_index->indisunique;
	bool indnullsnotdistinct = indexrel->rd_index->indnullsnotdistinct;
	bool indkeyhasnull = false;
	int indnatts = indexrel->rd_index->indnatts;
	int indnkeyatts = indexrel->rd_index->indnkeyatts;

	Assert(slot->tts_tupleDescriptor->natts ==
		   2 * (indnatts + 1) + (indisunique ? 1 : 0));

	/* First, validate ybctid and ybbasectid. */
	int ind_attnum = 2 * indnatts + 1;
	int base_attnum = ind_attnum + 1;

	bool ind_null;
	bool base_null;
	Datum ybbasectid_datum = slot_getattr(slot, ind_attnum, &ind_null);
	Datum base_datum = slot_getattr(slot, base_attnum, &base_null);
	Form_pg_attribute ind_att =
		TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);

	if (ind_null)
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				errmsg("index has row with ybbasectid = null")));

	if (base_null)
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				errmsg("index contains spurious row")));

	/* This should never happen */
	if (unlikely(!datumIsEqual(ybbasectid_datum, base_datum, ind_att->attbyval,
							   ind_att->attlen)))
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				errmsg("index's ybbasectid mismatch with base relation's ybctid")));

	/* Validate the index attributes. */
	for (int i = 0; i < indnatts; i++)
	{
		int ind_attnum = 2 * i + 1;
		int base_attnum = ind_attnum + 1;
		Form_pg_attribute ind_att =
			TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);

		bool ind_null;
		bool base_null;
		Datum ind_datum = slot_getattr(slot, ind_attnum, &ind_null);
		Datum base_datum = slot_getattr(slot, base_attnum, &base_null);

		if (ind_null && i < indnkeyatts)
			indkeyhasnull = true;

		if (ind_null || base_null)
		{
			if (ind_null && base_null)
				continue;

			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					errmsg("index row inconsistent with base table"),
					errdetail("NULL value mismatch for index attribute %d", i+1)));
		}

		if (datumIsEqual(ind_datum, base_datum, ind_att->attbyval,
						 ind_att->attlen))
			continue;

		/* Index key should be binary equal to base relation counterpart. */
		if (i < indnkeyatts)
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					errmsg("index row inconsistent with base table"),
					errdetail("Index row's key column is not binary equal to base row")));

		RegProcedure proc_oid = lfirst_int(list_nth_cell(equalProcOids, ind_attnum/2));
		if (proc_oid == InvalidOid)
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					errmsg("index row inconsistent with base table"),
					errdetail("Index row's non-key column is not binary equal "
							  "base row and doesn't have equalitu operator defined")));

		if (!DatumGetBool(OidFunctionCall2Coll(proc_oid, DEFAULT_COLLATION_OID,
											   ind_datum, base_datum)))
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					errmsg("index row inconsistent with base table"),
					errdetail("Index row's non-key column is neither binary "
							  "nor semantically equal to base row")));
	}

	if (indisunique)
	{
		/* Validate the ybuniqueidxkeysuffix */
		ind_attnum = 2 * (indnatts + 1) + 1;
		Datum ybuniqueidxkeysuffix_datum =
			slot_getattr(slot, ind_attnum, &ind_null);

		if (indnullsnotdistinct || !indkeyhasnull)
		{
			if (!ind_null)
				ereport(ERROR,
						(errcode(ERRCODE_INDEX_CORRUPTED),
						errmsg("unique index's ybuniqueidxkeysuffix is (unexpectedly) not null"),
						errdetail("It should be null if the index uses null-not-distinct "
								  "mode or key columns do not contain null(s)")));
		}
		else
		{
			ind_att = TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);
			bool equal = datumIsEqual(ybbasectid_datum, ybuniqueidxkeysuffix_datum,
								ind_att->attbyval, ind_att->attlen);
			if (!equal)
				ereport(ERROR,
						(errcode(ERRCODE_INDEX_CORRUPTED),
						errmsg("unique index's ybuniqueidxkeysuffix doesn't match ybbasectid"),
						errdetail("The two should match for index in null-are-distinct "
								  "mode when key columns contain null(s)")));
		}
	}
}

static int
yb_lsm_index_expected_row_count(Relation indexrel, Relation baserel)
{
	StringInfoData querybuf;
	initStringInfo(&querybuf);
	appendStringInfo(&querybuf, "/*+SeqScan(%s)*/ SELECT count(*) from %s",
					 RelationGetRelationName(baserel),
					 RelationGetRelationName(baserel));

	bool indpred_isnull;
	Datum indpred_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
										  Anum_pg_index_indpred,
										  &indpred_isnull);
	if (!indpred_isnull)
	{
		Oid basereloid = RelationGetRelid(baserel);
		char *indpred_clause = TextDatumGetCString(DirectFunctionCall2(pg_get_expr,
																	   indpred_datum,
																	   basereloid));
		appendStringInfo(&querybuf, " WHERE %s", indpred_clause);
	}

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");

	if (SPI_execute(querybuf.data, true, 0) != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed:");

	Assert(SPI_processed == 1);
	Assert(SPI_tuptable->tupdesc->natts == 1);

	bool isnull;
	Datum val =
		heap_getattr(SPI_tuptable->vals[0], 1, SPI_tuptable->tupdesc, &isnull);
	Assert(!isnull);
	int expected_rowcount = DatumGetInt64(val);

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");

	return expected_rowcount;
}

static void
yb_lsm_partitioned_index_check(Oid parentindexId)
{
	ListCell *lc;
	foreach(lc, find_inheritance_children(parentindexId, AccessShareLock))
	{
		Oid childindexId = ObjectIdGetDatum(lfirst_oid(lc));
		yb_lsm_index_check_internal(childindexId);
	}
}

static Plan* get_outer_plan(Relation indexrel)
{
	Expr *expr;
	List *index_cols = NIL;
	List *index_scan_tlist = NIL;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;

	TupleDesc indexdesc = RelationGetDescr(indexrel);
	bool indisunique = indexrel->rd_index->indisunique;

	int i;
	for (i = 0; i < indexdesc->natts; i++)
	{
		attr = TupleDescAttr(indexdesc, i);
		expr = (Expr *) makeVar(INDEX_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, i + 1, "", false);
		index_cols = lappend(index_cols, target_entry);
		index_scan_tlist = lappend(index_scan_tlist, target_entry);
	}

	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	Var *ybbasectid_expr = makeVar(INDEX_VAR, YBIdxBaseTupleIdAttributeNumber,
								   attr->atttypid, attr->atttypmod,
								   attr->attcollation, 0);
	target_entry = makeTargetEntry((Expr *) ybbasectid_expr, i + 1, "", false);
	index_scan_tlist = lappend(index_scan_tlist, target_entry);

	if (indisunique)
	{
		attr = SystemAttributeDefinition(YBUniqueIdxKeySuffixAttributeNumber);
		expr = (Expr *) makeVar(INDEX_VAR, YBUniqueIdxKeySuffixAttributeNumber,
								attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry((Expr *) expr, i + 2, "", false);
		index_scan_tlist = lappend(index_scan_tlist, target_entry);
	}

	IndexOnlyScan *index_scan = makeNode(IndexOnlyScan);
	Plan *plan = &index_scan->scan.plan;
	plan->targetlist = index_scan_tlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	index_scan->scan.scanrelid = 1; /* baserelid's rt index */
	index_scan->indexid = RelationGetRelid(indexrel);
	index_scan->indextlist = index_cols;
	return (Plan*) index_scan;
}

static Plan* get_inner_plan(Relation baserel, Relation indexrel)
{
	Expr *expr;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;

	TupleDesc indexdesc = RelationGetDescr(indexrel);
	TupleDesc base_desc = RelationGetDescr(baserel);
	List *base_scan_tlist = NIL;

	bool isnull;
	List *indexprs;
	ListCell *next_expr;
	Datum exprsDatum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
									   Anum_pg_index_indexprs, &isnull);
	if (!isnull)
	{
		char *exprsString = TextDatumGetCString(exprsDatum);
		indexprs = (List *) stringToNode(exprsString);
		next_expr = list_head(indexprs);
	}

	int i;
	for (i = 0; i < indexdesc->natts; i++)
	{
		AttrNumber attnum = indexrel->rd_index->indkey.values[i];
		if (attnum > 0)
		{
			attr = TupleDescAttr(base_desc, attnum - 1);
			expr = (Expr *) makeVar(1, attnum, attr->atttypid, attr->atttypmod,
									attr->attcollation, 0);
		}
		else
		{
			Assert(next_expr);
			expr = (Expr *) lfirst(next_expr);
			next_expr = lnext(indexprs, next_expr);
		}

		/* Assert that type of index attribute match base relation attribute. */
		Assert(exprType((Node *) expr) ==
			   TupleDescAttr(indexdesc, i)->atttypid);
		target_entry = makeTargetEntry(expr, i + 1, "", false);
		base_scan_tlist = lappend(base_scan_tlist, target_entry);
	}

	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *ybctid_expr = makeVar(1, YBTupleIdAttributeNumber, attr->atttypid,
							   attr->atttypmod, attr->attcollation, 0);
	target_entry = makeTargetEntry((Expr *) ybctid_expr, i + 1, "", false);
	base_scan_tlist = lappend(base_scan_tlist, target_entry);

	/* IndexScan qual */
	/* LHS */
	Var *ybctid_from_index = (Var *) copyObject(ybctid_expr);
	ybctid_from_index->varno = INDEX_VAR;
	ybctid_from_index->varattno = 1;

	/* RHS */
	Bitmapset *bms = NULL;
	List *params = NIL;
	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	for (int i = 0; i < yb_bnl_batch_size; i++)
	{
		bms = bms_add_member(bms, i);
		Param *param = makeNode(Param);
		param->paramkind = PARAM_EXEC;
		param->paramid = i;
		param->paramtype = attr->atttypid;
		param->paramtypmod = attr->atttypmod;
		param->paramcollid = attr->attcollation;
		param->location = -1;
		params = lappend(params, param);
	}
	ArrayExpr *arrexpr = makeNode(ArrayExpr);
	arrexpr->array_typeid = BYTEAARRAYOID;
	arrexpr->element_typeid = BYTEAOID;
	arrexpr->multidims = false;
	arrexpr->array_collid = InvalidOid;
	arrexpr->location = -1;
	arrexpr->elements = params;

	// Qual expression
	ScalarArrayOpExpr *saop = makeNode(ScalarArrayOpExpr);
	saop->opno = ByteaEqualOperator;
	saop->opfuncid = get_opcode(ByteaEqualOperator);
	saop->useOr = true;
	saop->inputcollid = InvalidOid;
	saop->args = list_make2(ybctid_from_index, arrexpr);

	ScalarArrayOpExpr *orig_saop = copyObjectImpl(saop);
	orig_saop->args = list_make2(ybctid_expr, arrexpr);

	target_entry = makeTargetEntry((Expr *) ybctid_from_index, 1, "", false);
	List *base_indextlist = list_make1(target_entry);

	/* Partial index predicate. */
	List *partial_idx_pred = NIL;
	List *partial_idx_colrefs = NIL;
	bool partial_idx_pushdown = false;
	bool indpred_isnull = false;
	Datum indpred_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
										  Anum_pg_index_indpred, &indpred_isnull);
	if (!indpred_isnull)
	{
		Expr *indpred = stringToNode(TextDatumGetCString(indpred_datum));
		partial_idx_pushdown = YbCanPushdownExpr(indpred, &partial_idx_colrefs);
		partial_idx_pred = lappend(partial_idx_pred, indpred);
	}

	IndexScan *base_scan = makeNode(IndexScan);
	Plan *plan = &base_scan->scan.plan;
	plan->targetlist = base_scan_tlist; // output from the node
	plan->lefttree = NULL;
	plan->righttree = NULL;
	plan->qual = !partial_idx_pushdown ? partial_idx_pred : NIL;
	plan->extParam = bms;
	plan->allParam = bms;
	base_scan->scan.scanrelid = 1; // baserelid's rt index
	base_scan->indexid = RelationGetRelid(baserel);
	base_scan->indextlist = base_indextlist; // index cols
	base_scan->indexqual = list_make1(saop);
	base_scan->yb_rel_pushdown.quals = partial_idx_pushdown ? partial_idx_pred :
															  NIL;
	base_scan->yb_rel_pushdown.colrefs =
		partial_idx_pushdown ? partial_idx_colrefs : NIL;
	return (Plan *) base_scan;
}

static Plan* get_join_plan(Relation baserel, Relation indexrel)
{
	Expr *expr;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;

	/* Index relation scan */
	Plan* index_scan = get_outer_plan(indexrel);
	TupleDesc index_scan_desc = ExecTypeFromTL(index_scan->targetlist);

	/* Base relation scan */
	Plan* base_scan = get_inner_plan(baserel, indexrel);

	TupleDesc indexdesc = RelationGetDescr(indexrel);

	// BNL
	List *join_tlist = NIL;
	int i;
	for (i = 0; i < index_scan_desc->natts; i++)
	{
		attr = TupleDescAttr(index_scan_desc, i);
		expr = (Expr *) makeVar(OUTER_VAR, // index's rt index
								i + 1, attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 1, "", false);
		join_tlist = lappend(join_tlist, target_entry);

		/*
		 * For unique index is ybuniqueidxkeysuffix, it doesn't have base rel
		 * counterpart.
		 */
		if (indexrel->rd_index->indisunique && i + 1 == index_scan_desc->natts)
			continue;

		expr = (Expr *) makeVar(INNER_VAR, // index's rt index
								i + 1, attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 2, "", false);
		join_tlist = lappend(join_tlist, target_entry);
	}

	// Join clause
	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	Var *join_lhs = makeVar(OUTER_VAR, indexdesc->natts + 1, attr->atttypid,
							attr->atttypmod, attr->attcollation, 0);

	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *join_rhs = makeVar(INNER_VAR, indexdesc->natts + 1, attr->atttypid,
							attr->atttypmod, attr->attcollation, 0);

	OpExpr *join_clause = (OpExpr *) make_opclause(ByteaEqualOperator, BOOLOID,
												   false, (Expr *) join_lhs,
												   (Expr *) join_rhs,
												   InvalidOid, InvalidOid);
	join_clause->opfuncid = get_opcode(ByteaEqualOperator);

	// NLP
	NestLoopParam *nlp = makeNode(NestLoopParam);
	nlp->paramno = 0;
	nlp->paramval = join_lhs;
	nlp->yb_batch_size = yb_bnl_batch_size;

	YbBatchedNestLoop *join_plan = makeNode(YbBatchedNestLoop);
	Plan* plan = &join_plan->nl.join.plan;
	plan->targetlist = join_tlist;
	plan->lefttree = (Plan *) index_scan;
	plan->righttree = (Plan *) base_scan;
	join_plan->nl.join.jointype = JOIN_LEFT;
	join_plan->nl.join.inner_unique = true;
	join_plan->nl.join.joinqual = list_make1(join_clause);
	join_plan->nl.nestParams = list_make1(nlp);
	join_plan->first_batch_factor = 1.0;
	join_plan->num_hashClauseInfos = 1;
	join_plan->hashClauseInfos = palloc0(sizeof(YbBNLHashClauseInfo));
	join_plan->hashClauseInfos->hashOp = ByteaEqualOperator;
	join_plan->hashClauseInfos->innerHashAttNo = join_rhs->varattno; // TODO
	join_plan->hashClauseInfos->outerParamExpr = (Expr *) join_lhs;	 // TODO
	join_plan->hashClauseInfos->orig_expr = (Expr *) join_clause;
	return (Plan *) join_plan;
}

static void
yb_lsm_index_check_internal(Oid indexoid)
{
	Relation indexrel = RelationIdGetRelation(indexoid);

	if (indexrel->rd_rel->relkind != RELKIND_INDEX &&
		indexrel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX)
		elog(ERROR, "Object is not an index");

	Assert(indexrel->rd_index);

	if (!IsYBRelation(indexrel))
		elog(ERROR, "This operation is only supported for LSM indexes");

	if (!indexrel->rd_index->indisvalid)
		elog(ERROR, "Index '%s' is marked invalid",
			 RelationGetRelationName(indexrel));
	if (!indexrel->rd_index->indisready)
		elog(ERROR, "Index '%s' is not ready",
			 RelationGetRelationName(indexrel));
	if (!indexrel->rd_index->indislive)
		elog(ERROR, "Index '%s' is not live",
			 RelationGetRelationName(indexrel));

	/* There is not separate PK index, hence it is always consistent. */
	if (indexrel->rd_index->indisprimary)
	{
		RelationClose(indexrel);
		return;
	}

	if (indexrel->rd_rel->relam == YBGIN_AM_OID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("this operation is not yet supported for ybgin indexes")));

	if (indexrel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
	{
		RelationClose(indexrel);
		return yb_lsm_partitioned_index_check(indexoid);
	}

	TupleDesc indexdesc = RelationGetDescr(indexrel);

	Oid basereloid = indexrel->rd_index->indrelid;
	Relation baserel = RelationIdGetRelation(basereloid);

	yb_index_checker = true;
	
	// join plan
	// here
	Plan *join_plan = get_join_plan(baserel, indexrel);

	List *equalProcOids = NIL;
	for (int i = 0; i < indexdesc->natts; i++)
	{
		const FormData_pg_attribute *attr = TupleDescAttr(indexdesc, i);
		Oid operator_oid = OpernameGetOprid(list_make1(makeString("=")),
											attr->atttypid, attr->atttypid);
		if (operator_oid == InvalidOid)
		{
			equalProcOids = lappend_int(equalProcOids, InvalidOid);
			continue;
		}
		RegProcedure proc_oid = get_opcode(operator_oid);
		equalProcOids = lappend_int(equalProcOids, proc_oid);
	}

	// Execution
	EState *estate = CreateExecutorState();
	MemoryContext oldctxt = MemoryContextSwitchTo(estate->es_query_cxt);

	RangeTblEntry *rte1 = makeNode(RangeTblEntry);
	rte1->rtekind = RTE_RELATION;
	rte1->relid = basereloid;
	rte1->relkind = RELKIND_RELATION;
	ExecInitRangeTable(estate, list_make1(rte1));

	estate->es_param_exec_vals =
		(ParamExecData *) palloc0(yb_bnl_batch_size * sizeof(ParamExecData));

	PlanState *join_state = ExecInitNode((Plan *) join_plan, estate, 0);
	TupleTableSlot *output;

	int index_rowcount = 0;
	while ((output = ExecProcNode(join_state)))
	{
		index_rowcount++;
		// TODO: Why is this required?
		output->tts_ops->materialize(output);
		yb_lsm_index_row_check(output, equalProcOids, indexrel);
	}
	ExecEndNode(join_state);

	int expected_rowcount = yb_lsm_index_expected_row_count(indexrel, baserel);

	if (index_rowcount != expected_rowcount)
		elog(ERROR,
			 "Index is missing some rows. Index has %d rows, expected rows "
			 "%d",
			 index_rowcount, expected_rowcount);

	/* Reset state */
	yb_index_checker = false;
	RelationClose(indexrel);
	RelationClose(baserel);
	ExecResetTupleTable(estate->es_tupleTable, true);
	ExecCloseResultRelations(estate);
	ExecCloseRangeTableRelations(estate);
	MemoryContextSwitchTo(oldctxt);
	FreeExecutorState(estate);
}

Datum
yb_lsm_index_check(PG_FUNCTION_ARGS)
{
	Oid indexoid = PG_GETARG_OID(0);
	yb_lsm_index_check_internal(indexoid);
	PG_RETURN_VOID();
}
