#!/bin/bash

dev=${1:-video0}
res=${2:-1280x720}
fps=${3:-25}

id=$( date +"${dev}_%y%m%d%H%M%S" )

convert -size $res xc:none $id.png
avclapper_avconv -y -f video4linux2 -s $res -i /dev/$dev -filter_complex '[0:v]fps=25[vo]' \
	-map '[vo]' -c:v libx264 -preset ultrafast $id.mp4 -map 0:v -r 8 -f image2 -update 1 $id.png &

sleep 2
echo; echo
echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++"
echo "+                                                     +"
echo "+       Close monitor window to end recording.        +"
echo "+                                                     +"
echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++"
echo; echo; pqiv -w -P off $id.png

fuser -INT -k $id.mp4
wait

rm -f $id.png

