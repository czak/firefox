// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.prototype.until
description: Negative zero, as an extended year, is rejected
features: [Temporal, arrow-function]
---*/

const invalidStrings = [
  "-000000-10-31",
  "-000000-10-31T17:45",
  "-000000-10-31T17:45Z",
  "-000000-10-31T17:45+01:00",
  "-000000-10-31T17:45+00:00[UTC]",
];
const instance = new Temporal.PlainDateTime(2000, 5, 2, 12, 34, 56, 987, 654, 321);
invalidStrings.forEach((str) => {
  const arg = { year: 1976, month: 11, day: 18, calendar: str };
  assert.throws(
    RangeError,
    () => instance.until(arg),
    "reject minus zero as extended year"
  );
});

reportCompare(0, 0);
