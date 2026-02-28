# YB regression: ensure LISTEN/NOTIFY delivers a max-sized payload without crashing
#
# This exercises the bgworker poller path that converts a varlena into an
# AsyncQueueEntry. Before the fix, a header-inclusive length was used with
# VARDATA_ANY(), causing memcpy to overread and an oversized entry length.
#
# We choose a channel name of length 63 (NAMEDATALEN - 1) and a payload length
# of current_setting('block_size') - 64 - 128 - 1 (i.e., NOTIFY_PAYLOAD_MAX_LENGTH - 1),
# so channel + payload + 2 NULs exactly equals the AsyncQueueEntry data capacity.
#
session notifier
step notifymax { SELECT pg_notify(repeat('a', 63),
                                  repeat('x', current_setting('block_size')::int - 64 - 128 - 1)); }
teardown { UNLISTEN *; }

session listener
step llisten { LISTEN "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; }
step lcheck  { SELECT 1 AS x; }
teardown    { UNLISTEN *; }

permutation llisten notifymax lcheck

