/*-------------------------------------------------------------------------
 *
 * heapam_handler.c
 *	  heap table access method code
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/access/heap/heapahandler.c
 *
 *
 * NOTES
 *	  This files wires up the lower level heapam.c et al routines with the
 *	  tableam abstraction.
 *
 *-------------------------------------------------------------------------
 */

// #include "access/tableam.h"
#include "postgres.h"
#include "access/yb_pg_inherits_scan.h"
#include "access/yb_scan.h"

/* ------------------------------------------------------------------------
 * Slot related callbacks for heap AM
 * ------------------------------------------------------------------------
 */

static const TupleTableSlotOps *
ybcache_slot_callbacks(Relation relation)
{
	return &TTSOpsHeapTuple;
}

/* ------------------------------------------------------------------------
 * Definition of the heap table access method.
 * ------------------------------------------------------------------------
 */

static const TableAmRoutine ybcache_methods = {
	.type = T_TableAmRoutine,

	.slot_callbacks = ybcache_slot_callbacks,
	.scan_begin = yb_pg_inherits_beginscan,
	.scan_end = yb_pg_inherits_end_scan,
	// .scan_rescan = heap_rescan,
	.scan_getnextslot = yb_pg_inherits_get_next,

	// .scan_set_tidrange = heap_set_tidrange,
	// .scan_getnextslot_tidrange = heap_getnextslot_tidrange,

	// .parallelscan_estimate = table_block_parallelscan_estimate,
	// .parallelscan_initialize = table_block_parallelscan_initialize,
	// .parallelscan_reinitialize = table_block_parallelscan_reinitialize,

	// .index_fetch_begin = heapam_index_fetch_begin,
	// .index_fetch_reset = heapam_index_fetch_reset,
	// .index_fetch_end = heapam_index_fetch_end,
	// .index_fetch_tuple = heapam_index_fetch_tuple,

	// .tuple_insert = heapam_tuple_insert,
	// .tuple_insert_speculative = heapam_tuple_insert_speculative,
	// .tuple_complete_speculative = heapam_tuple_complete_speculative,
	// .multi_insert = heap_multi_insert,
	// .tuple_delete = heapam_tuple_delete,
	// .tuple_update = heapam_tuple_update,
	// .tuple_lock = heapam_tuple_lock,

	// .tuple_fetch_row_version = heapam_fetch_row_version,
	// .tuple_get_latest_tid = heap_get_latest_tid,
	// .tuple_tid_valid = heapam_tuple_tid_valid,
	// .tuple_satisfies_snapshot = heapam_tuple_satisfies_snapshot,
	// .index_delete_tuples = heap_index_delete_tuples,

	// .relation_set_new_filenode = heapam_relation_set_new_filenode,
	// .relation_nontransactional_truncate =
	// 	heapam_relation_nontransactional_truncate,
	// .relation_copy_data = heapam_relation_copy_data,
	// .relation_copy_for_cluster = heapam_relation_copy_for_cluster,
	// .relation_vacuum = heap_vacuum_rel,
	// .scan_analyze_next_block = heapam_scan_analyze_next_block,
	// .scan_analyze_next_tuple = heapam_scan_analyze_next_tuple,
	// .index_build_range_scan = heapam_index_build_range_scan,
	// .index_validate_scan = heapam_index_validate_scan,

	// .relation_size = table_block_relation_size,
	// .relation_needs_toast_table = heapam_relation_needs_toast_table,
	// .relation_toast_am = heapam_relation_toast_am,
	// .relation_fetch_toast_slice = heap_fetch_toast_slice,

	// .relation_estimate_size = heapam_estimate_rel_size,

	// .scan_bitmap_next_block = heapam_scan_bitmap_next_block,
	// .scan_bitmap_next_tuple = heapam_scan_bitmap_next_tuple,
	// .scan_sample_next_block = heapam_scan_sample_next_block,
	// .scan_sample_next_tuple = heapam_scan_sample_next_tuple
};

const TableAmRoutine *
GetYbCacheTableAmRoutine(void)
{
	return &ybcache_methods;
}

Datum
yb_cache_tableam_handler(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(&ybcache_methods);
}
