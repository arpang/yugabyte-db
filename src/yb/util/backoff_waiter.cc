// Copyright (c) YugaByte, Inc.
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

#include "yb/util/backoff_waiter.h"
#include "yb/util/flag_tags.h"

#include <algorithm>
#include <string>

#include <glog/logging.h>

using std::string;

DEFINE_test_flag(int32, max_jitter, 0, "Max backoff jitter");
DEFINE_test_flag(
    int32, backoff_start_exponent, 0, "Initial exponent of 2 that backoff starts with");

namespace yb {

template <class Clock>
typename Clock::duration GenericBackoffWaiter<Clock>::DelayForTime(TimePoint now) const {
  Duration max_wait = std::min(deadline_ - now, max_wait_);
  // 1st retry delayed 2^4 of base delays, 2nd 2^5 base delays, etc..
  Duration attempt_delay =
      base_delay_ * (attempt_ >= 29 ? std::numeric_limits<int32_t>::max()
                                    : 1LL << (attempt_ + FLAGS_TEST_backoff_start_exponent));
  Duration jitter = std::chrono::milliseconds(RandomUniformInt(0, FLAGS_TEST_max_jitter));

  LOG(INFO) << "Total delay: " << (attempt_delay + jitter) << " attempt_delay: " << attempt_delay
            << " jitter: " << jitter;
  return std::min(attempt_delay + jitter, max_wait);
}

template class GenericBackoffWaiter<std::chrono::steady_clock>;
template class GenericBackoffWaiter<CoarseMonoClock>;

}  // namespace yb
