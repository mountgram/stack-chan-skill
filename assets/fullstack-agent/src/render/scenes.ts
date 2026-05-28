import type { RenderAnimation, RenderNode, RenderScene } from "./protocol";

const BG = "#000000";
const EYE = "#eef7ff";
const EYE_SHADOW = "#84a4c4";
const CHEEK = "#ff9eb5";

type Emotion = "neutral" | "happy" | "sad" | "angry" | "grumpy" | "doubt" | "sleepy" | "surprised";

function faceScene(emotion: Emotion, nodes: RenderNode[]): RenderScene {
  return {
    sceneId: `stacky.${emotion}.v1`,
    size: { width: 320, height: 240 },
    background: BG,
    nodes: [
      { id: "face", kind: "group", x: 160, y: 120 },
      ...nodes,
      { id: "leftCheek", parent: "face", kind: "circle", x: -82, y: 38, r: 10, fill: CHEEK, opacity: 0.55 },
      { id: "rightCheek", parent: "face", kind: "circle", x: 82, y: 38, r: 10, fill: CHEEK, opacity: 0.55 },
    ],
  };
}

function eyes(left: Partial<RenderNode>, right: Partial<RenderNode>): RenderNode[] {
  return [
    { id: "leftEye", parent: "face", kind: "ellipse", x: -48, y: -20, rx: 28, ry: 20, fill: EYE, ...left },
    { id: "rightEye", parent: "face", kind: "ellipse", x: 48, y: -20, rx: 28, ry: 20, fill: EYE, ...right },
    { id: "leftPupil", parent: "leftEye", kind: "circle", x: 3, y: 2, r: 7, fill: BG },
    { id: "rightPupil", parent: "rightEye", kind: "circle", x: -3, y: 2, r: 7, fill: BG },
  ] as RenderNode[];
}

function mouth(props: Partial<Extract<RenderNode, { kind: "rect" }>>): RenderNode {
  return { id: "mouth", parent: "face", kind: "rect", x: 0, y: 50, width: 58, height: 8, radius: 4, fill: EYE, ...props };
}

export const neutralScene = faceScene("neutral", [...eyes({}, {}), mouth({ width: 54, height: 8, y: 48 })]);
export const happyScene = faceScene("happy", [
  ...eyes({ y: -26, rotation: -4, scaleY: 0.9 }, { y: -26, rotation: 4, scaleY: 0.9 }),
  { id: "smile", parent: "face", kind: "ellipse", x: 0, y: 46, rx: 34, ry: 18, fill: EYE },
  { id: "smileCut", parent: "face", kind: "rect", x: 0, y: 34, width: 78, height: 22, radius: 11, fill: BG },
  { id: "mouth", parent: "face", kind: "rect", x: 0, y: 54, width: 42, height: 6, radius: 3, fill: CHEEK, opacity: 0.72 },
]);
export const sadScene = faceScene("sad", [
  ...eyes({ rotation: 12, scaleY: 0.7, y: -16 }, { rotation: -12, scaleY: 0.7, y: -16 }),
  { id: "leftBrow", parent: "face", kind: "rect", x: -48, y: -50, width: 46, height: 8, radius: 4, rotation: -16, fill: EYE_SHADOW, opacity: 0.85 },
  { id: "rightBrow", parent: "face", kind: "rect", x: 48, y: -50, width: 46, height: 8, radius: 4, rotation: 16, fill: EYE_SHADOW, opacity: 0.85 },
  mouth({ width: 56, height: 7, y: 54, fill: EYE_SHADOW }),
]);
export const angryScene = faceScene("angry", [
  ...eyes({ rotation: 13, scaleY: 0.8 }, { rotation: -13, scaleY: 0.8 }),
  { id: "leftBrow", parent: "face", kind: "rect", x: -48, y: -48, width: 52, height: 9, radius: 5, rotation: 18, fill: EYE },
  { id: "rightBrow", parent: "face", kind: "rect", x: 48, y: -48, width: 52, height: 9, radius: 5, rotation: -18, fill: EYE },
  mouth({ width: 62, height: 8, y: 52, fill: EYE }),
]);
export const doubtScene = faceScene("doubt", [
  ...eyes({ rotation: -8, x: -52, scaleY: 0.86 }, { rotation: -8, x: 52, scaleY: 0.62 }),
  { id: "leftBrow", parent: "face", kind: "rect", x: -52, y: -52, width: 42, height: 7, radius: 4, rotation: -8, fill: EYE_SHADOW },
  { id: "rightBrow", parent: "face", kind: "rect", x: 52, y: -48, width: 42, height: 7, radius: 4, rotation: 14, fill: EYE_SHADOW },
  mouth({ width: 50, height: 7, y: 52, rotation: -5, fill: EYE_SHADOW }),
]);
export const sleepyScene = faceScene("sleepy", [...eyes({ scaleY: 0.16, y: -12, fill: EYE_SHADOW }, { scaleY: 0.16, y: -12, fill: EYE_SHADOW }), mouth({ width: 44, height: 7, y: 50, fill: EYE_SHADOW })]);
export const surprisedScene = faceScene("surprised", [...eyes({ rx: 30, ry: 28, y: -24 }, { rx: 30, ry: 28, y: -24 }), { id: "mouth", parent: "face", kind: "ellipse", x: 0, y: 52, rx: 18, ry: 24, fill: EYE }]);
export const grumpyScene = faceScene("grumpy", [
  ...eyes({ rotation: 10, scaleY: 0.78 }, { rotation: -10, scaleY: 0.78 }),
  { id: "leftBrow", parent: "face", kind: "rect", x: -48, y: -48, width: 50, height: 8, radius: 4, rotation: 14, fill: EYE_SHADOW },
  { id: "rightBrow", parent: "face", kind: "rect", x: 48, y: -48, width: 50, height: 8, radius: 4, rotation: -14, fill: EYE_SHADOW },
  mouth({ width: 58, height: 8, y: 52, fill: EYE }),
]);

export const blinkAnimation: RenderAnimation = {
  animationId: "blink",
  tracks: [
    { target: "leftEye", property: "scaleY", keyframes: [{ t: 0, value: 1 }, { t: 55, value: 0.01 }, { t: 125, value: 1 }] },
    { target: "rightEye", property: "scaleY", keyframes: [{ t: 0, value: 1 }, { t: 55, value: 0.01 }, { t: 125, value: 1 }] },
    { target: "leftPupil", property: "opacity", keyframes: [{ t: 0, value: 1 }, { t: 35, value: 0 }, { t: 95, value: 0 }, { t: 125, value: 1 }] },
    { target: "rightPupil", property: "opacity", keyframes: [{ t: 0, value: 1 }, { t: 35, value: 0 }, { t: 95, value: 0 }, { t: 125, value: 1 }] },
  ],
};

export const talkingAnimation: RenderAnimation = {
  animationId: "talking-mouth",
  loop: true,
  tracks: [{ target: "mouth", property: "scaleY", keyframes: [{ t: 0, value: 1 }, { t: 120, value: 2.8 }, { t: 240, value: 0.7 }, { t: 360, value: 2.1 }, { t: 480, value: 1 }] }],
};

export const sampleScenes = [grumpyScene, neutralScene, happyScene, sadScene, angryScene, doubtScene, sleepyScene, surprisedScene];
export const sampleAnimations = [blinkAnimation, talkingAnimation];
