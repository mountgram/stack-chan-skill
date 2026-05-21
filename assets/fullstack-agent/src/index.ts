import { config } from "./config";
import { createServer } from "./server/routes";

createServer();

console.log(`Stacky brain listening on http://${config.host}:${config.port}`);
console.log(`Device WS: ws://<this-host>:${config.port}/stacky/device?token=<STACKY_DEVICE_TOKEN>`);
