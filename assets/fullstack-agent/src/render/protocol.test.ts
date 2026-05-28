import { expect, test } from "bun:test";
import { blinkAnimation, grumpyScene } from "./scenes";
import { validateRenderAnimation, validateRenderScene } from "./protocol";

test("validates sample render scene", () => {
  expect(validateRenderScene(grumpyScene).sceneId).toBe("stacky.grumpy.v1");
});

test("rejects duplicate node ids", () => {
  expect(() => validateRenderScene({ ...grumpyScene, nodes: [...grumpyScene.nodes, grumpyScene.nodes[0]] })).toThrow("duplicate render node id");
});

test("rejects animation tracks for unknown nodes", () => {
  expect(() => validateRenderAnimation({ ...blinkAnimation, tracks: [{ ...blinkAnimation.tracks[0], target: "missing" }] }, grumpyScene)).toThrow("unknown animation target");
});
