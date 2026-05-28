import { commandId } from "../device/protocol";
import { registry } from "../device/registry";
import { validateRenderAnimation, validateRenderScene, type RenderAnimation, type RenderScene } from "./protocol";

export function defineScene(scene: RenderScene) {
  const parsed = validateRenderScene(scene);
  return registry.send({ type: "render.defineScene", requestId: commandId("render"), ...parsed } as never);
}

export function setScene(sceneId: string) {
  return registry.send({ type: "render.setScene", requestId: commandId("render"), sceneId } as never);
}

export function animate(animation: RenderAnimation, scene?: RenderScene) {
  const parsed = validateRenderAnimation(animation, scene);
  return registry.send({ type: "render.animate", requestId: commandId("render"), ...parsed } as never);
}

export function reset() {
  return registry.send({ type: "render.reset", requestId: commandId("render") } as never);
}
