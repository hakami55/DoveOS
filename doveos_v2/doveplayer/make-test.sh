#!/bin/sh
set -eu
Q=/mnt/c/vscode/HPlapdock/analysis/doveplayer-20261008
mkdir -p "$Q"
ffmpeg -hide_banner -loglevel error -nostdin -y -stream_loop 9 -i /mnt/c/vscode/HPlapdock/analysis/gpu-player-followup-20261007/test-1280x720.mp4 -t 120 -c copy -movflags +faststart "$Q/720p60-120sec.mp4"
ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate -show_entries format=duration -of json "$Q/720p60-120sec.mp4" > "$Q/720p60-media.json"
