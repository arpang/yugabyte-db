/*-------------------------------------------------------------------------
 *
 * pg_yb_notifications.h
 *
 *	  definition of the "YSQL LISTEN/NOTIFY notifications" system catalog (pg_yb_notifications)
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 * src/include/catalog/pg_yb_notifications.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_NOTIFICATIONS_H
#define PG_YB_NOTIFICATIONS_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_notifications_d.h"

/* ----------------
 *		pg_yb_notifications definition.  cpp turns this into
 *		typedef struct FormData_pg_yb_notifications
 * ----------------
 */
CATALOG(pg_yb_notifications,8101,YbNotificationsRelationId) BKI_SHARED_RELATION BKI_ROWTYPE_OID(8102,YbNotificationsRelation_Rowtype_Id) YB_BKI_TSERVER_HOSTED BKI_SCHEMA_MACRO
{
	uuid		sender_node;	/* uuid of node hosting the sender backend */

	int32		sender_pid;		/* pid of the sender backend */

	Oid			dbid;			/* notification's db oid */

	bool		is_listen;		/* whether the record corresponds to LISTEN */

#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	text		data BKI_FORCE_NOT_NULL;
#endif
} FormData_pg_yb_notifications;

/* ----------------
 *		FormData_pg_yb_notifications corresponds to a pointer to a tuple with
 *		the format of pg_yb_notifications relation.
 * ----------------
 */
typedef FormData_pg_yb_notifications * Form_pg_yb_notifications;

DECLARE_UNIQUE_INDEX_PKEY(pg_yb_notifications_pkey, 8103, YbNotificationsPKeyIndexId, on pg_yb_notifications using btree(sender_node uuid_ops HASH, sender_pid int4_ops HASH) num_tablets 3);

#endif							/* PG_YB_NOTIFICATIONS_H */
