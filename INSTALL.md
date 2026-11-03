# Installing and using LoopStation on macOS 10.12 / Logic Pro X 10.2.3

LoopStation is a **MIDI-controlled effect** (Audio Unit type `aumf`). It loops the audio
it receives, and you control it with notes or CCs from a MIDI keyboard.

## 1. Download the build

1. Open the repository on GitHub, click the **Actions** tab, then **Build AU (macOS 10.12, x86_64)**.
2. Open the most recent run with a green check mark.
3. Under **Artifacts** at the bottom of the page, click **LoopStation-AU-x86_64**.
   You must be signed in to GitHub. Artifacts are deleted after 90 days.
4. That download is a zip with another zip inside it: `LoopStation-AU-macOS10.12-x86_64.zip`.
   Double-click both. (Safari may unzip the outer one by itself.) You end up with
   **`LoopStation.component`**.

> If GitHub doesn't work well in the browser on the Sierra Mac, download the zip on
> another computer and copy it over with a USB stick or AirDrop. Copy the **zip**, not
> the unzipped bundle, so nothing inside it gets changed on the way.

The artifact also contains `sierra_dlopen_test.py` (see Troubleshooting), plus
`compat-check.txt`, `auval.txt` and `BUILD_INFO.txt`, which show what was built and checked.

## 2. Copy it into the Components folder

Quit Logic first. Then, assuming the zip was unzipped into Downloads:

```sh
mkdir -p ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/Components/LoopStation.component   # old version, if any
cp -R ~/Downloads/LoopStation.component ~/Library/Audio/Plug-Ins/Components/
```

(In Finder you can instead use **Go ▸ Go to Folder…** (⇧⌘G), paste
`~/Library/Audio/Plug-Ins/Components/`, and drag the `.component` into it.)

The step-1 test plugin `LoopStationGain.component` is a separate plugin. You can delete
it when you don't need it any more:
`rm -rf ~/Library/Audio/Plug-Ins/Components/LoopStationGain.component`

## 3. Remove the quarantine flag

Files downloaded from the internet get a quarantine flag, and that can stop the
unsigned plugin from loading:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/LoopStation.component
```

## 4. Check that it loads with `auval`

```sh
killall -9 AudioComponentRegistrar        # "No matching processes" is fine
auval -a | grep -i loopstation            # should list: aumf Lsl1 Lsau - LoopStation: LoopStation
auval -v aumf Lsl1 Lsau
```

The last line must be **`AU VALIDATION SUCCEEDED.`** If it isn't, see Troubleshooting.

## 5. Set it up in Logic

MIDI-controlled effects go in the **instrument slot** of a software instrument track.
The audio to loop comes in through the plugin's **Side Chain** menu.

1. Create a **Software Instrument** track. Your keyboard's MIDI goes to the selected track.
2. Click the track's **Instrument** slot and choose
   **AU MIDI-controlled Effects ▸ LoopStation ▸ LoopStation**.
3. In the header of the plugin window, open the **Side Chain** menu and choose the audio
   source:
   - **Input 1** (or whichever input your mic or instrument is on) to loop live audio, or
   - an audio track, to loop what that track plays.
4. Keep the instrument track **selected** (or record-armed), so that MIDI from your
   keyboard reaches it.

> If **AU MIDI-controlled Effects** isn't in the instrument slot menu, or the plugin
> doesn't receive any audio from the side chain, tell me exactly what you see. I don't
> have a Logic 10.2.3 to try it on, and the fix depends on how that version routes audio.

## 6. Controls

| Action | Default key | What it does |
|---|---|---|
| **Rec / Dub** | **C1** (note 36) | Empty: start recording. Recording: finish and play. Playing: start overdub. Overdubbing: back to playing. Stopped: play from the start and overdub. |
| **Play / Stop** | **D1** (note 38) | Playing: stop (fades out, rewinds to the start). Stopped: play from the start. Recording: finish and stay stopped. |
| **Clear** | **E1** (note 40) | Fades out and deletes the loop. |

Notes and CCs work on any MIDI channel. Key names follow Logic, where note 60 is C3.

**MIDI learn:** click **Learn** next to an action, then press a key, or move a pedal or
button that sends a CC. A CC triggers when its value goes from below 64 to 64 or above,
so sustain pedals and most buttons work. Click **Learn** again to cancel. A key or CC can
only be assigned to one action at a time. Assignments are saved with the Logic project.

**Sync:**
- **Free:** the loop is exactly as long as you recorded.
- **Host tempo** (the default): when you finish recording, the length is rounded to the
  nearest multiple of the **Grid** value (1/16 note up to 4 bars), using Logic's project
  tempo and time signature.
  - If you stopped a little *early*, the plugin keeps recording up to the next grid line.
    The state then shows **Recording (to grid)**.
  - If you stopped a little *late*, the loop is cut at the grid line, and playback carries
    on in time.
  - This works whether Logic's transport is playing or not.

  The loop doesn't yet start on Logic's bar lines, so it will drift against Logic's
  grid. Locking the loop to the bar lines is planned for step 3.

**Levels:**
- **Loop:** the playback level of the loop.
- **Input:** how much of the incoming audio passes straight through. If you already hear
  the source somewhere else, for example on the audio track you picked in the Side Chain
  menu, turn **Input** fully down so you don't hear it twice.

**Limits for now:**
- one stereo loop, up to 60 seconds;
- the loop's audio is **not saved** with the project (the settings and MIDI assignments are);
- no undo yet.

## Updating to a newer build

Quit Logic, repeat steps 1–4, and open Logic again. If Logic still uses the old build,
open **Logic Pro X ▸ Preferences ▸ Plug-in Manager…**, select LoopStation, and click
**Reset & Rescan Selection**.

## Troubleshooting

### auval or Logic says "couldn't be opened" (`OpenAComponent: result: -1`)

That means macOS couldn't load the plugin binary. Run the script from the artifact zip:

```sh
python ~/Downloads/sierra_dlopen_test.py
```

It tries to load each system framework the plugin might use, then the plugin itself.

- **CoreAudioKit** is expected to show `FAILED` on the target Mac (see README.md). The
  plugin doesn't use it.
- What matters is the last line: **`ok       plugin binary`**.

If the plugin binary fails, send me the whole output.
