// |reftest| shell-option(--enable-upsert) skip-if(!Map.prototype.getOrInsertComputed||!xulRuntime.shell) -- upsert is not enabled unconditionally, requires shell-options
// Copyright (C) 2015 the V8 project authors. All rights reserved.
// Copyright (C) 2025 Jonas Haukenes. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-weakmap.prototype.getOrInsert
description: |
  Throws TypeError if `this` doesn't have a [[WeakMapData]] internal slot.
info: |
  WeakMap.prototype.getOrInsert ( key, value )

  ...
  1. Let M be the this value.
  2. Perform ? RequireInternalSlot(M, [[WeakMapData]]).
  ...
features: [Set, upsert]
---*/
assert.throws(TypeError, function() {
  WeakMap.prototype.getOrInsert.call(new Set(), {}, 1);
});

assert.throws(TypeError, function() {
  var map = new WeakMap();
  map.getOrInsert.call(new Set(), {}, 1);
});


reportCompare(0, 0);
