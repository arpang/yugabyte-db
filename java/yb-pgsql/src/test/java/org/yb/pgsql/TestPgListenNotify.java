// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import com.yugabyte.PGConnection;
import com.yugabyte.PGNotification;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.util.ProcessUtil;
import org.yb.util.Timeouts;
import org.yb.util.YBBackupUtil;

/**
 * Tests for LISTEN/NOTIFY functionality including error handling during
 * background worker startup and basic notification delivery.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgListenNotify extends BasePgListenNotifyTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgListenNotify.class);

  private static final String CHANNEL = "test_channel";
  private static final String PAYLOAD = "test_payload";

  /**
   * LISTEN should fail when cdc_max_virtual_wal_per_tserver is 0 (no virtual
   * WAL can be created for the notifications poller). After resetting the flag,
   * LISTEN and NOTIFY should work normally.
   */
  @Test
  public void testListenFailsDueToVirtualWalLimit() throws Exception {
    setVirtualWalLimit("0");
    try {
      try (Connection conn = getConnectionBuilder().connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
        fail("LISTEN should have failed with cdc_max_virtual_wal_per_tserver=0");
      }
    } catch (SQLException e) {
      LOG.info("Expected LISTEN failure: {}", e.getMessage());
      assertTrue("Error should mention initialization failure",
          e.getMessage().contains("failed to initialize"));
    }

    setVirtualWalLimit("5");
    verifyListenNotifyWorks();
  }

  /**
   * LISTEN should fail when the replication slot cannot be created because the
   * max_replication_slots limit on the master is already reached. After
   * increasing the limit, LISTEN and NOTIFY should work normally.
   */
  @Test
  public void testListenFailsDueToReplicationSlotLimit() throws Exception {
    setMaxReplicationSlots("1");

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SELECT pg_create_logical_replication_slot('blocker_slot', 'pgoutput')");
    }

    try {
      try (Connection conn = getConnectionBuilder().connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
        fail("LISTEN should have failed due to replication slot limit");
      }
    } catch (SQLException e) {
      LOG.info("Expected LISTEN failure: {}", e.getMessage());
      assertTrue("Error should mention replication slots",
          e.getMessage().contains("all replication slots are in use"));
    }

    setMaxReplicationSlots("5");
    verifyListenNotifyWorks();
  }

  /**
   * Validates that LISTEN/NOTIFY continues to work after the only listening backend is killed
   * with SIGKILL.
   *
   * Scenario:
   *   1. Session 1 starts listening on channel 'c', then is killed via SIGKILL.
   *   2. Session 2 starts listening on channel 'c'.
   *   3. Session 3 sends NOTIFY on channel 'c' with a payload.
   *   4. Session 2 receives the notification with the correct payload.
   */
  @Test
  public void testListenNotifyAfterSoloListenerCrash() throws Exception {
    final String channel = "c";
    final String payload = "some payload";

    // Session 1: start listening, then crash it.
    Connection conn1 = getConnectionBuilder().withTServer(0).connect();
    try (Statement stmt1 = conn1.createStatement()) {
      stmt1.execute("LISTEN " + channel);
    }
    int pid1 = getPgBackendPid(conn1);
    LOG.info("Session 1 backend PID: " + pid1);
    ProcessUtil.signalProcess(pid1, "KILL");
    LOG.info("Sent SIGKILL to session 1 (pid " + pid1 + ")");

    // Give the system a moment to clean up the crashed backend.
    Thread.sleep(Timeouts.adjustTimeoutSecForBuildType(2000));

    // Session 2: a new listener that should work despite the previous crash.
    try (Connection conn2 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt2 = conn2.createStatement()) {
      stmt2.execute("LISTEN " + channel);
      LOG.info("Session 2 is now listening on channel '" + channel + "'");

      // Session 3: send a notification.
      try (Connection conn3 = getConnectionBuilder().connect();
           Statement stmt3 = conn3.createStatement()) {
        stmt3.execute("NOTIFY " + channel + ", '" + payload + "'");
        LOG.info("Session 3 sent NOTIFY on channel '" + channel + "'");
      }

      // Poll session 2 for notifications.
      waitForNotification(conn2, channel, payload);
      LOG.info("Session 2 received notification on channel '" + channel + "'");
    }
  }

  /**
   * Validates that LISTEN/NOTIFY continues to work for a surviving listener after another
   * listening backend on the same node is killed with SIGKILL.
   *
   * Scenario:
   *   1. Session 1 starts listening on channel 'c'.
   *   2. Session 2 starts listening on channel 'c' (same node), then is killed via SIGKILL.
   *   3. Session 3 starts listening on channel 'c' (same node).
   *   3. Session 4 sends NOTIFY on channel 'c' with a payload.
   *   4. Session 1 & 3 receives the notification with the correct payload.
   */
  @Test
  public void testListenNotifyAfterPeerListenerCrash() throws Exception {
    final String channel = "c";
    final String payload = "some payload";

    // Session 1: start listening (this session survives).
    try (Connection conn1 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt1 = conn1.createStatement()) {
      stmt1.execute("LISTEN " + channel);
      LOG.info("Session 1 is now listening on channel '" + channel + "'");

      // Session 2: start listening on the same node, then crash it.
      Connection conn2 = getConnectionBuilder().withTServer(0).connect();
      try (Statement stmt2 = conn2.createStatement()) {
        stmt2.execute("LISTEN " + channel);
      }
      int pid2 = getPgBackendPid(conn2);
      LOG.info("Session 2 backend PID: " + pid2);
      ProcessUtil.signalProcess(pid2, "KILL");
      LOG.info("Sent SIGKILL to session 2 (pid " + pid2 + ")");

      // Give the system a moment to clean up the crashed backend.
      Thread.sleep(Timeouts.adjustTimeoutSecForBuildType(2000));

      // Session 3: start new listening session
      try (Connection conn3 = getConnectionBuilder().withTServer(0).connect();
           Statement stmt3 = conn3.createStatement()) {
        stmt3.execute("LISTEN " + channel);

        // Session 4: send a notification.
        try (Connection conn4 = getConnectionBuilder().connect();
            Statement stmt4 = conn4.createStatement()) {
          stmt4.execute("NOTIFY " + channel + ", '" + payload + "'");
          LOG.info("Session 3 sent NOTIFY on channel '" + channel + "'");
        }

        // Poll session 1 & 3 for notifications.
        waitForNotification(conn1, channel, payload);
        waitForNotification(conn3, channel, payload);
      }
    }
  }

  /**
   * Backs up the source database, sends notifications on it, restores to a new database,
   * then verifies that LISTEN/NOTIFY works on the restored database with no spurious
   * notifications from the source.
   */
  @Test
  public void testListenNotifyAfterBackupRestore() throws Exception {
    YBBackupUtil.setTSAddresses(miniCluster.getTabletServers());
    YBBackupUtil.setMasterAddresses(masterAddresses);
    YBBackupUtil.setPostgresContactPoint(miniCluster.getPostgresContactPoints().get(0));
    YBBackupUtil.maybeStartYbControllers(miniCluster);

    final String restoredDb = "restored_db";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)");
      stmt.execute("INSERT INTO t VALUES (1, 'hello'), (2, 'world')");
    }

    // Send notifications on the source database before backup.
    try (Connection notifierConn = getConnectionBuilder().connect();
         Statement stmt = notifierConn.createStatement()) {
      stmt.execute("NOTIFY " + CHANNEL + ", 'before_backup'");
    }

    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", "ysql.yugabyte");
    if (!TestUtils.useYbController()) {
      backupDir = new JSONObject(output).getString("snapshot_url");
    }

    // Send more notifications after backup.
    try (Connection notifierConn = getConnectionBuilder().connect();
         Statement stmt = notifierConn.createStatement()) {
      stmt.execute("NOTIFY " + CHANNEL + ", 'after_backup'");
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql." + restoredDb);

    // Verify data was restored.
    try (Connection restoredConn = getConnectionBuilder().withDatabase(restoredDb).connect();
         Statement stmt = restoredConn.createStatement()) {
      assertQuery(stmt, "SELECT * FROM t ORDER BY id",
          new Row(1, "hello"), new Row(2, "world"));
    }

    // LISTEN on the restored database -- no stale notifications should arrive.
    try (Connection listenerConn = getConnectionBuilder().withDatabase(restoredDb).connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      Thread.sleep(15000);
      PGConnection pgConn = listenerConn.unwrap(PGConnection.class);
      try (Statement pollStmt = listenerConn.createStatement()) {
        pollStmt.execute("SELECT 1");
      }
      PGNotification[] stale = pgConn.getNotifications();
      assertTrue("Expected no spurious notifications after restore",
          stale == null || stale.length == 0);

      // Send a new NOTIFY on the restored database and verify delivery.
      try (Connection notifierConn = getConnectionBuilder().withDatabase(restoredDb).connect();
           Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'after_restore'");
      }

      waitForNotification(listenerConn, CHANNEL, "after_restore");
    }

    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE " + restoredDb);
    }
  }

  /**
   * Verifies NOTIFY behavior inside subtransactions:
   *   - A notification sent inside a committed subtransaction (RELEASE SAVEPOINT)
   *     is delivered to the listener.
   *   - A notification sent inside a rolled-back subtransaction (ROLLBACK TO SAVEPOINT)
   *     is NOT delivered to the listener.
   */
  @Test
  public void testNotifyInSubtransaction() throws Exception {
    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      // Subtransaction that commits: notification should be delivered.
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("SAVEPOINT sp1");
        stmt.execute("NOTIFY " + CHANNEL + ", 'subtxn_commit'");
        stmt.execute("RELEASE SAVEPOINT sp1");
        stmt.execute("COMMIT");
      }
      waitForNotification(listenerConn, CHANNEL, "subtxn_commit");

      // Subtransaction that aborts: notification should NOT be delivered.
      // A sentinel notification is sent after the rollback to confirm the delivery path.
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("SAVEPOINT sp2");
        stmt.execute("NOTIFY " + CHANNEL + ", 'subtxn_abort'");
        stmt.execute("ROLLBACK TO SAVEPOINT sp2");
        stmt.execute("NOTIFY " + CHANNEL + ", 'sentinel'");
        stmt.execute("COMMIT");
      }

      List<PGNotification> received =
          waitForNotification(listenerConn, CHANNEL, "sentinel");
      for (PGNotification n : received) {
        assertTrue("Should not receive notification from rolled-back subtransaction, got: "
            + n.getParameter(), !n.getParameter().equals("subtxn_abort"));
      }
    }
  }

  /**
   * Verifies that a connection receives its own notifications (self-notify).
   */
  @Test
  public void testSelfNotify() throws Exception {
    try (Connection conn = getConnectionBuilder().connect()) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
        stmt.execute("NOTIFY " + CHANNEL + ", 'self_notify'");
      }
      waitForNotification(conn, CHANNEL, "self_notify");
    }
  }

  /**
   * Verifies that LISTEN and NOTIFY issued within the same transaction
   * result in the notification being delivered after COMMIT.
   */
  @Test
  public void testListenAndNotifyInSameTransaction() throws Exception {
    try (Connection conn = getConnectionBuilder().connect()) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("LISTEN " + CHANNEL);
        stmt.execute("NOTIFY " + CHANNEL + ", 'same_txn'");
        stmt.execute("COMMIT");
      }
      waitForNotification(conn, CHANNEL, "same_txn");
    }
  }

  /**
   * Sends a large number of notifications in a single transaction and verifies
   * that all are delivered in order to the listener.
   */
  @Test
  public void testLargeNumberOfNotifiesInTransaction() throws Exception {
    final int numNotifications = 500;

    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("BEGIN");
        for (int i = 0; i < numNotifications; i++) {
          stmt.execute("NOTIFY " + CHANNEL + ", 'msg_" + i + "'");
        }
        stmt.execute("COMMIT");
      }

      List<PGNotification> allNotifs = waitForNotification(
          listenerConn, CHANNEL, "msg_" + (numNotifications - 1));
      assertEquals("Expected all notifications to be delivered",
          numNotifications, allNotifs.size());
      for (int i = 0; i < numNotifications; i++) {
        assertEquals(CHANNEL, allNotifs.get(i).getName());
        assertEquals("msg_" + i, allNotifs.get(i).getParameter());
      }
    }
  }

  /**
   * Verifies that a committed notification is still delivered to an active
   * listener even after the notifier's backend is killed with SIGKILL.
   */
  @Test
  public void testNotifyCommitAndNotifierCrash() throws Exception {
    try (Connection listenerConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      Connection notifierConn = getConnectionBuilder().connect();
      int notifierPid = getPgBackendPid(notifierConn);
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'survive_crash'");
      }
      ProcessUtil.signalProcess(notifierPid, "KILL");
      LOG.info("Crashed notifier backend PID: {}", notifierPid);

      waitForNotification(listenerConn, CHANNEL, "survive_crash");
    }
  }

  /**
   * Polls the given connection for notifications by executing "SELECT 1",
   * collecting all received notifications until one matching the expected
   * channel and payload is found. Returns the complete list of notifications
   * received (including the match).
   */
  private List<PGNotification> waitForNotification(Connection connection,
      String expectedChannel, String expectedPayload) throws Exception {
    List<PGNotification> allNotifications = new ArrayList<>();
    PGConnection pgConn = connection.unwrap(PGConnection.class);
    boolean found = false;
    try (Statement stmt = connection.createStatement()) {
      for (int attempt = 0; attempt < 60 && !found; attempt++) {
        stmt.execute("SELECT 1");
        PGNotification[] notifications = pgConn.getNotifications();
        if (notifications != null) {
          for (PGNotification n : notifications) {
            allNotifications.add(n);
            if (n.getName().equals(expectedChannel)
                && n.getParameter().equals(expectedPayload)) {
              found = true;
            }
          }
        }
        if (!found) Thread.sleep(500);
      }
    }
    assertTrue("Expected to receive notification on channel '" + expectedChannel
        + "' with payload '" + expectedPayload + "'", found);
    return allNotifications;
  }

  /**
   * Verifies that LISTEN succeeds, and that a NOTIFY sent from another
   * connection is delivered to the listener.
   */
  private void verifyListenNotifyWorks() throws Exception {
    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", '" + PAYLOAD + "'");
      }

      waitForNotification(listenerConn, CHANNEL, PAYLOAD);
    }
  }

  private void setVirtualWalLimit(String value) throws Exception {
    Set<HostAndPort> tservers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tserver : tservers) {
      miniCluster.getClient().setFlag(tserver, "cdc_max_virtual_wal_per_tserver", value);
    }
  }

  private void setMaxReplicationSlots(String value) throws Exception {
    for (HostAndPort master : miniCluster.getMasters().keySet()) {
      assertTrue("Failed to set max_replication_slots",
          miniCluster.getClient().setFlag(master, "max_replication_slots", value,
                                          /* force = */ true));
    }
  }
}
