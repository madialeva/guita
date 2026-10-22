#pragma once

// Text typed with dead keys in the web client: on a Spanish keyboard, "é" is
// the dead key ´ and then e. The browser sends a keydown "Dead", a keydown
// "e" and an input event with the composed "é"; Qt 6.12 for WebAssembly loses
// the dead key, inserts the "e" and drops that input event. While a Qt text
// field has focus, this keeps the dead key and the key after it from reaching
// Qt, so the composed text comes in through the input event, which Qt inserts.
// Call once at startup (WASM only). Check it, and whether a later Qt still
// needs it, with tests/web/dead-keys.mjs.
void installDeadKeyHandling();
