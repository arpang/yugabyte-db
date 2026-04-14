// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.pgsql.BasePgListenNotifyTest;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestConnectionManagerListenNotify extends BaseYsqlConnMgr {
  private static final String CHANNEL = "test_channel";
  private static final String PAYLOAD = "test_payload";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    BasePgListenNotifyTest.addListenNotifyFlags(flagMap);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    BasePgListenNotifyTest.addListenNotifyFlags(flagMap);
    return flagMap;
  }

  @Before
  public void waitForNotificationsTable() throws Exception {
    try (Connection conn = getConnectionBuilder().connect()) {
      BasePgListenNotifyTest.waitForNotificationsTableReady(conn, getConnectionBuilder());
    }
  }

  @Test
  public void testListenMakesConnectionSticky() throws Exception {
    try (Connection listenerConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection otherConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect()) {

      try (Statement listenerStmt = listenerConn.createStatement();
           Statement otherStmt = otherConn.createStatement()) {
        assertTrue("Listener should not be sticky before LISTEN",
            verifySessionParameterValue(listenerStmt,
                "ysql_conn_mgr_sticky_object_count", "0"));
        assertTrue("Other connection should not be sticky before LISTEN",
            verifySessionParameterValue(otherStmt,
                "ysql_conn_mgr_sticky_object_count", "0"));

        listenerStmt.execute("LISTEN " + CHANNEL);

        assertTrue("Listener should be sticky after LISTEN",
            verifySessionParameterValue(listenerStmt,
                "ysql_conn_mgr_sticky_object_count", "1"));
        assertTrue("Other connection should not be affected by LISTEN on listener",
            verifySessionParameterValue(otherStmt,
                "ysql_conn_mgr_sticky_object_count", "0"));
      }

      try (Statement stmt = listenerConn.createStatement()) {
        assertConnectionStickyState(stmt, true);
      }
    }
  }

  @Test
  public void testOnlyListenerReceivesNotification() throws Exception {
    try (Connection listenerConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection otherConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect()) {

      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      try (Statement stmt = otherConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", '" + PAYLOAD + "'");
      }

      BasePgListenNotifyTest.waitForNotification(listenerConn, CHANNEL, PAYLOAD);

      BasePgListenNotifyTest.waitAndAssertNoNotifications(otherConn,
          "Non-listener should not receive notifications");
    }
  }
}
