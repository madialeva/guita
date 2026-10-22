#include <client/WebKeyboard.h>
#include <emscripten.h>

// A keydown listener on window, in the capture phase, so it runs before Qt's
// own on its window elements. Qt receives text through a hidden <input>
// (inside a shadow root) while one of its text fields has focus: only then
// does it act. It stops events from reaching Qt (stopImmediatePropagation)
// without cancelling them, so the browser still composes and inserts the text
// and sends its input event. Keys without a character of their own (Backspace,
// arrows, Ctrl shortcuts) and keys of an input method composition (isComposing,
// keyCode 229, which Qt handles) pass through.
EM_JS(void, guita_install_dead_key_handling, (), {
    let afterDeadKey = false;
    window.addEventListener('keydown', (event) => {
        if (!(event.composedPath()[0] instanceof HTMLInputElement)) {
            afterDeadKey = false;
            return;
        }
        if (event.key === 'Dead') {
            afterDeadKey = true;
            event.stopImmediatePropagation();
            return;
        }
        if (!afterDeadKey || event.key === 'Shift' || event.key === 'AltGraph' || event.key === 'CapsLock')
            return;
        afterDeadKey = false;
        if (!event.isComposing && event.keyCode !== 229 && Array.from(event.key).length === 1)
            event.stopImmediatePropagation();
    }, true);
});

void installDeadKeyHandling() {
    guita_install_dead_key_handling();
}
