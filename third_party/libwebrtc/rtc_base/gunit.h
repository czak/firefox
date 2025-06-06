/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_GUNIT_H_
#define RTC_BASE_GUNIT_H_

#include "absl/strings/string_view.h"
#include "rtc_base/fake_clock.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "test/gtest.h"

// Wait until "ex" is true, or "timeout" expires.
#define WAIT(ex, timeout)                                            \
  for (int64_t wait_start = rtc::SystemTimeMillis();                 \
       !(ex) && rtc::SystemTimeMillis() < wait_start + (timeout);) { \
    rtc::Thread::Current()->ProcessMessages(0);                      \
    rtc::Thread::Current()->SleepMs(1);                              \
  }

// This returns the result of the test in res, so that we don't re-evaluate
// the expression in the XXXX_WAIT macros below, since that causes problems
// when the expression is only true the first time you check it.
#define WAIT_(ex, timeout, res)                                        \
  do {                                                                 \
    int64_t wait_start = rtc::SystemTimeMillis();                      \
    res = (ex) && true;                                                \
    while (!res && rtc::SystemTimeMillis() < wait_start + (timeout)) { \
      rtc::Thread::Current()->ProcessMessages(0);                      \
      rtc::Thread::Current()->SleepMs(1);                              \
      res = (ex) && true;                                              \
    }                                                                  \
  } while (0)

// Wait until "ex" is true, or "timeout" expires, using fake clock where
// messages are processed every millisecond.
// TODO(pthatcher): Allow tests to control how many milliseconds to advance.
#define SIMULATED_WAIT(ex, timeout, clock)                     \
  for (int64_t wait_start = rtc::TimeMillis();                 \
       !(ex) && rtc::TimeMillis() < wait_start + (timeout);) { \
    (clock).AdvanceTime(webrtc::TimeDelta::Millis(1));         \
  }

#endif  // RTC_BASE_GUNIT_H_
