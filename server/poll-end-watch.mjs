import WebSocket from "ws";
const ID = "559931613";
const ws = new WebSocket("wss://pubsub-edge.twitch.tv");
const stamp = () => new Date().toTimeString().slice(0, 8);
const seen = new Set();
ws.on("open", () => {
  ws.send(JSON.stringify({ type: "LISTEN", nonce: "end", data: { topics: [`polls.${ID}`] } }));
  setInterval(() => ws.send(JSON.stringify({ type: "PING" })), 60000);
});
ws.on("message", (d) => {
  const m = JSON.parse(d.toString());
  if (m.type !== "MESSAGE") return;
  const body = JSON.parse(m.data.message);
  // Only print each distinct frame type once: updates repeat on every vote.
  if (seen.has(body.type)) return;
  seen.add(body.type);
  console.log(stamp(), "type =", body.type, "status =", body.data?.poll?.status,
              "ended_at =", body.data?.poll?.ended_at);
});
setTimeout(() => { console.log(stamp(), "types seen:", [...seen].join(", ") || "none"); ws.close(); process.exit(0); }, 340000);
