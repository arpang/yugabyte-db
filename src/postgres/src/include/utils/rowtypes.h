#include "postgres.h"
#include "access/htup_details.h"
#include "fmgr.h"
/*
 * structure to cache metadata needed for record I/O
 */
typedef struct ColumnIOData
{
	Oid		 column_type;
	Oid		 typiofunc;
	Oid		 typioparam;
	bool	 typisvarlena;
	FmgrInfo proc;
} ColumnIOData;

typedef struct RecordIOData
{
	Oid			 record_type;
	int32		 record_typmod;
	int			 ncolumns;
	ColumnIOData columns[FLEXIBLE_ARRAY_MEMBER];
} RecordIOData;

Datum record_out_internal(HeapTupleHeader rec, TupleDesc *tupdesc_ptr,
						  FmgrInfo *flinfo);
