// Types into the web client what a Spanish keyboard sends for "Pérez á´´xn",
// dead keys included, through Chrome's DevTools protocol: the same key and
// input events as a real keyboard, which synthetic dead keys (xdotool) do not
// reliably give. Outside ctest, which runs native tests only: it checks the
// dead-key handling of the web client (src/client/WebKeyboard.cpp).
//
//   google-chrome --remote-debugging-port=9222 http://127.0.0.1:8090/
//   (click a text field of the client)
//   node tests/web/dead-keys.mjs [port]
//
// The field must then show "Pérez á´´xn"; without the dead-key handling, Qt
// 6.12 shows "Perez a xn". Needs Node.js 22 or later (built-in WebSocket).

const port = process.argv[2] ?? '9222';
const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const page = targets.find(target => target.type === 'page');
if (!page) {
    console.error(`No page in the Chrome at port ${port}`);
    process.exit(1);
}

const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => socket.addEventListener('open', resolve));
let lastId = 0;
const replies = new Map();
socket.addEventListener('message', message => {
    const reply = JSON.parse(message.data);
    replies.get(reply.id)?.(reply);
});
const send = (method, params) => new Promise(resolve => {
    replies.set(++lastId, resolve);
    socket.send(JSON.stringify({id: lastId, method, params}));
});
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

// A key press; text is what the browser inserts for it (none for a dead key)
async function press(key, code, text) {
    await send('Input.dispatchKeyEvent', {type: text ? 'keyDown' : 'rawKeyDown', key, code, text, unmodifiedText: text});
    await send('Input.dispatchKeyEvent', {type: 'keyUp', key, code});
    await pause(30);
}
// The acute accent key of a Spanish keyboard, beside the P
const deadAcute = () => press('Dead', 'BracketLeft');

await press('P', 'KeyP', 'P');
await deadAcute(); await press('e', 'KeyE', 'é');      // composed
for (const letter of 'rez') await press(letter, `Key${letter.toUpperCase()}`, letter);
await press(' ', 'Space', ' ');
await deadAcute(); await press('a', 'KeyA', 'á');      // composed
await deadAcute(); await press(' ', 'Space', '´');     // the accent alone
await deadAcute(); await press('x', 'KeyX', '´x');     // nothing to compose: both
await press('n', 'KeyN', 'n');
socket.close();

console.log('Typed "Pérez á´´xn": the focused field must show exactly that');
