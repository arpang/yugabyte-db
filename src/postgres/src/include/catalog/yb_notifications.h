/*-------------------------------------------------------------------------
 *
 * yb_notifications.h
 *
 *	  definition of the "YSQL LISTEN/NOTIFY notifications" system catalog (yb_notifications)
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 * src/include/catalog/yb_notifications.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_NOTIFICATIONS_H
#define YB_NOTIFICATIONS_H

#include "catalog/genbki.h"
#include "catalog/yb_notifications_d.h"

/* ----------------
 *		yb_notifications definition.  cpp turns this into
 *		typedef struct FormData_yb_notifications
 *      The YbNotificationsRelationId value (8100) is also used in docdb
 *      (kYbNotificationsTableOid in entity_ids.cc) so this value here
 *      should match kYbNotificationsTableOid.
 * ----------------
 */
CATALOG(yb_notifications,8100,YbNotificationsRelationId) BKI_SHARED_RELATION BKI_ROWTYPE_OID(8101,YbNotificationsRelation_Rowtype_Id) YB_BKI_TSERVER_HOSTED BKI_SCHEMA_MACRO
{
	/* TODO: add field code comments */
	uuid		node;

	int32		pid;

	Oid			db;

#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	text		channel;

	text		payload;
#endif

} FormData_yb_notifications;

/* ----------------
 *		FormData_yb_notifications corresponds to a pointer to a tuple with
 *		the format of yb_notifications relation.
 * ----------------
 */
typedef FormData_yb_notifications *Form_yb_notifications;

DECLARE_UNIQUE_INDEX_PKEY(yb_notifications_pkey, 8102, YbNotificationsPKeyIndexId, on yb_notifications using btree(node uuid_ops HASH, pid int4_ops HASH));

#endif							/* YB_NOTIFICATIONS_H */
