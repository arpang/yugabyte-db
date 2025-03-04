//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include "yb/rocksdb/env.h"
#include "yb/util/io.h"

namespace rocksdb {

class RateLimiter {
 public:
  virtual ~RateLimiter() {}

  // This API allows user to dynamically change rate limiter's bytes per second.
  // Returns true if the actual bytes per second update happened.
  // REQUIRED: bytes_per_second > 0
  virtual yb::Result<bool> SetBytesPerSecond(int64_t bytes_per_second) = 0;

  // Request for token to write bytes. If this request can not be satisfied,
  // the call is blocked. Caller is responsible to make sure
  // bytes <= GetSingleBurstBytes()
  virtual void Request(const int64_t bytes, const yb::IOPriority pri) = 0;

  // Max bytes can be granted in a single burst
  virtual int64_t GetSingleBurstBytes() const = 0;

  // Total bytes that go though rate limiter
  virtual int64_t GetTotalBytesThrough(const yb::IOPriority pri = yb::IOPriority::kTotal) const = 0;

  // Total # of requests that go though rate limiter
  virtual int64_t GetTotalRequests(const yb::IOPriority pri = yb::IOPriority::kTotal) const = 0;

  virtual void EnableLoggingWithDescription(std::string description) = 0;
};

// Create a RateLimiter object, which can be shared among RocksDB instances to
// control write rate of flush and compaction.
// @rate_bytes_per_sec: this is the only parameter you want to set most of the
// time. It controls the total write rate of compaction and flush in bytes per
// second. Currently, RocksDB does not enforce rate limit for anything other
// than flush and compaction, e.g. write to WAL.
// @refill_period_us: this controls how often tokens are refilled. For example,
// when rate_bytes_per_sec is set to 10MB/s and refill_period_us is set to
// 100ms, then 1MB is refilled every 100ms internally. Larger value can lead to
// burstier writes while smaller value introduces more CPU overhead.
// The default should work for most cases.
// @fairness: RateLimiter accepts high-pri requests and low-pri requests.
// A low-pri request is usually blocked in favor of hi-pri request. Currently,
// RocksDB assigns low-pri to request from compaciton and high-pri to request
// from flush. Low-pri requests can get blocked if flush requests come in
// continuouly. This fairness parameter grants low-pri requests permission by
// 1/fairness chance even though high-pri requests exist to avoid starvation.
// You should be good by leaving it at default 10.
extern RateLimiter* NewGenericRateLimiter(
    int64_t rate_bytes_per_sec,
    int64_t refill_period_us = 100 * 1000,
    int32_t fairness = 10);

}  // namespace rocksdb
