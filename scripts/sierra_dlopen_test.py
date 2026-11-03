# Run ON THE SIERRA MAC with the built-in Python 2.7:
#   python sierra_dlopen_test.py
# Tries to load each system framework the plugin links, then the plugin itself,
# and prints the exact dyld error for anything that fails.
from __future__ import print_function
import ctypes, os, sys

PLUGIN = os.path.expanduser(
    "~/Library/Audio/Plug-Ins/Components/LoopStationGain.component/Contents/MacOS/LoopStationGain")

FRAMEWORKS = """Accelerate AppKit ApplicationServices AudioToolbox AudioUnit Cocoa CoreAudio
CoreAudioKit CoreFoundation CoreGraphics CoreImage CoreMIDI CoreServices CoreText CoreVideo
DiscRecording Foundation IOKit ImageIO Metal MetalKit QuartzCore Security WebKit""".split()

bad = 0
for fw in FRAMEWORKS:
    path = "/System/Library/Frameworks/%s.framework/%s" % (fw, fw)
    try:
        ctypes.CDLL(path)
        print("ok      ", fw)
    except OSError as e:
        bad += 1
        print("FAILED  ", fw, "\n   ", str(e).replace("\n", "\n    "))

try:
    ctypes.CDLL(PLUGIN)
    print("ok       plugin binary")
except OSError as e:
    bad += 1
    print("FAILED   plugin binary\n   ", str(e).replace("\n", "\n    "))

sys.exit(1 if bad else 0)
