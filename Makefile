
CXX = clang
CXXFLAGS = -std=c++11 -Wall -Wextra -Os -ggdb
LDLIBS = -lstdc++ -lm

all: avclapper_video avclapper_audio avclapper_analyze avclapper_record avclapper_avconv

install: all
	install avclapper_video /usr/local/bin/
	install avclapper_audio /usr/local/bin/
	install avclapper_analyze /usr/local/bin/
	install avclapper_record /usr/local/bin/
	install avclapper_avconv /usr/local/bin/

avclapper_video: LDLIBS += $(shell pkg-config opencv --libs)
avclapper_video: aruco-1.2.4/src/libaruco.a avclapper_video.o
avclapper_video.o: CXXFLAGS += -Iaruco-1.2.4/src/
avclapper_video.o: aruco-1.2.4/src/libaruco.a

avclapper_audio: avclapper_audio.o

avclapper_analyze: avclapper_analyze.py
	install avclapper_analyze.py avclapper_analyze

avclapper_record: avclapper_record.sh
	install avclapper_record.sh avclapper_record

markers.js: aruco-1.2.4/src/libaruco.a genmarkers.py
	python genmarkers.py > markers.js

aruco-1.2.4/src/libaruco.a:
	rm -rf aruco-1.2.4 && tar xvzf aruco-1.2.4.tgz && patch -p0 < aruco-hotfixes.patch
	cd aruco-1.2.4 && cmake -DBUILD_SHARED_LIBS=OFF . && make

avclapper_avconv:
	rm -rf libav-HEAD-aca2510 && tar xvzf libav-HEAD-aca2510.tar.gz
	cd libav-HEAD-aca2510 && ./configure --enable-libx264 --enable-libpulse --enable-gpl --enable-libmp3lame && make
	cp libav-HEAD-aca2510/avconv avclapper_avconv

clean:
	rm -rf aruco-1.2.4 libav-HEAD-aca2510 avclapper_video avclapper_audio avclapper_analyze avclapper_record markers.js *.o

