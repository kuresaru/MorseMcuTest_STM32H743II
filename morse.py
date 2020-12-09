#!/usr/bin/python3
# encoding: utf8

import sys
import wave
import numpy as np
import struct
import matplotlib.pyplot as plt

rate = 44100
freq = 2000
startfreq = 1000
ticklen = 1
vol = 30000
speed = 0.05

morsetable = {
    'A': '.- ',
    '/': '.-.- ', # ä
    'B': '-... ',
    'C': '-.-. ',
    '=': '---- ', # ch
    'D': '-.. ',
    'E': '. ',
    'F': '..-. ',
    'G': '--. ',
    'H': '.... ',
    'I': '.. ',
    'J': '.--- ',
    'K': '-.- ',
    'L': '.-.. ',
    'M': '-- ',
    'N': '-. ',
    'O': '--- ',
    '.': '---. ', # ö
    'P': '.--. ',
    'Q': '--.- ',
    'R': '.-. ',
    'S': '... ',
    'T': '- ',
    'U': '..- ',
    '-': '..-- ', # ü
    'V': '...- ',
    'W': '.-- ',
    'X': '-..- ',
    'Y': '-.-- ',
    'Z': '--.. ',
    '1': '.---- ',
    '2': '..--- ',
    '3': '...-- ',
    '4': '....- ',
    '5': '..... ',
    '6': '-.... ',
    '7': '--... ',
    '8': '---.. ',
    '9': '----. ',
    '0': '----- ',
}


def gennone(len):
    y = np.linspace(0, 0, int(len * rate * speed))
    return y


def gensine(len):
    x = np.linspace(0, len * speed, int(len * rate * speed))
    y = np.sin(2 * np.pi * freq * x) * vol
    return y


def out(wf, val):
    if val not in ['.', '-', ' ']:
        return
    if '.' == val:
        y = gensine(1)
    if '-' == val:
        y = gensine(3)
    if ' ' == val:
        y = gennone(2)
    for i in y:
        wf.writeframesraw(struct.pack('<h', int(i)))
    for i in gennone(1):
        wf.writeframesraw(struct.pack('<h', int(i)))


def write(wf, morse):
    for ch in morse:
        out(wf, ch)


wf = wave.open('test.wav', 'wb')
wf.setnchannels(1)
wf.setframerate(rate)
wf.setsampwidth(2)

x = np.linspace(0, 0.5, int(0.5 * rate))
y = np.sin(2 * np.pi * startfreq * x) * vol
for i in y:
    wf.writeframesraw(struct.pack('<h', int(i)))

for ch in sys.stdin.read():
    ch = ch.upper()
    if ch in morsetable:
        write(wf, morsetable[ch])
        sys.stdout.write(morsetable[ch])
print('')

wf.close()
