/*-------------------------------------------------------------------------
 *
 * relscan.h
 *	  POSTGRES relation scan descriptor definitions.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/relscan.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef RELSCAN_H
#define RELSCAN_H

#include "access/htup_details.h"
#include "access/itup.h"
#include "port/atomics.h"
#include "storage/buf.h"
#include "storage/spin.h"
#include "utils/relcache.h"

/* Yugabyte includes */
#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"
// #include "access/yb_scan.h"

struct ParallelTableScanDescData;

/*
 * SCAN PLAN - Two structures.
 * - "struct YbScanPlanData" contains variables that are used during preparing statement.
 * - "struct YbScanDescData" contains variables that are used thru out the life of the statement.
 *
 * YugaByte ScanPlan has two different lists.
 *   Binding list:
 *   - This list is used to receive the user-given data for key columns (WHERE clause).
 *   - "sk_attno" is the INDEX's attnum for the key columns that are used to query data.
 *     In YugaByte, primary index attnum is the same as column attnum in the UserTable.
 *   - "bind_desc" is the description of the index columns (count, types, ...).
 *   - The bind lists don't need to be included in the description as they are only needed
 *     during setup.
 *   Target list:
 *   - This list identifies the user-wanted data to be fetched from the database.
 *   - "target_desc" contains the description of the target columns (count, types, ...).
 *   - The target fields are used for double-checking key values after selecting data
 *     from database. It should be removed once we no longer need to double-check the key values.
 */
typedef struct YbScanDescData
{
#define YB_MAX_SCAN_KEYS (INDEX_MAX_KEYS * 2) /* A pair of lower/upper bounds per column max */

	// /* Base of a scan descriptor - Currently it is used either by postgres::heap or Yugabyte.
	//  * It contains basic information that defines a scan.
	//  * - Relation: Which table to scan.
	//  * - Keys: Scan conditions.
	//  *   In YB ScanKey could be one of two types:
	//  *   o key for regular column
	//  *   o key which represents the yb_hash_code function.
	//  *   The keys array holds keys of both types.
	//  *   All regular keys go before keys for yb_hash_code.
	//  *   Keys in range [0, nkeys) are regular keys.
	//  *   Keys in range [nkeys, nkeys + nhash_keys) are keys for yb_hash_code
	//  *   Such separation allows to process regular and non-regular keys independently.
	//  */
	// TableScanDescData rs_base;

	/* The handle for the internal YB Select statement. */
	YBCPgStatement handle;
	bool is_exec_done;

	Relation relation;
	/* Secondary index used in this scan. */
	Relation index;

	/*
	 * In YB ScanKey could be one of two types:
	 *  - key for regular column
	 *  - key which represents the yb_hash_code function.
	 * The keys array holds keys of both types.
	 * All regular keys go before keys for yb_hash_code.
	 * Keys in range [0, nkeys) are regular keys.
	 * Keys in range [nkeys, nkeys + nhash_keys) are keys for yb_hash_code
	 * Such separation allows to process regular and non-regular keys independently.
	 */
	/*
	 * Array of keys that are reordered to regular keys first then yb_hash_code().
	 * Size and contents are the same as rs_keys in different order.
	 */
	ScanKey *keys;
	/* number of regular keys */
	int nkeys;
	/* number of keys which represents the yb_hash_code function */
	int nhash_keys;

	/* True if all the conditions for this index were bound to pggate. */
	bool is_full_cond_bound;

	/* Destination for queried data from Yugabyte database */
	TupleDesc target_desc;
	AttrNumber target_key_attnums[YB_MAX_SCAN_KEYS];

	/* Kept query-plan control to pass it to PgGate during preparation */
	YBCPgPrepareParameters prepare_params;

	/*
	 * Kept execution control to pass it to PgGate.
	 * - When YBC-index-scan layer is called by Postgres IndexScan functions, it will read the
	 *   "yb_exec_params" from Postgres IndexScan and kept the info in this attribute.
	 *
	 * - YBC-index-scan in-turn will passes this attribute to PgGate to control the index-scan
	 *   execution in YB tablet server.
	 */
	YBCPgExecParameters *exec_params;

	/*
	 * Flag used for bailing out from scan early. Currently used to bail out
	 * from scans where one of the bind conditions is:
	 *   - A comparison operator with null, e.g.: c = null, etc.
	 *   - A search array and is empty.
	 *     Consider an example query,
	 *       select c1,c2 from test
	 *       where c1 = XYZ AND c2 = ANY(ARRAY[]::integer[]);
	 *     The second bind condition c2 = ANY(ARRAY[]::integer[]) will never be
	 *     satisfied.
	 * Hence when, such condition is detected, we bail out from creating and
	 * sending a request to docDB.
	 */
	bool quit_scan;
} YbScanDescData;

// TODO(neil) Reorganize code so that description can be placed at appropriate places.
typedef struct YbScanDescData *YbScanDesc;

/*
 * Generic descriptor for table scans. This is the base-class for table scans,
 * which needs to be embedded in the scans of individual AMs.
 */
typedef struct TableScanDescData
{
	/* scan parameters */
	Relation	rs_rd;			/* heap relation descriptor */
	struct SnapshotData *rs_snapshot;	/* snapshot to see */
	int			rs_nkeys;		/* number of scan keys */
	struct ScanKeyData *rs_key; /* array of scan key descriptors */

	/* Range of ItemPointers for table_scan_getnextslot_tidrange() to scan. */
	ItemPointerData rs_mintid;
	ItemPointerData rs_maxtid;

	/*
	 * Information about type and behaviour of the scan, a bitmask of members
	 * of the ScanOptions enum (see tableam.h).
	 */
	uint32		rs_flags;

	struct ParallelTableScanDescData *rs_parallel;	/* parallel scan
													 * information */
	YbScanDesc	ybscan;			/* only valid in yb-scan case */
} TableScanDescData;
typedef struct TableScanDescData *TableScanDesc;

/*
 * Shared state for parallel table scan.
 *
 * Each backend participating in a parallel table scan has its own
 * TableScanDesc in backend-private memory, and those objects all contain a
 * pointer to this structure.  The information here must be sufficient to
 * properly initialize each new TableScanDesc as workers join the scan, and it
 * must act as a information what to scan for those workers.
 */
typedef struct ParallelTableScanDescData
{
	Oid			phs_relid;		/* OID of relation to scan */
	bool		phs_syncscan;	/* report location to syncscan logic? */
	bool		phs_snapshot_any;	/* SnapshotAny, not phs_snapshot_data? */
	Size		phs_snapshot_off;	/* data for snapshot */
} ParallelTableScanDescData;
typedef struct ParallelTableScanDescData *ParallelTableScanDesc;

/*
 * Shared state for parallel table scans, for block oriented storage.
 */
typedef struct ParallelBlockTableScanDescData
{
	ParallelTableScanDescData base;

	BlockNumber phs_nblocks;	/* # blocks in relation at start of scan */
	slock_t		phs_mutex;		/* mutual exclusion for setting startblock */
	BlockNumber phs_startblock; /* starting block number */
	pg_atomic_uint64 phs_nallocated;	/* number of blocks allocated to
										 * workers so far. */
}			ParallelBlockTableScanDescData;
typedef struct ParallelBlockTableScanDescData *ParallelBlockTableScanDesc;

/*
 * Per backend state for parallel table scan, for block-oriented storage.
 */
typedef struct ParallelBlockTableScanWorkerData
{
	uint64		phsw_nallocated;	/* Current # of blocks into the scan */
	uint32		phsw_chunk_remaining;	/* # blocks left in this chunk */
	uint32		phsw_chunk_size;	/* The number of blocks to allocate in
									 * each I/O chunk for the scan */
} ParallelBlockTableScanWorkerData;
typedef struct ParallelBlockTableScanWorkerData *ParallelBlockTableScanWorker;

/*
 * Base class for fetches from a table via an index. This is the base-class
 * for such scans, which needs to be embedded in the respective struct for
 * individual AMs.
 */
typedef struct IndexFetchTableData
{
	Relation	rel;
} IndexFetchTableData;

/*
 * We use the same IndexScanDescData structure for both amgettuple-based
 * and amgetbitmap-based index scans.  Some fields are only relevant in
 * amgettuple-based scans.
 */
typedef struct IndexScanDescData
{
	/* scan parameters */
	Relation	heapRelation;	/* heap relation descriptor, or NULL */
	Relation	indexRelation;	/* index relation descriptor */
	struct SnapshotData *xs_snapshot;	/* snapshot to see */
	int			numberOfKeys;	/* number of index qualifier conditions */
	int			numberOfOrderBys;	/* number of ordering operators */
	struct ScanKeyData *keyData;	/* array of index qualifier descriptors */
	struct ScanKeyData *orderByData;	/* array of ordering op descriptors */
	bool		xs_want_itup;	/* caller requests index tuples */
	bool		xs_temp_snap;	/* unregister snapshot at scan end? */

	/* signaling to index AM about killing index tuples */
	bool		kill_prior_tuple;	/* last-returned tuple is dead */
	bool		ignore_killed_tuples;	/* do not return killed entries */
	bool		xactStartedInRecovery;	/* prevents killing/seeing killed
										 * tuples */

	/* index access method's private state */
	void	   *opaque;			/* access-method-specific info */

	/*
	 * In an index-only scan, a successful amgettuple call must fill either
	 * xs_itup (and xs_itupdesc) or xs_hitup (and xs_hitupdesc) to provide the
	 * data returned by the scan.  It can fill both, in which case the heap
	 * format will be used.
	 */
	IndexTuple	xs_itup;		/* index tuple returned by AM */
	struct TupleDescData *xs_itupdesc;	/* rowtype descriptor of xs_itup */
	HeapTuple	xs_hitup;		/* index data returned by AM, as HeapTuple */
	struct TupleDescData *xs_hitupdesc; /* rowtype descriptor of xs_hitup */

	/*
	 * Result from Yugabyte.
	 * - Note that Postgres keeps the result in field "ItemPointerData xs_heaptid;".
	 * - This field now also contains the returned value from Yugabyte including ybctid.
	 * - It is called "YbItemPointerData yb_itemptr;"
	 */
	ItemPointerData xs_heaptid; /* result */
	bool		xs_heap_continue;	/* T if must keep walking, potential
									 * further results */
	IndexFetchTableData *xs_heapfetch;

	bool		xs_recheck;		/* T means scan keys must be rechecked */

	/*
	 * When fetching with an ordering operator, the values of the ORDER BY
	 * expressions of the last returned tuple, according to the index.  If
	 * xs_recheckorderby is true, these need to be rechecked just like the
	 * scan keys, and the values returned here are a lower-bound on the actual
	 * values.
	 */
	Datum	   *xs_orderbyvals;
	bool	   *xs_orderbynulls;
	bool		xs_recheckorderby;

	/* parallel index scan information, in shared memory */
	struct ParallelIndexScanDescData *parallel_scan;

	/* During execution, Postgres will push down hints to YugaByte for performance purpose.
	 * (currently, only LIMIT values are being pushed down). All these execution information will
	 * kept in "yb_exec_params".
	 *
	 * - Generally, "yb_exec_params" is kept in execution-state. As Postgres executor traverses and
	 *   excutes the nodes, it passes along the execution state. Necessary information (such as
	 *   LIMIT values) will be collected and written to "yb_exec_params" in EState.
	 *
	 * - However, IndexScan execution doesn't use Postgres's node execution infrastructure. Neither
	 *   execution plan nor execution state is passed to IndexScan operators. As a result,
	 *   "yb_exec_params" is kept in "IndexScanDescData" to avoid passing EState to a lot of
	 *   IndexScan functions.
	 *
	 * - Postgres IndexScan function will call and pass "yb_exec_params" to PgGate to control the
	 *   index-scan execution in YugaByte.
	 */
	YBCPgExecParameters *yb_exec_params;

	/*
	 * yb_scan_plan stores postgres scan plan for current index scan.
	 * This information is used to determine target columns that must be read from DocDB
	 * and columns which can be omitted.
	 * TODO: Calculate set of required YB targets on plan stage and use it here
	 *       instead of scan plan. In addition to code speedup this approach will allow to
	 *       remove scan plan from IndexScanDescData structure. Native postgres code doesn't
	 *       have plan information in scan state structures.
	 */
	Scan *yb_scan_plan;
	PushdownExprs *yb_rel_pushdown;
	PushdownExprs *yb_idx_pushdown;

	/*
	 * Result from Yugabyte.
	 * - This field contains the returned value from Yugabyte including ybctid.
	 * - Note that Postgres keeps the result in field "ItemPointerData xs_heaptid;".
	 */
	YbItemPointerData yb_dataip;
}			IndexScanDescData;

/* Generic structure for parallel scans */
typedef struct ParallelIndexScanDescData
{
	Oid			ps_relid;
	Oid			ps_indexid;
	Size		ps_offset;		/* Offset in bytes of am specific structure */
	char		ps_snapshot_data[FLEXIBLE_ARRAY_MEMBER];
}			ParallelIndexScanDescData;

struct TupleTableSlot;

/* Struct for storage-or-index scans of system tables */
typedef struct SysScanDescData
{
	Relation	heap_rel;		/* catalog being scanned */
	Relation	irel;			/* NULL if doing heap or yb scan */
	struct TableScanDescData *scan; /* only valid in storage-scan case */
	struct IndexScanDescData *iscan;	/* only valid in index-scan case */
	struct SnapshotData *snapshot;	/* snapshot to unregister at end of scan */
	struct TupleTableSlot *slot;
	YbScanDesc	ybscan;			/* only valid in yb-scan case */
}			SysScanDescData;

#endif							/* RELSCAN_H */
