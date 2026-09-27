import WebSocket from "ws";
const ID = "559931613"; // the user's own channel only
const ws = new WebSocket("wss://pubsub-edge.twitch.tv");
let frames = 0;
const stamp = () => new Date().toTimeString().slice(0, 8);
ws.on("open", () => {
  console.log(stamp(), "connected, listening on polls." + ID);
  ws.send(JSON.stringify({ type: "LISTEN", nonce: "own",
    data: { topics: [`polls.${ID}`, `predictions-channel-v1.${ID}`] } }));
  setInterval(() => ws.send(JSON.stringify({ type: "PING" })), 60000);
});
ws.on("message", (d) => {
  const m = JSON.parse(d.toString());
  if (m.type === "RESPONSE") console.log(stamp(), "LISTEN accepted, error =", JSON.stringify(m.error));
  else if (m.type === "MESSAGE") { frames++; console.log(stamp(), "=== FRAME on", m.data.topic, "==="); console.log(m.data.message.slice(0, 1500)); }
});
ws.on("error", e => console.log(stamp(), "error:", e.message));
setTimeout(() => { console.log(stamp(), frames ? `${frames} frame(s)` : "NO FRAMES in 180s"); ws.close(); process.exit(0); }, 180000);
