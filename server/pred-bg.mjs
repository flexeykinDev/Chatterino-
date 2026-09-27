import WebSocket from "ws";
const ID = "559931613"; // the user's own channel only
const ws = new WebSocket("wss://pubsub-edge.twitch.tv");
const stamp = () => new Date().toTimeString().slice(0, 8);
const seen = new Set();
ws.on("open", () => {
  console.log(stamp(), "listening on predictions-channel-v1." + ID);
  ws.send(JSON.stringify({ type: "LISTEN", nonce: "pred",
    data: { topics: [`predictions-channel-v1.${ID}`] } }));
  setInterval(() => ws.send(JSON.stringify({ type: "PING" })), 60000);
});
ws.on("message", (d) => {
  const m = JSON.parse(d.toString());
  if (m.type !== "MESSAGE") return;
  const body = JSON.parse(m.data.message);
  const key = body.type;
  if (seen.has(key)) return;   // updates repeat on every bet
  seen.add(key);
  console.log(stamp(), "=== type =", key, "===");
  console.log(JSON.stringify(body, null, 1).slice(0, 2600));
});
ws.on("error", e => console.log(stamp(), "error:", e.message));
setTimeout(() => { console.log(stamp(), "types seen:", [...seen].join(", ") || "NONE"); ws.close(); process.exit(0); }, 600000);
