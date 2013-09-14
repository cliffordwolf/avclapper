/*
 *  Find clapboard DTMF markers in an audio track
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

#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <utility>
#include <algorithm>
#include <complex>
#include <vector>
#include <list>

#define AUDIO_RATE 48000
#define AUDIO_FPS 20
#define AUDIO_FRAME (AUDIO_RATE / AUDIO_FPS)

#define MIN_PEAK_RELEVANCE 1000.0
#define MIN_MARK_RELEVANCE  200.0
#define MIN_RELEVANCE        50.0
#define MAX_SEQ_SECONDS 3.5

#if (AUDIO_RATE % AUDIO_FPS) != 0
#error AUDIO_RATE must be divisible by AUDIO_FPS!
#endif

struct event_t {
	char key;
	float frame;
	int span;
	float rel;

	// sort by frame
	bool operator < (const event_t &other) const {
		return frame < other.frame;
	}
};

std::list<event_t> event_queue;

void analyze_event_queue(int current_frame)
{
	// the clapboard app create 200ms long tones (5 tones per second) and no sequence of identical tones
	float tone_window = AUDIO_FPS / 5.0;

	// extract tones of 'tone_window_size' frames size from event_queue
	std::list<event_t> unfiltered_events = event_queue;
	std::vector<event_t> filtered_events;

	while (!unfiltered_events.empty())
	{
		std::list<event_t>::iterator best_event_begin, best_event_end;
		event_t best_event = { 0, 0, 0, 0 };

		auto cursor_start = unfiltered_events.begin();
		for (auto event_it = unfiltered_events.begin(); event_it != unfiltered_events.end(); event_it++)
		{
			event_t ev = *event_it;

			while (cursor_start->frame + tone_window / 2 < ev.frame)
				cursor_start++;

			auto cursor_end = cursor_start;
			while (cursor_end != unfiltered_events.end() && cursor_start->frame + tone_window > cursor_end->frame)
				cursor_end++;

			float sum_frame_wght = 0;
			ev.frame = 0, ev.span = 0, ev.rel = 0;
			for (auto it = cursor_start; it != cursor_end; it++) {
				if (ev.key == it->key) {
					ev.span += it->span;
					ev.frame += it->frame * it->rel;
					sum_frame_wght += it->rel;
				}
				ev.rel += it->rel * (ev.key == it->key ? +1 : -1);
			}
			ev.frame /= sum_frame_wght;

			if (ev.rel > best_event.rel) {
				best_event_begin = cursor_start;
				best_event_end = cursor_end;
				best_event = ev;
			}
		}

		if (best_event.rel < MIN_RELEVANCE)
			break;

		if (best_event.span > 1)
			filtered_events.push_back(best_event);
		unfiltered_events.erase(best_event_begin, best_event_end);
	}

	if (filtered_events.empty() || filtered_events.front().rel < MIN_PEAK_RELEVANCE)
		return;

	// sort and merge
	std::sort(filtered_events.begin(), filtered_events.end());
	for (size_t i = 0; i+1 < filtered_events.size(); i++) {
		if (filtered_events[i].key != filtered_events[i+1].key)
			continue;
		filtered_events[i].span += filtered_events[i+1].span;
		filtered_events[i].rel += filtered_events[i+1].rel;
		filtered_events.erase(filtered_events.begin()+(i--)+1);
	}

	// calculate expected end of sequence using marker keys
	float expected_end_frame = current_frame;
	for (auto &ev : filtered_events)
		if (ev.rel > MIN_MARK_RELEVANCE)
			switch (ev.key) {
				case 'A': expected_end_frame = std::min(expected_end_frame, ev.frame + 12 * tone_window); break;
				case 'B': expected_end_frame = std::min(expected_end_frame, ev.frame + 11 * tone_window); break;
				case '*': expected_end_frame = std::min(expected_end_frame, ev.frame +  6 * tone_window); break;
				case 'C': expected_end_frame = std::min(expected_end_frame, ev.frame +  1 * tone_window); break;
				case 'D': expected_end_frame = std::min(expected_end_frame, ev.frame +  0 * tone_window); break;
				default:  expected_end_frame = std::min(expected_end_frame, ev.frame + 10 * tone_window); break;
			}
	if (expected_end_frame + tone_window > current_frame)
		return;

	// find raster offset
	std::complex<float> raster_phase = 0;
	for (size_t i = 1; i+1 < filtered_events.size(); i++) {
		auto &ev = filtered_events[i];
		float this_theta = 2 * M_PI * fmod(ev.frame, tone_window) / tone_window;
		raster_phase += std::polar(ev.rel, this_theta);
	}
	float raster_offset = tone_window * std::arg(raster_phase) / (2 * M_PI);

	std::string rasterized_string;
	float cursor = (floor(filtered_events.front().frame / tone_window)) * tone_window + raster_offset;
	int event_index = 0;
	while (event_index < int(filtered_events.size())) {
		// printf("cursor=%f, event_index=%d, frame=%f, window=%.2f\n", cursor, event_index, filtered_events[event_index].frame, tone_window);
		if (filtered_events[event_index].frame - tone_window*0.5 <= cursor)
			rasterized_string += filtered_events[event_index++].key;
		else if (event_index > 0 && filtered_events[event_index].frame - filtered_events[event_index-1].frame > tone_window*1.5)
			rasterized_string += ".";
		cursor += tone_window;
	}

	printf("%10.2f %s\n", filtered_events.front().frame / AUDIO_FPS, rasterized_string.c_str());

#if 0
	printf("----\n");
	for (auto &ev : filtered_events)
		printf("%10.1f %c %6.2f\n", ev.frame, ev.key, ev.rel);
	printf("----\n");
#endif

	event_queue.clear();
	return;
}

float goertzel_dft(int16_t *samples, int num_samples, float freq)
{
	// http://en.wikipedia.org/wiki/Goertzel_algorithm#Power_spectrum_terms
	//
	// This is a straight single prec. floating point implementation of
	// the goertzel_algorithm. The other tool (avsync_video) diggst thru the
	// whole video data, so I'm not spending time on optimising this here.

	float omega = 2 * M_PI * freq / num_samples;
	float wr = cos(omega);
	float coeff = 2 * wr;

	float sprev = 0;
	float sprev2 = 0;

	for (int k = 0; k < num_samples; k++) {
		float s = samples[k] + coeff * sprev - sprev2;
		sprev2 = sprev;
		sprev = s;
	}

	float power = sprev2*sprev2 + sprev*sprev - coeff*sprev*sprev2;
	return power;
}

int main(int argc, char **argv)
{
	setlinebuf(stdout);

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <input-file>\n", argv[0]);
		return 1;
	}

	char audio_rate_str[8];
	snprintf(audio_rate_str, 8, "%d", AUDIO_RATE);
	
	setenv("AVSYNC_AUDIO_FILE", argv[1], 1);
	setenv("AVSYNC_AUDIO_RATE", audio_rate_str, 1);

	FILE *f = popen("avconv -v error -i \"$AVSYNC_AUDIO_FILE\" -f s16be -ar $AVSYNC_AUDIO_RATE -ac 1 -", "r");

	if (f == NULL) {
		fprintf(stderr, "Input error (popen)!\n");
		return 1;
	}

	int frame_counter = 0;
	int16_t samples[AUDIO_FRAME];

	printf("%10s %s\n", "AUDIO", argv[1]);

	for (;; frame_counter++)
	{
		if (frame_counter % 1000 == 0) {
			int sec = frame_counter / AUDIO_FPS;
			fprintf(stderr, "[%d:%02d]\r", sec / 60, sec % 60);
		}

		if (fread(samples, sizeof(int16_t)*AUDIO_FRAME, 1, f) != 1) {
			if (frame_counter == 0) {
				fprintf(stderr, "Input error (read)!\n");
				return 1;
			}
			break;
		}
		for (int i = 0; i < AUDIO_FRAME; i++)
			samples[i] = ntohs(samples[i]);

		float dtmf_spectrum[8];
		static float freq_list[8] = { 697, 770, 852, 941, 1209, 1336, 1477, 1633 };
		for (int i = 0; i < 8; i++)
			dtmf_spectrum[i] = goertzel_dft(samples, AUDIO_FRAME, freq_list[i] / AUDIO_FPS);

		int largest_idx[2] = {
			dtmf_spectrum[0] > dtmf_spectrum[1] ? 0 : 1,
			dtmf_spectrum[0] > dtmf_spectrum[1] ? 1 : 0
		};
		for (int i = 2; i < 8; i++) {
			if (dtmf_spectrum[i] > dtmf_spectrum[largest_idx[0]]) {
				largest_idx[1] = largest_idx[0];
				largest_idx[0] = i;
			} else
			if (dtmf_spectrum[i] > dtmf_spectrum[largest_idx[1]])
				largest_idx[1] = i;
		}

		float sum_largest = dtmf_spectrum[largest_idx[0]] + dtmf_spectrum[largest_idx[1]];
		float sum_rest = 0;
		for (int i = 0; i < 8; i++)
			if (i != largest_idx[0] && i != largest_idx[1])
				sum_rest += dtmf_spectrum[i];

		if (sum_largest > sum_rest) {
			int i = largest_idx[0], j = largest_idx[1];
			if (i > j)
				std::swap(i, j);
			char key = 0;
			switch ((i << 4) | j) {
				case 0x04: key = '1'; break;
				case 0x14: key = '4'; break;
				case 0x24: key = '7'; break;
				case 0x34: key = '*'; break;
				case 0x05: key = '2'; break;
				case 0x15: key = '5'; break;
				case 0x25: key = '8'; break;
				case 0x35: key = '0'; break;
				case 0x06: key = '3'; break;
				case 0x16: key = '6'; break;
				case 0x26: key = '9'; break;
				case 0x36: key = '#'; break;
				case 0x07: key = 'A'; break;
				case 0x17: key = 'B'; break;
				case 0x27: key = 'C'; break;
				case 0x37: key = 'D'; break;
			}
			if (key != 0) {
				// printf("%10.2f %c %5.1f\n", float(frame_counter) / AUDIO_FPS, key, sum_largest / sum_rest);
				event_t ev = { key, float(frame_counter), 1, sum_largest / sum_rest };
				event_queue.push_back(ev);
			}
		}

		if (!event_queue.empty() && event_queue.front().frame + MAX_SEQ_SECONDS*AUDIO_FPS < frame_counter) {
			analyze_event_queue(frame_counter);
			if (!event_queue.empty())
				event_queue.pop_front();
		}

#if 0
		// gnuplot> plot for [i=1:8] 'x' using 1:(column(i+1)) with lines title sprintf("%d", i)
		printf("%10.2f", float(frame_counter) / AUDIO_FPS);
		for (int i = 0; i < 8; i++)
			printf(" %e", dtmf_spectrum[i]);
		printf("\n");
#endif
	}

	printf("%10.2f EOF\n", float(frame_counter) / AUDIO_FPS);

	pclose(f);
	return 0;
}

