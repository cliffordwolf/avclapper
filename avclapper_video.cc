/*
 *  Find clapboard AR markers in a video track
 *
 *  Copyright (C) 2012  Clifford Wolf <clifford@clifford.at>
 *  
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "aruco.h"
#include <algorithm>

#define COLOR_MARK_STEP_MS 500

using namespace cv;
using namespace aruco;

Mat TheInputImage;
MarkerDetector MDetector;
vector<Marker> TheMarkers;
float TheMarkerSize=-1;

VideoCapture TheVideoCapturer;
CameraParameters TheCameraParameters;

vector<pair<float, double>> hue_history;

int idHash(int id)
{
	return (id + 1000 * (id + id/10 + id/100)) % 10000;
}

void hue_history_analyze()
{
	// the color mark is 3 seconds long. each color is displayed for 0.5 seconds. the
	// expected hue values are 0, 1, 2, 3, 4, 5 (modulo 6). only one frame per value is
	// used: the one that best matches the expected time stamp.

	vector<int> best_idx(6);
	vector<double> best_msec(6);

	for (int k = 0; k < 6; k++)
		best_msec.at(k) = hue_history.back().second - (5-k)*COLOR_MARK_STEP_MS;

	size_t start_index = hue_history.size()-1;
	while (start_index > 0) {
		if (hue_history.at(start_index).second < hue_history.back().second - 8*COLOR_MARK_STEP_MS)
			break;
		start_index--;
	}
	if (start_index == 0)
		return;

	for (size_t i = start_index; i < hue_history.size(); i++) {
		for (int k = 0; k < 6; k++) {
			double old_delay = fabs(hue_history.at(best_idx.at(k)).second - best_msec.at(k));
			double this_delay = fabs(hue_history.at(i).second - best_msec.at(k));
			if (this_delay < old_delay)
				best_idx.at(k) = i;
		}
	}

#if 0
	// grep '^>>' output.mrk | cut -c3- | gnuplot -p -e "plot '-' using 1:2 with lines"
	printf(">> %10.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
			(hue_history.back().second / 1000) - 7 * COLOR_MARK_STEP_MS / 1000.0,
			hue_history.at(best_idx.at(0)).first,
			hue_history.at(best_idx.at(1)).first,
			hue_history.at(best_idx.at(2)).first,
			hue_history.at(best_idx.at(3)).first,
			hue_history.at(best_idx.at(4)).first,
			hue_history.at(best_idx.at(5)).first);
#endif

	int fail_counter = 0;
	for (int k = 0; k < 6; k++) {
		double h = hue_history.at(best_idx.at(k)).first - k;
		if (h > 3.0)
			h -= 6.0;
		if (h < -0.5 || 0.5 < h)
			fail_counter++;
	}
	if (fail_counter >= 3)
		return;

	double corrected_start_time = (hue_history.back().second / 1000) - 6 * COLOR_MARK_STEP_MS / 1000.0;
	printf("%10.2f ABABAB\n", corrected_start_time);
	hue_history.clear();
}

int main(int argc, char **argv)
{
	setlinebuf(stdout);

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <video-file>\n", argv[0]);
		return 1;
	}

	TheVideoCapturer.open(argv[1]);

	if (!TheVideoCapturer.isOpened()) {
		fprintf(stderr, "Failed to open video file `%s'.\n", argv[1]);
		return 1;
	}

	double startTime = TheVideoCapturer.get(CV_CAP_PROP_POS_MSEC);
	std::vector<int> lastMakerIds;

	printf("%10s %s\n", "VIDEO", argv[1]);

	for (int frame = 0; TheVideoCapturer.grab(); frame++)
	{
		if (frame % 100 == 0) {
			int sec = TheVideoCapturer.get(CV_CAP_PROP_POS_MSEC) / 1000;
			fprintf(stderr, "[%d:%02d]\r", sec / 60, sec % 60);
		}

		TheVideoCapturer.retrieve(TheInputImage);
		MDetector.detect(TheInputImage, TheMarkers, TheCameraParameters, TheMarkerSize);

		std::vector<int> makerIds;
		for (auto &m : TheMarkers)
			if (m.id >= 10)
				makerIds.push_back(m.id);
		std::sort(makerIds.begin(), makerIds.end());

		if (makerIds.size() != 0 && makerIds.size() != 2)
			continue;

		if (makerIds != lastMakerIds) {
			if (lastMakerIds.size() == 2) {
				double stopTime = TheVideoCapturer.get(CV_CAP_PROP_POS_MSEC);
				double corrected_start_time = (startTime + stopTime) / 2000.0 - 13.0 * 0.2 / 2.0;
				char seq[14];
				snprintf(seq, 14, "AB%04d*%04dCD", idHash(lastMakerIds.at(0)), idHash(lastMakerIds.at(1)));
				for (int i = 1; i < 13; i++)
					if (seq[i-1] == seq[i])
						seq[i] = '#';
				printf("%10.2f %s\n", corrected_start_time, seq);
			}
			lastMakerIds = makerIds;
			startTime = TheVideoCapturer.get(CV_CAP_PROP_POS_MSEC);
		}

		if (TheInputImage.channels() == 3)
		{
			int totalBlue = 0, totalGreen = 0, totalRed = 0;
			uint8_t* pixelPtr = (uint8_t*)TheInputImage.data;
			for(int i = 0; i < TheInputImage.rows; i++)
			for(int j = 0; j < TheInputImage.cols; j++) {
				totalBlue  += *(pixelPtr++); // B
				totalGreen += *(pixelPtr++); // G
				totalRed   += *(pixelPtr++); // R
			}

			float B = float(totalBlue)  / (totalBlue + totalGreen + totalRed);
			float G = float(totalGreen) / (totalBlue + totalGreen + totalRed);
			float R = float(totalRed)   / (totalBlue + totalGreen + totalRed);

			float MAX = std::max(B, std::max(G, R));
			float MIN = std::min(B, std::min(G, R));

			float H = -1;
			if (MAX > 2*MIN) {
				float C = MAX - MIN;
				if (MAX == R)
					H = fmod((G-B)/C, 6);
				if (MAX == G)
					H = (B-R)/C + 2;
				if (MAX == B)
					H = (R-G)/C + 4;
				while (H < 0)
					H += 6;
			}

			hue_history.push_back(pair<float, float>(H, TheVideoCapturer.get(CV_CAP_PROP_POS_MSEC)));
			hue_history_analyze();
		}
	}

	printf("%10.2f EOF\n", TheVideoCapturer.get(CV_CAP_PROP_POS_MSEC) / 1000.0);

	return 0;
}

