import { z } from "zod";

const colorSchema = z.string().regex(/^#[0-9a-fA-F]{6}$/);

const baseNodeSchema = z.object({
  id: z.string().min(1).max(64),
  parent: z.string().min(1).max(64).optional(),
  x: z.number().finite().optional(),
  y: z.number().finite().optional(),
  scaleX: z.number().finite().optional(),
  scaleY: z.number().finite().optional(),
  rotation: z.number().finite().optional(),
  opacity: z.number().min(0).max(1).optional(),
  visible: z.boolean().optional(),
});

export const renderNodeSchema = z.discriminatedUnion("kind", [
  baseNodeSchema.extend({ kind: z.literal("group") }),
  baseNodeSchema.extend({ kind: z.literal("circle"), r: z.number().positive(), fill: colorSchema.optional(), stroke: colorSchema.optional(), strokeWidth: z.number().min(0).optional() }),
  baseNodeSchema.extend({ kind: z.literal("ellipse"), rx: z.number().positive(), ry: z.number().positive(), fill: colorSchema.optional(), stroke: colorSchema.optional(), strokeWidth: z.number().min(0).optional() }),
  baseNodeSchema.extend({ kind: z.literal("rect"), width: z.number().positive(), height: z.number().positive(), radius: z.number().min(0).optional(), fill: colorSchema.optional(), stroke: colorSchema.optional(), strokeWidth: z.number().min(0).optional() }),
]);

export const renderSceneSchema = z.object({
  sceneId: z.string().min(1).max(96),
  size: z.object({ width: z.literal(320), height: z.literal(240) }),
  background: colorSchema.optional(),
  nodes: z.array(renderNodeSchema).max(64),
});

export const renderKeyframeSchema = z.object({ t: z.number().min(0), value: z.number().finite() });
export const renderTrackSchema = z.object({
  target: z.string().min(1).max(64),
  property: z.enum(["x", "y", "scaleX", "scaleY", "rotation", "opacity"]),
  keyframes: z.array(renderKeyframeSchema).min(1).max(64),
});
export const renderAnimationSchema = z.object({
  animationId: z.string().min(1).max(96),
  loop: z.boolean().optional(),
  yoyo: z.boolean().optional(),
  tracks: z.array(renderTrackSchema).min(1).max(32),
});

export type RenderNode = z.infer<typeof renderNodeSchema>;
export type RenderScene = z.infer<typeof renderSceneSchema>;
export type RenderAnimation = z.infer<typeof renderAnimationSchema>;

export function validateRenderScene(scene: unknown): RenderScene {
  const parsed = renderSceneSchema.parse(scene);
  const ids = new Set<string>();
  for (const node of parsed.nodes) {
    if (ids.has(node.id)) throw new Error(`duplicate render node id: ${node.id}`);
    ids.add(node.id);
  }
  for (const node of parsed.nodes) {
    if (node.parent && !ids.has(node.parent)) throw new Error(`unknown parent '${node.parent}' for node '${node.id}'`);
    if (node.parent === node.id) throw new Error(`node '${node.id}' cannot parent itself`);
  }
  return parsed;
}

export function validateRenderAnimation(animation: unknown, scene?: RenderScene): RenderAnimation {
  const parsed = renderAnimationSchema.parse(animation);
  if (!scene) return parsed;
  const ids = new Set(scene.nodes.map((node) => node.id));
  for (const track of parsed.tracks) {
    if (!ids.has(track.target)) throw new Error(`unknown animation target: ${track.target}`);
  }
  return parsed;
}
