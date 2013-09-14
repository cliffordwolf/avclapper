#!/usr/bin/python

from __future__ import division
from __future__ import print_function

import sys
import numpy as np
import subprocess
from math import sqrt

#################################################################

files = { }
syncs = { }
all_unmatched_tags = set()

class Variable:
    def __init__(self, i, f, t):
        self.i = i # index (col in system matrix)
        self.f = f # file name
        self.t = t # variable type ('o' => offset, 's' => scale)
        files[f].variables[t] = self

class Tag:
    def __init__(self, fn, ts, tx):
        self.filename = fn
        self.timestamp = float(ts)
        self.text = tx
        self.sync = None
        all_unmatched_tags.add(tx)

class File:
    def __init__(self, fn, ty):
        self.filename = fn
        self.filetype = ty
        self.length = 0.0
        self.variables = { }
        self.solution = [0.0, 1.0]
        self.scale = 0.0
        self.tags = [ ]
        self.cuts = [ ]

class Sync:
    def __init__(self, tx):
        self.text = tx
        self.tags = [ ]

#################################################################

def read_input():
    current_file = None
    for line in sys.stdin:
        [ tag, value ] = line.split()
        if tag == 'AUDIO':
            current_file = File(value, tag)
            files[value] = current_file
            continue
        if tag == 'VIDEO':
            current_file = File(value, tag)
            if not value in files:
                files[value] = current_file
            continue
        if tag == 'SCALE':
            current_file.scale = float(value)
            continue
        if value == 'EOF':
            current_file.length = float(tag)
            continue
        if value == 'A#A#A#' or value == 'B#B#B#':
            current_file.cuts.append([ tag, value ])
            continue
        current_file.tags.append(Tag(current_file.filename, tag, value))

#################################################################

def match_tag(tag):
    sync = Sync(tag)
    for filename in files:
        best_tag_in_file = None
        best_tag_wildcards = 100
        for this_tag in files[filename].tags:
            if this_tag.sync != None:
                continue
            if len(tag) != len(this_tag.text):
                continue
            match_wildcards = 0
            match_errors = 0
            for i in range(len(tag)):
                if tag[i] == '.' or this_tag.text[i] == '.':
                    match_wildcards = match_wildcards + 1
                    continue
                if tag[i] != this_tag.text[i]:
                    match_errors = match_errors + 1
                    continue
            if match_errors > 0:
                continue
            if best_tag_wildcards < 100:
                print('Warning: Tags "{}" and "{}" in file "{}" both match tag "{}"!'.format(best_tag_in_file, this_tag, filename, tag), file = sys.stderr)
            if match_wildcards < best_tag_wildcards:
                best_tag_in_file = this_tag
                best_tag_wildcards = match_wildcards
        if best_tag_in_file == None:
            continue
        sync.tags.append(best_tag_in_file)
        best_tag_in_file.sync = sync
        all_unmatched_tags.discard(best_tag_in_file.text)
    syncs[tag] = sync
    all_unmatched_tags.discard(tag)

def match_tags():
    while len(all_unmatched_tags):
        best_tag = all_unmatched_tags.__iter__().next()
        for tag in all_unmatched_tags:
            if best_tag.count('.') > tag.count('.'):
                best_tag = tag
        match_tag(best_tag)

#################################################################

def solve():
    variables = []
    first_file = True
    for fn in files:
        if files[fn].filetype == 'AUDIO':
            if not first_file:
                variables.append(Variable(len(variables), fn, 'o'))
        if files[fn].filetype == 'VIDEO':
            if not first_file:
                variables.append(Variable(len(variables), fn, 'o'))
            variables.append(Variable(len(variables), fn, 's'))
        first_file = False

    eq_index = 0
    for fn in files:
        if files[fn].scale > 0:
            eq_index = eq_index + 1
    for tag in syncs:
        eq_index = eq_index + (len(syncs[tag].tags) * (len(syncs[tag].tags)-1)) // 2
    A = np.zeros((eq_index, len(variables)))
    y = np.zeros((eq_index, 1))

    eq_index = 0
    for fn in files:
        if files[fn].scale > 0:
            A[eq_index][files[fn].variables['s'].i] = 1
            y[eq_index] = files[fn].scale
            eq_index = eq_index + 1
    for tag in syncs:
        for idx_a in range(len(syncs[tag].tags)):
            for idx_b in range(idx_a+1, len(syncs[tag].tags)):

                # create equation: first we collect all variables
                sync = syncs[tag]
                tag_a = syncs[tag].tags[idx_a]
                tag_b = syncs[tag].tags[idx_b]
                file_a = files[tag_a.filename]
                file_b = files[tag_b.filename]

                # add file A to equation
                if 'o' in file_a.variables:
                    A[eq_index][file_a.variables['o'].i] = 1
                if 's' in file_a.variables:
                    A[eq_index][file_a.variables['s'].i] = tag_a.timestamp
                else:
                    y[eq_index] = y[eq_index] - tag_a.timestamp

                # add file B to equation
                if 'o' in file_b.variables:
                    A[eq_index][file_b.variables['o'].i] = -1
                if 's' in file_b.variables:
                    A[eq_index][file_b.variables['s'].i] = -tag_b.timestamp
                else:
                    y[eq_index] = y[eq_index] + tag_b.timestamp

                eq_index = eq_index + 1
    assert eq_index == A.shape[0]

    # solve for least squares error using pseudo-inverse of A
    # A' A x = A' y
    x = np.linalg.solve(np.dot(A.transpose(), A), np.dot(A.transpose(), y))

    # copy results to global data
    min_offset = None
    for i in range(len(variables)):
        if variables[i].t == 'o':
            files[variables[i].f].solution[0] = x[i]
            if min_offset == None or min_offset > x[i]:
                min_offset = x[i]
        if variables[i].t == 's':
            files[variables[i].f].solution[1] = x[i]

    # correct for min_offset = 0
    for f in files:
        files[f].solution[0] = files[f].solution[0] - min_offset

#################################################################

def print_syncs():
    for tag in syncs:
        s = syncs[tag]
        print('\nSync "{}":'.format(s.text))
        for t in s.tags:
            print('%8.2f %s %s' % (t.timestamp, t.text, t.filename))

def print_solution():
    print('\nSync solution:')
    for f in files:
        print('%8.3f %8.6f %s' % (files[f].solution[0], files[f].solution[1], files[f].filename))

def print_deviation():
    print('\nSync deviation:')
    for s in syncs:
        print('%15s:' % s)
        stddev_sum = 0
        stddev_squares = 0
        stddev_num = 0
        for tag in syncs[s].tags:
            ts = files[tag.filename].solution[0] + tag.timestamp*files[tag.filename].solution[1]
            print('%25s %5.2f' % (tag.filename, ts))
            stddev_sum = stddev_sum + ts
            stddev_squares = stddev_squares + ts*ts
            stddev_num = stddev_num + 1
        if stddev_num > 0:
            mean = stddev_sum/stddev_num
            stddev = sqrt(abs(stddev_squares/stddev_num - mean*mean))
            print('%25s %5.2f' % ("mean", mean))
            print('%25s %5.2f' % ("dev", stddev))

def print_avconv():
    print('\nAvconv command lines:')
    outfile_nr = 0
    audio_file = 'null'
    audio_offset = 0.0
    duration = 0.0
    for f in files:
        if files[f].filetype == 'AUDIO':
            audio_file = f
            audio_offset = files[f].solution[0]
        duration = max(duration, files[f].solution[0] + files[f].length*files[f].solution[1])
    print('FPS=25 SR=48000 AVCONV=avclapper_avconv')
    for f in files:
        print('$AVCONV -y', end='')
        print(' -i "%s"' % f, end='')
        res = subprocess.check_output(['sh', '-c', 'mp4info "%s" | sed -re \'/^[0-9]+\s+video\s/ { s/.*\\s([0-9]+x[0-9]+)\\s.*/\\1/; p; }; d;\'' % f]).strip()
        if files[f].filetype == 'AUDIO':
            print(' -filter_complex "color=size=%s[bk]; [0:v]setpts=%f/TB+PTS-STARTPTS[dv]; [0:a]asetpts=%f*SR+PTS-STARTPTS,aformat=s16:$SR:stereo[oa]; [bk][dv]overlay,fps=$FPS[ov]" -map "[ov]" -map "[oa]"' % (res, files[f].solution[0], files[f].solution[0]), end='')
        if files[f].filetype == 'VIDEO':
            print(' -i "%s" -filter_complex "color=size=%s[bk]; [0:v]setpts=%f/TB+(PTS-STARTPTS)*%f[dv]; [1:a]asetpts=%f*SR+PTS-STARTPTS,aformat=s16:$SR:stereo[oa]; [bk][dv]overlay,fps=$FPS[ov]" -map "[ov]" -map "[oa]"' % (audio_file, res, files[f].solution[0], files[f].solution[1], audio_offset), end='')
        print(' -c:v libx264 -c:a libmp3lame -preset ultrafast -t %.2f out%03d.mp4' % (duration, outfile_nr))
        outfile_nr = outfile_nr + 1

#################################################################

read_input()
match_tags()
print_syncs()

solve()
print_solution()
print_deviation()
print_avconv()

