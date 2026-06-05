# StackChan Launch Marketing Video

This directory contains the source and final output for the approved StackChan launch video.

## Final Output

- Upload this: `outputs/stackchan-launch-twitter-v11-h264.mp4`
- Duration: `33s`
- Format: H.264, 1920x1080, 30fps, BT.709 SDR tags

The original HyperFrames HEVC/HDR master was intentionally not kept here. The source is reproducible from `index.html`, and the upload-ready H.264 is the only video artifact worth preserving in the repo.

## Source

- Composition: `index.html`
- Local video-only clips: `launch_assets/` (ignored by git because these are large, reproducible intermediates)
- Generated/trimmed audio: `audio/`
- Screenshots used in scenes: `images/`
- Narration scripts: `scripts/`
- Visual proof frames: `proofs/`

## Current Story Beats

1. Possess your StackChan.
2. Show actual head movement: left, right, up, down.
3. On-device wake word with boosted real audio: `"Hey Stacky"`.
4. Agent remote control and command surface.
5. Personal agent with body, memory, and notes.
6. Boosted real Stacky line: `"I got a servo body and a face and a voice. He'd say that's not enough."`
7. End card: `github.com/mountgram/stack-chan-skill`.

## Key Source Timings

- Head movement source: `../docs/walkThrough.MOV`, around `0:45-0:52`.
- Wake-word source: `../raw_videos/IMG_6381.MOV`, around `0:00-0:02.2`.
- Stacky servo/body line source: `../raw_videos/IMG_6385.MOV`, around `186.6s-191.05s`.

## Rebuild Commands

Run from this directory:

```bash
npx hyperframes lint
npx hyperframes validate
npx hyperframes inspect --samples 18
npx hyperframes render --output outputs/stackchan-launch-twitter-v11.mp4 --quality standard
ffmpeg -hide_banner -y -i outputs/stackchan-launch-twitter-v11.mp4 -vf "scale=1920:1080,format=yuv420p,sidedata=delete:type=MASTERING_DISPLAY_METADATA,sidedata=delete:type=CONTENT_LIGHT_LEVEL,setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" -map_metadata -1 -c:v libx264 -preset medium -crf 18 -color_primaries bt709 -color_trc bt709 -colorspace bt709 -c:a aac -b:a 192k -movflags +faststart outputs/stackchan-launch-twitter-v11-h264.mp4
```

The render step may emit an HDR HEVC master because the iPhone source clips are HLG. The FFmpeg step strips HDR metadata and creates the upload-safe SDR H.264 copy. Do not commit the HDR master or the copied `.mov` clips.

## Visual Language

Follow `../DESIGN.md`: dark workshop console, mono typography, cyan for orientation/status, green for concrete command/action moments, practical panels, and device frames. Do not drift into generic marketing cards or decorative neon unless the site design is explicitly being referenced.

## Cleanup Policy

Keep only:

- The current approved upload video.
- Small assets referenced by `index.html`.
- Local-only copied `.mov` clips in `launch_assets/`, ignored by git.
- Proof frames for the latest version.
- Scripts needed to regenerate current narration.

Do not keep old version renders, experimental scripts, alternate audio takes, or archived rejected compositions in this directory.
