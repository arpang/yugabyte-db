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

import java.sql.Connection;
import java.sql.Statement;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBDaemon;

/**
 * Tests for NOTIFY buffering that rely on counting PgClientService Perform RPCs.
 * Shared memory is disabled so Perform operations go through RPC and update the
 * handler_latency histogram.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgListenNotifyBuffering extends BasePgListenNotifyTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgListenNotifyBuffering.class);

  private static final String CHANNEL = "test_channel";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_object_lock_fastpath", "false");
    flagMap.put("pg_client_use_shared_memory", "false");
    return flagMap;
  }

  private long getPerformCountForTServer(int tserverIndex) throws Exception {
    String host = getPgHost(tserverIndex);
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      if (ts.getLocalhostIP().equals(host)) {
        return new Metrics(ts.getLocalhostIP(), ts.getWebPort(), "server")
            .getHistogram("handler_latency_yb_tserver_PgClientService_Perform").totalCount;
      }
    }
    throw new RuntimeException("No tserver found at index " + tserverIndex);
  }

  /**
   * Test that NOTIFYs within a transaction are buffered and flushed in batches.
   */
  @Test
  public void testTxnNotifysAreBuffered() throws Exception {
    final int N = 10;
    final int tserverIndex = 0;

    try (Connection conn = getConnectionBuilder().withTServer(tserverIndex).connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      for (int i = 0; i < N; i++) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'msg_" + i + "'");
      }

      long before = getPerformCountForTServer(tserverIndex);
      stmt.execute("COMMIT");
      long delta = getPerformCountForTServer(tserverIndex) - before;

      LOG.info("NOTIFY flush optimization: {} notifications, {} Perform RPCs", N, delta);
      assertEquals("Perform RPCs for " + N + " notifications", 2, delta);
    }
  }
}
