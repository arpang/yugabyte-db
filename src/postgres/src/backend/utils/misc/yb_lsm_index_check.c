/*-------------------------------------------------------------------------
 *
 * yb_index_check.c
 * Utiity to check if a YB index is consistent with its base relation.
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
 *	  src/backend/utils/misc/yb_index_check.c.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "catalog/heap.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_operator.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "executor/tuptable.h"
#include "executor/ybModifyTable.h"
#include "fmgr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "utils/arrayaccess.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

/* TODO: rename the file to yb_index_check.c */

static void yb_index_check_internal(Oid indexoid);

static void
check_index_row_consistency(TupleTableSlot *slot, List *equalProcOids,
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
	Datum ybctid_datum = slot_getattr(slot, base_attnum, &base_null);
	Form_pg_attribute ind_att = TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);

	if (ind_null)
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				errmsg("index has row with ybbasectid = null")));

	if (base_null)
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				errmsg("index contains spurious row")));

	/*
	 * TODO: datumIsEqual() returns false due to header size mismatch for types
	 * with variable length. For instance, in the following case, ybbasectid is
	 * VARATT_IS_1B, whereas ybctid VARATT_IS_4B. Look into it.
	 */
	/* This should never happen because this was the join condition */
	if (unlikely(!datum_image_eq(ybbasectid_datum, ybctid_datum,
								 ind_att->attbyval, ind_att->attlen)))
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				errmsg("index's ybbasectid mismatch with base relation's ybctid")));

	/* Validate the index attributes */
	for (int i = 0; i < indnatts; i++)
	{
		int ind_attnum = 2 * i + 1;
		int base_attnum = ind_attnum + 1;
		Form_pg_attribute ind_att = TupleDescAttr(slot->tts_tupleDescriptor, ind_attnum - 1);

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

		if (datum_image_eq(ind_datum, base_datum, ind_att->attbyval,
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
		Datum ybuniqueidxkeysuffix_datum = slot_getattr(slot, ind_attnum, &ind_null);

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
			bool equal = datum_image_eq(ybbasectid_datum,
										ybuniqueidxkeysuffix_datum,
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

/*
 * Generate plan corresponding to:
 *		SELECT (index attributes, ybbasectid, ybuniqueidxkeysuffix if index is
 *		unique) from indexrel
 */
static Plan *
indexrel_scan_plan(Relation indexrel)
{
	TupleDesc indexdesc = RelationGetDescr(indexrel);

	Expr *expr;
	List *index_cols = NIL;
	List *plan_targetlist = NIL;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	int i;

	for (i = 0; i < indexdesc->natts; i++)
	{
		attr = TupleDescAttr(indexdesc, i);
		expr = (Expr *) makeVar(INDEX_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, i + 1, "", false);
		index_cols = lappend(index_cols, target_entry);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	expr = (Expr *) makeVar(INDEX_VAR, YBIdxBaseTupleIdAttributeNumber,
							attr->atttypid, attr->atttypmod, attr->attcollation,
							0);
	target_entry = makeTargetEntry((Expr *) expr, i + 1, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);

	if (indexrel->rd_index->indisunique)
	{
		attr = SystemAttributeDefinition(YBUniqueIdxKeySuffixAttributeNumber);
		expr = (Expr *) makeVar(INDEX_VAR, YBUniqueIdxKeySuffixAttributeNumber,
								attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry((Expr *) expr, i + 2, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	IndexOnlyScan *index_scan = makeNode(IndexOnlyScan);
	Plan *plan = &index_scan->scan.plan;
	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	index_scan->scan.scanrelid = 1; /* only one relation is involved */
	index_scan->indexid = RelationGetRelid(indexrel);
	index_scan->indextlist = index_cols;
	return (Plan *) index_scan;
}

List *
fetch_index_expressions(Relation indexrel)
{
	bool isnull;
	List *indexprs = NIL;
	Datum exprs_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
										Anum_pg_index_indexprs, &isnull);
	if (!isnull)
		indexprs = (List *) stringToNode(TextDatumGetCString(exprs_datum));
	return indexprs;
}

/*
 * Generate plan corresponding to:
 *		SELECT (index attributes, ybctid) from baserel where ybctid IN (....)
 *		 AND <partial index predicate, if any>
 * This plan makes the inner subplan of BNL. So this must be an IndexScan, but
 * at the same time this should scan the base relation. To achive this, index
 * scan is done an a dummy index that is on the ybctid column. Under the hood,
 * it works as an index only scan on the base relation. This is similair to how
 * PK index scan works in YB.
 */
static Plan *
baserel_scan_plan(Relation baserel, Relation indexrel)
{
	TupleDesc indexdesc = RelationGetDescr(indexrel);
	TupleDesc base_desc = RelationGetDescr(baserel);

	Expr *expr;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	List *plan_targetlist = NIL;
	int i;
	List *indexprs = NIL;
	ListCell *next_expr = NULL;
	for (i = 0; i < indexdesc->natts; i++)
	{
		AttrNumber attnum = indexrel->rd_index->indkey.values[i];
		if (attnum > 0)
		{
			/* regular index attribute */
			attr = TupleDescAttr(base_desc, attnum - 1);
			expr = (Expr *) makeVar(1, attnum, attr->atttypid, attr->atttypmod,
									attr->attcollation, 0);
		}
		else
		{
			/* expression index attribute */
			if (next_expr == NULL)
			{
				/* Fetch expressions in the index */
				Assert(indexprs == NIL);
				indexprs = fetch_index_expressions(indexrel);
				next_expr = list_head(indexprs);
			}
			expr = (Expr *) lfirst(next_expr);
			next_expr = lnext(indexprs, next_expr);
		}

		/* Assert that type of index attribute match base relation attribute. */
		Assert(exprType((Node *) expr) ==
			   TupleDescAttr(indexdesc, i)->atttypid);
		target_entry = makeTargetEntry(expr, i + 1, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *ybctid_expr = makeVar(1, YBTupleIdAttributeNumber, attr->atttypid,
							   attr->atttypmod, attr->attcollation, 0);
	target_entry = makeTargetEntry((Expr *) ybctid_expr, i + 1, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);

	/* Index qual */

	/* LHS */
	Var *ybctid_from_index = (Var *) copyObject(ybctid_expr);
	ybctid_from_index->varno = INDEX_VAR;
	ybctid_from_index->varattno = 1;

	/* RHS */
	Bitmapset *params_bms = NULL;
	List *params = NIL;
	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	for (int i = 0; i < yb_bnl_batch_size; i++)
	{
		params_bms = bms_add_member(params_bms, i);
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

	ScalarArrayOpExpr *saop = makeNode(ScalarArrayOpExpr);
	saop->opno = ByteaEqualOperator;
	saop->opfuncid = get_opcode(ByteaEqualOperator);
	saop->useOr = true;
	saop->inputcollid = InvalidOid;
	saop->args = list_make2(ybctid_from_index, arrexpr);

	target_entry = makeTargetEntry((Expr *) ybctid_from_index, 1, "", false);
	List *indextlist = list_make1(target_entry);

	/* Partial index predicate */
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
	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	plan->qual = !partial_idx_pushdown ? partial_idx_pred : NIL;
	plan->extParam = params_bms;
	plan->allParam = params_bms;
	base_scan->scan.scanrelid = 1; /* only one relation is involved */
	base_scan->indexid = RelationGetRelid(baserel);
	base_scan->indextlist = indextlist;
	base_scan->indexqual = list_make1(saop);
	base_scan->yb_rel_pushdown.quals = partial_idx_pushdown ? partial_idx_pred :
															  NIL;
	base_scan->yb_rel_pushdown.colrefs =
		partial_idx_pushdown ? partial_idx_colrefs : NIL;
	return (Plan *) base_scan;
}

static Plan *
spurious_check_plan(Relation baserel, Relation indexrel)
{
	/*
	 * To check for spurious rows in index relation, we join the index relation
	 * with the base relation on indexrow.ybbasectid == baserow.ybctid. We use
	 * BNL for this purpose. To satisfy the BNL's join condition requirement,
	 * index relation is used as the outer (left) subplan.
	 */

	/* Outer subplan: index relation scan */
	Plan *indexrel_scan = indexrel_scan_plan(indexrel);
	TupleDesc indexrel_scan_desc = ExecTypeFromTL(indexrel_scan->targetlist);

	/* Inner subplan: base relation scan */
	Plan *baserel_scan = baserel_scan_plan(baserel, indexrel);

	/*
	 * Join plan targetlist
	 *
	 * Outer subplan tlist: index_attributes, ybbasectid, ybuniqueidxkeysuffix
	 * (if unique) Inner subplan tlist: index_attributes (scanned/computed from
	 * baserel), ybctid
	 *
	 * Join plan tlist: union of the above such that semantically same entries
	 * are next to each other:
	 * (outer_attr1, inner_attr1, outer_attr2, inner_att2 ..*,
	 * ybuniqueidxkeysuffix if unqiue index)
	 *
	 */
	Expr *expr;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	List *plan_tlist = NIL;
	int i;
	for (i = 0; i < indexrel_scan_desc->natts; i++)
	{
		attr = TupleDescAttr(indexrel_scan_desc, i);
		expr = (Expr *) makeVar(OUTER_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 1, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);

		/*
		 * For unique index, the last tlist entry is ybuniqueidxkeysuffix, which
		 * doesn't have base rel counterpart.
		 */
		if (indexrel->rd_index->indisunique &&
			i == indexrel_scan_desc->natts - 1)
			continue;

		expr = (Expr *) makeVar(INNER_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 2, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);
	}

	/* Join claue */
	TupleDesc indexdesc = RelationGetDescr(indexrel);
	attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
	Var *join_clause_lhs = makeVar(OUTER_VAR, indexdesc->natts + 1,
								   attr->atttypid, attr->atttypmod,
								   attr->attcollation, 0);
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *join_clause_rhs = makeVar(INNER_VAR, indexdesc->natts + 1,
								   attr->atttypid, attr->atttypmod,
								   attr->attcollation, 0);
	OpExpr *join_clause = (OpExpr *) make_opclause(ByteaEqualOperator, BOOLOID,
												   false, /* opretset */
												   (Expr *) join_clause_lhs,
												   (Expr *) join_clause_rhs,
												   InvalidOid, InvalidOid);
	join_clause->opfuncid = get_opcode(ByteaEqualOperator);

	/* NestLoopParam */
	NestLoopParam *nlp = makeNode(NestLoopParam);
	nlp->paramno = 0;
	nlp->paramval = join_clause_lhs;
	nlp->yb_batch_size = yb_bnl_batch_size;

	/* BNL join plan */
	YbBatchedNestLoop *join_plan = makeNode(YbBatchedNestLoop);
	Plan *plan = &join_plan->nl.join.plan;
	plan->targetlist = plan_tlist;
	plan->lefttree = (Plan *) indexrel_scan;
	plan->righttree = (Plan *) baserel_scan;
	join_plan->nl.join.jointype = JOIN_LEFT;
	join_plan->nl.join.inner_unique = true;
	join_plan->nl.join.joinqual = list_make1(join_clause);
	join_plan->nl.nestParams = list_make1(nlp);
	join_plan->first_batch_factor = 1.0;
	join_plan->num_hashClauseInfos = 1;
	join_plan->hashClauseInfos = palloc0(sizeof(YbBNLHashClauseInfo));
	join_plan->hashClauseInfos->hashOp = ByteaEqualOperator;
	join_plan->hashClauseInfos->innerHashAttNo = join_clause_rhs->varattno;
	join_plan->hashClauseInfos->outerParamExpr = (Expr *) join_clause_lhs;
	join_plan->hashClauseInfos->orig_expr = (Expr *) join_clause;
	return (Plan *) join_plan;
}

static EState *
init_estate(Relation baserel)
{
		/* Plan execution */
	EState *estate = CreateExecutorState();
	MemoryContext oldctxt = MemoryContextSwitchTo(estate->es_query_cxt);

	estate->yb_exec_params.yb_index_check = true;

	RangeTblEntry *rte1 = makeNode(RangeTblEntry);
	rte1->rtekind = RTE_RELATION;
	rte1->relid = RelationGetRelid(baserel);
	rte1->relkind = RELKIND_RELATION;
	ExecInitRangeTable(estate, list_make1(rte1));

	estate->es_param_exec_vals =
		(ParamExecData *) palloc0(yb_bnl_batch_size * sizeof(ParamExecData));
	MemoryContextSwitchTo(oldctxt);
	return estate;
}

static void
cleanup_estate(EState* estate)
{
	ExecResetTupleTable(estate->es_tupleTable, true);
	ExecCloseResultRelations(estate);
	ExecCloseRangeTableRelations(estate);
	FreeExecutorState(estate);
}

static void
check_spurious_index_rows(Relation baserel, Relation indexrel, EState* estate)
{
	Plan *join_plan = spurious_check_plan(baserel, indexrel);
	TupleDesc indexdesc = RelationGetDescr(indexrel);

	MemoryContext oldctxt = MemoryContextSwitchTo(estate->es_query_cxt);

	PlanState *join_state = ExecInitNode((Plan *) join_plan, estate, 0);

	List *equality_opcodes = NIL;
	for (int i = 0; i < indexdesc->natts; i++)
	{
		const FormData_pg_attribute *attr = TupleDescAttr(indexdesc, i);
		Oid operator_oid = OpernameGetOprid(list_make1(makeString("=")),
											attr->atttypid, attr->atttypid);
		if (operator_oid == InvalidOid)
		{
			equality_opcodes = lappend_int(equality_opcodes, InvalidOid);
			continue;
		}
		RegProcedure proc_oid = get_opcode(operator_oid);
		equality_opcodes = lappend_int(equality_opcodes, proc_oid);
	}

	TupleTableSlot *output;
	while ((output = ExecProcNode(join_state)))
	{
		const char* string = YbTupleTableSlotToString(output);
		elog(INFO, "1 scan index rel %s", string);
		check_index_row_consistency(output, equality_opcodes, indexrel);
	}
	ExecEndNode(join_state);

	MemoryContextSwitchTo(oldctxt);
	return;
}

/*
 * Generate plan corresponding to:
 *		SELECT (index attributes, ybbasectid, ybuniqueidxkeysuffix if index is
 *		unique) from indexrel
 */
static Plan *
indexrel_scan_plan2(Relation indexrel)
{
	TupleDesc indexdesc = RelationGetDescr(indexrel);

	List *plan_targetlist = NIL;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	int i = 0;

	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Expr *ybctid_expr = (Expr *) makeVar(INDEX_VAR, YBTupleIdAttributeNumber,
										 attr->atttypid, attr->atttypmod,
										 attr->attcollation, 0);
	target_entry = makeTargetEntry((Expr *) ybctid_expr, i + 1, "", false);
	plan_targetlist = lappend(plan_targetlist, target_entry);
	i++;
	// todo: does the following increase i?
	Expr *expr;
	for (int attno = indexrel->rd_index->indnkeyatts; attno < indexdesc->natts; attno++, i++)
	{
		attr = TupleDescAttr(indexdesc, attno);
		expr = (Expr *) makeVar(INDEX_VAR, attno + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, i + 1, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	if (indexrel->rd_index->indisunique)
	{
		attr = SystemAttributeDefinition(YBIdxBaseTupleIdAttributeNumber);
		expr = (Expr *) makeVar(INDEX_VAR, YBIdxBaseTupleIdAttributeNumber,
								attr->atttypid, attr->atttypmod,
								attr->attcollation, 0);
		target_entry = makeTargetEntry((Expr *) expr, i + 1, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	List *index_cols = NIL;
	for (int j = 0; j < indexdesc->natts; j++)
	{
		attr = TupleDescAttr(indexdesc, j);
		expr = (Expr *) makeVar(INDEX_VAR, j + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, j + 1, "", false);
		index_cols = lappend(index_cols, target_entry);
	}

	/* Index qual */
	/* RHS */
	Bitmapset *params_bms = NULL;
	List *params = NIL;
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	for (int i = 0; i < yb_bnl_batch_size; i++)
	{
		params_bms = bms_add_member(params_bms, i);
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

	ScalarArrayOpExpr *saop = makeNode(ScalarArrayOpExpr);
	saop->opno = ByteaEqualOperator;
	saop->opfuncid = get_opcode(ByteaEqualOperator);
	saop->useOr = true;
	saop->inputcollid = InvalidOid;
	saop->args = list_make2(ybctid_expr, arrexpr);

	IndexOnlyScan *index_scan = makeNode(IndexOnlyScan);
	Plan *plan = &index_scan->scan.plan;
	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	plan->extParam = params_bms;
	plan->allParam = params_bms;
	index_scan->indexqual = list_make1(saop);
	index_scan->scan.scanrelid = 1; /* only one relation is involved */
	index_scan->indexid = RelationGetRelid(indexrel);
	index_scan->indextlist = index_cols;
	return (Plan *) index_scan;
}

/*
 * Generate plan corresponding to:
 *		SELECT (index attributes, ybctid) from baserel where ybctid IN (....)
 *		 AND <partial index predicate, if any>
 * This plan makes the inner subplan of BNL. So this must be an IndexScan, but
 * at the same time this should scan the base relation. To achive this, index
 * scan is done an a dummy index that is on the ybctid column. Under the hood,
 * it works as an index only scan on the base relation. This is similair to how
 * PK index scan works in YB.
 */
static Plan *
baserel_scan_plan2(Relation baserel, Relation indexrel)
{
	TupleDesc indexdesc = RelationGetDescr(indexrel);
	TupleDesc base_desc = RelationGetDescr(baserel);

	Expr *expr;
	const FormData_pg_attribute *attr;
	List *indexprs = NIL;
	ListCell *next_expr = NULL;

	List *indexatts = NIL;
	for (int i = 0; i < indexdesc->natts; i++)
	{
		AttrNumber attnum = indexrel->rd_index->indkey.values[i];
		if (attnum > 0)
		{
			/* regular index attribute */
			attr = TupleDescAttr(base_desc, attnum - 1);
			expr = (Expr *) makeVar(1, attnum, attr->atttypid, attr->atttypmod,
									attr->attcollation, 0);
		}
		else
		{
			/* expression index attribute */
			if (next_expr == NULL)
			{
				/* Fetch expressions in the index */
				Assert(indexprs == NIL);
				indexprs = fetch_index_expressions(indexrel);
				next_expr = list_head(indexprs);
			}
			expr = (Expr *) lfirst(next_expr);
			next_expr = lnext(indexprs, next_expr);
		}
		indexatts = lappend(indexatts, expr);
	}


	int indnkeyatts = indexrel->rd_index->indnkeyatts;
	List *keyatts = NIL;
	for (int i = 0; i < indnkeyatts; i++)
		keyatts = lappend(keyatts, lfirst(list_nth_cell(indexatts, i)));

	ArrayExpr *keyattsexpr = makeNode(ArrayExpr);
	keyattsexpr->array_typeid = ANYARRAYOID;
	keyattsexpr->element_typeid = ANYOID;
	keyattsexpr->multidims = false;
	keyattsexpr->array_collid = InvalidOid;
	keyattsexpr->location = -1;
	keyattsexpr->elements = keyatts;

	Const *indexoidarg = makeConst(OIDOID, 0, InvalidOid, sizeof(Oid),
								   (Datum) indexrel->rd_rel->oid, false, true);

	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *ybctid_expr = makeVar(1, YBTupleIdAttributeNumber, attr->atttypid,
							   attr->atttypmod, attr->attcollation, 0);

	List *args = list_make3(indexoidarg, keyattsexpr, ybctid_expr);

	FuncExpr *funcexpr = makeFuncExpr(F_YB_COMPUTE_YBCTID, BYTEAOID, args,
									  InvalidOid, InvalidOid,
									  COERCE_EXPLICIT_CALL);

	TargetEntry *target_entry;
	List *plan_targetlist = NIL;
	int tlist_resno = 1;
	target_entry = makeTargetEntry((Expr *) funcexpr, tlist_resno, "", false);
	tlist_resno++;
	plan_targetlist = lappend(plan_targetlist, target_entry);

	for (int i = indnkeyatts; i < indexdesc->natts; i++, tlist_resno++)
	{
		expr = lfirst(list_nth_cell(indexatts, i));
		/* Assert that type of index attribute match base relation attribute. */
		Assert(exprType((Node *) expr) ==
			   TupleDescAttr(indexdesc, i)->atttypid);
		target_entry = makeTargetEntry(expr, tlist_resno, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	if (indexrel->rd_index->indisunique)
	{
		target_entry =
			makeTargetEntry((Expr *) ybctid_expr, tlist_resno, "", false);
		plan_targetlist = lappend(plan_targetlist, target_entry);
	}

	/* Partial index predicate */
	List *partial_idx_pred = NIL;
	List *partial_idx_colrefs = NIL;
	bool partial_idx_pushdown = false;
	bool indpred_isnull = false;
	Datum indpred_datum = SysCacheGetAttr(INDEXRELID, indexrel->rd_indextuple,
										  Anum_pg_index_indpred,
										  &indpred_isnull);
	if (!indpred_isnull)
	{
		Expr *indpred = stringToNode(TextDatumGetCString(indpred_datum));
		partial_idx_pushdown = YbCanPushdownExpr(indpred, &partial_idx_colrefs);
		partial_idx_pred = lappend(partial_idx_pred, indpred);
	}

	YbSeqScan *base_scan = makeNode(YbSeqScan);
	Plan *plan = &base_scan->scan.plan;
	plan->targetlist = plan_targetlist;
	plan->lefttree = NULL;
	plan->righttree = NULL;
	plan->qual = !partial_idx_pushdown ? partial_idx_pred : NIL;
	base_scan->scan.scanrelid = 1; /* only one relation is involved */
	base_scan->yb_pushdown.quals = partial_idx_pushdown ? partial_idx_pred :
														  NIL;
	base_scan->yb_pushdown.colrefs =
		partial_idx_pushdown ? partial_idx_colrefs : NIL;
	return (Plan *) base_scan;
}

static Plan *
missing_check_plan(Relation baserel, Relation indexrel)
{
	/*
	 * To check for spurious rows in index relation, we join the index relation
	 * with the base relation on indexrow.ybbasectid == baserow.ybctid. We use
	 * BNL for this purpose. To satisfy the BNL's join condition requirement,
	 * index relation is used as the outer (left) subplan.
	 */

	/* Outer subplan: index relation scan */
	Plan *indexrel_scan = indexrel_scan_plan2(indexrel);
	TupleDesc indexrel_scan_desc = ExecTypeFromTL(indexrel_scan->targetlist);

	/* Inner subplan: base relation scan */
	Plan *baserel_scan = baserel_scan_plan2(baserel, indexrel);

	/*
	 * Join plan targetlist
	 *
	 * Outer subplan tlist: index_attributes, ybbasectid, ybuniqueidxkeysuffix
	 * (if unique) Inner subplan tlist: index_attributes (scanned/computed from
	 * baserel), ybctid
	 *
	 * Join plan tlist: union of the above such that semantically same entries
	 * are next to each other:
	 * (outer_attr1, inner_attr1, outer_attr2, inner_att2 ..*,
	 * ybuniqueidxkeysuffix if unqiue index)
	 *
	 */
	Expr *expr;
	TargetEntry *target_entry;
	const FormData_pg_attribute *attr;
	List *plan_tlist = NIL;
	int i;
	for (i = 0; i < indexrel_scan_desc->natts; i++)
	{
		attr = TupleDescAttr(indexrel_scan_desc, i);
		expr = (Expr *) makeVar(OUTER_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 1, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);

		expr = (Expr *) makeVar(INNER_VAR, i + 1, attr->atttypid,
								attr->atttypmod, attr->attcollation, 0);
		target_entry = makeTargetEntry(expr, 2 * i + 2, "", false);
		plan_tlist = lappend(plan_tlist, target_entry);
	}

	/* Join claue */
	attr = SystemAttributeDefinition(YBTupleIdAttributeNumber);
	Var *join_clause_lhs = makeVar(OUTER_VAR, 1,
								   attr->atttypid, attr->atttypmod,
								   attr->attcollation, 0);
	Var *join_clause_rhs = makeVar(INNER_VAR, 1,
								   attr->atttypid, attr->atttypmod,
								   attr->attcollation, 0);
	OpExpr *join_clause = (OpExpr *) make_opclause(ByteaEqualOperator, BOOLOID,
												   false, /* opretset */
												   (Expr *) join_clause_lhs,
												   (Expr *) join_clause_rhs,
												   InvalidOid, InvalidOid);
	join_clause->opfuncid = get_opcode(ByteaEqualOperator);

	/* NestLoopParam */
	NestLoopParam *nlp = makeNode(NestLoopParam);
	nlp->paramno = 0;
	nlp->paramval = join_clause_lhs;
	nlp->yb_batch_size = yb_bnl_batch_size;

	/* BNL join plan */
	YbBatchedNestLoop *join_plan = makeNode(YbBatchedNestLoop);
	Plan *plan = &join_plan->nl.join.plan;
	plan->targetlist = plan_tlist;
	plan->lefttree = (Plan *) baserel_scan;
	plan->righttree = (Plan *) indexrel_scan;
	join_plan->nl.join.jointype = JOIN_LEFT;
	join_plan->nl.join.inner_unique = true;
	join_plan->nl.join.joinqual = list_make1(join_clause);
	join_plan->nl.nestParams = list_make1(nlp);
	join_plan->first_batch_factor = 1.0;
	join_plan->num_hashClauseInfos = 1;
	join_plan->hashClauseInfos = palloc0(sizeof(YbBNLHashClauseInfo));
	join_plan->hashClauseInfos->hashOp = ByteaEqualOperator;
	join_plan->hashClauseInfos->innerHashAttNo = join_clause_rhs->varattno;
	join_plan->hashClauseInfos->outerParamExpr = (Expr *) join_clause_lhs;
	join_plan->hashClauseInfos->orig_expr = (Expr *) join_clause;
	return (Plan *) join_plan;
}

static void
check_missing_index_rows(Relation baserel, Relation indexrel, EState* estate)
{
	Plan *indexrel2 = missing_check_plan(baserel, indexrel);
	MemoryContext oldctxt = MemoryContextSwitchTo(estate->es_query_cxt);
	PlanState *indexrel2_state = ExecInitNode((Plan *) indexrel2, estate, 0);
	TupleTableSlot* output;
	while ((output = ExecProcNode(indexrel2_state)))
	{
		// todo: why is this required
		if (TTS_EMPTY(output))
			break;
		const char* string = YbTupleTableSlotToString(output);
		elog(INFO, "2nd missing check plan %s", string);
	}
	ExecEndNode(indexrel2_state);
	MemoryContextSwitchTo(oldctxt);
	return;
}

static void
partitioned_index_check(Oid parentindexId)
{
	ListCell *lc;
	foreach (lc, find_inheritance_children(parentindexId, AccessShareLock))
	{
		Oid childindexId = ObjectIdGetDatum(lfirst_oid(lc));
		/* TODO: A new read time can be used for each partition. */
		yb_index_check_internal(childindexId);
	}
}


static void
yb_index_check_internal(Oid indexoid)
{
	Relation indexrel = RelationIdGetRelation(indexoid);

	if (indexrel->rd_rel->relkind != RELKIND_INDEX &&
		indexrel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX)
		elog(ERROR, "Object is not an index");

	Assert(indexrel->rd_index);

	if (indexrel->rd_rel->relam == YBGIN_AM_OID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("this operation is not yet supported for ybgin "
				"indexes")));

	if (indexrel->rd_rel->relam != LSM_AM_OID)
		elog(ERROR,
			 "This operation is not supported for index with access method %d",
			 indexrel->rd_rel->relam);

	if (!indexrel->rd_index->indisvalid)
		elog(ERROR, "Index '%s' is marked invalid",
			 RelationGetRelationName(indexrel));

	/* YB doesn't have separate PK index, hence it is always consistent */
	if (indexrel->rd_index->indisprimary)
	{
		RelationClose(indexrel);
		return;
	}

	if (indexrel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
	{
		RelationClose(indexrel);
		return partitioned_index_check(indexoid);
	}

	Relation baserel = RelationIdGetRelation(indexrel->rd_index->indrelid);

	EState* estate = init_estate(baserel);

	PG_TRY();
	{
		check_spurious_index_rows(baserel, indexrel, estate);
		check_missing_index_rows(baserel, indexrel, estate);
	}
	PG_CATCH();
	{
		if (baserel->rd_index)
			yb_free_dummy_baserel_index(baserel);
		PG_RE_THROW();
	}
	PG_END_TRY();

	cleanup_estate(estate);

	RelationClose(indexrel);
	RelationClose(baserel);
}

Datum
yb_index_check(PG_FUNCTION_ARGS)
{
	Oid indexoid = PG_GETARG_OID(0);
	yb_index_check_internal(indexoid);
	PG_RETURN_VOID();
}

Datum
yb_compute_ybctid(PG_FUNCTION_ARGS)
{
	Oid relid = PG_GETARG_OID(0);
	Relation rel = RelationIdGetRelation(relid);
	TupleDesc desc = RelationGetDescr(rel);
	TupleTableSlot *slot = MakeTupleTableSlot(desc, &TTSOpsVirtual);

	AnyArrayType *array = PG_GETARG_ANY_ARRAY_P(1);
	int ndims = AARR_NDIM(array);
	int *dims = AARR_DIMS(array);
	Assert(ndims == 1);
	int nitems = ArrayGetNItems(ndims, dims);
	Assert(nitems <= desc->natts);
	Assert(nitems > 0);
	array_iter iter;
	array_iter_setup(&iter, array);

	bool null = false;
	for (int i = 0; i < nitems; i++)
	{
		FormData_pg_attribute *att = TupleDescAttr(desc, i);
		slot->tts_values[i] = array_iter_next(&iter, &slot->tts_isnull[i], i,
											  att->attlen, att->attbyval,
											  att->attalign);
		null = null || slot->tts_isnull[i];
	}

	slot->tts_nvalid = nitems;
	slot->tts_flags &= ~TTS_FLAG_EMPTY; /* Not empty */
	slot->tts_tableOid = relid;

	Form_pg_index index = rel->rd_index;
	if (index)
	{
		int nkeyatts = index->indnkeyatts;
		int indisunique = index->indisunique;
		if (nitems != nkeyatts)
			elog(ERROR, "Number of argument mimatched. Expected %d, actual %d",
				 nkeyatts, nitems);
		Datum ybbasetid = PG_GETARG_DATUM(2);
		if (indisunique && !index->indnullsnotdistinct && null)
			slot->ts_ybuniqueidxkeysuffix = ybbasetid;
		else
			slot->ts_ybbasectid = ybbasetid;
	}

	Datum result = YBCComputeYBTupleIdFromSlot(rel, slot);
	ExecDropSingleTupleTableSlot(slot);
	RelationClose(rel);
	return result;
}