import { config } from "./config";
import { createServer } from "./server/routes";

createServer();

console.log(`Stacky brain listening on http://${config.host}:${config.port}`);
console.log(config.deviceWsUrl ? `Connecting to device WS: ${config.deviceWsUrl}` : "Set STACKY_DEVICE_WS_URL to connect to StackChan");
