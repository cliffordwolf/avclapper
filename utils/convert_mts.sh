#!/bin/bash
for x in BDMV/STREAM/*.MTS; do avclapper_avconv -y -i $x -vf yadif -c:v libx264 -c:a libmp3lame -preset ultrafast video_$(basename $x .MTS).mp4; done
