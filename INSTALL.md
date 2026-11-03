# Installing LoopStation Gain (proof-of-concept) on macOS 10.12 / Logic Pro X 10.2.3

## 1. Download the build

1. Open the repository on GitHub, click the **Actions** tab, then **Build AU (macOS 10.12, x86_64)**.
2. Open the most recent run with a green check mark.
3. Under **Artifacts** at the bottom of the page, click **LoopStationGain-AU-x86_64**.
   You must be signed in to GitHub. Artifacts are deleted after 90 days.
4. That download is a zip with another zip inside it: `LoopStationGain-AU-macOS10.12-x86_64.zip`.
   Double-click both. (Safari may unzip the outer one by itself.) You end up with
   **`LoopStationGain.component`**.

> If GitHub doesn't work well in the browser on the Sierra Mac, download the zip on
> another computer and copy it over with a USB stick or AirDrop. Copy the **zip**, not
> the unzipped bundle, so nothing inside it gets changed on the way.

The artifact also contains `compat-check.txt` and `BUILD_INFO.txt`, which show what was
built and checked.

## 2. Copy it into the Components folder

Install it for your user only (recommended):

```
~/Library/Audio/Plug-Ins/Components/
```

(Or `/Library/Audio/Plug-Ins/Components/` for all users, which needs an admin password.)

In Finder: **Go ▸ Go to Folder…** (⇧⌘G), paste the path, and drag the `.component` into it.
Or in Terminal, assuming it was unzipped into Downloads:

```sh
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R ~/Downloads/LoopStationGain.component ~/Library/Audio/Plug-Ins/Components/
```

## 3. Remove the quarantine flag

Files downloaded from the internet get a quarantine flag, and that can stop the
unsigned plugin from loading. Remove it:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/LoopStationGain.component
```

To check that it worked, run the command below. It should print nothing:

```sh
xattr -lr ~/Library/Audio/Plug-Ins/Components/LoopStationGain.component
```

## 4. Check that it loads with `auval`

Make macOS rescan the plugin folders. If it says "No matching processes", that's fine:

```sh
killall -9 AudioComponentRegistrar
```

Check that the system can see the plugin:

```sh
auval -a | grep -i loopstation
```

You should see a line similar to `aufx Lsg1 Lsau  -  LoopStation: LoopStation Gain`.

Run the full validation:

```sh
auval -v aufx Lsg1 Lsau
```

The last line must be **`AU VALIDATION SUCCEEDED.`** If it fails, save the output and
send it to me:

```sh
auval -v aufx Lsg1 Lsau > ~/Desktop/auval.txt 2>&1
```

## 5. Check it in Logic Pro X

1. Quit Logic if it is running, then open it again. Logic scans new plugins when it starts.
2. Open the Plug-in Manager: **Logic Pro X ▸ Preferences ▸ Plug-in Manager…**
   (Some versions put it directly in the **Logic Pro X** menu.)
3. Click the manufacturer **LoopStation** on the left. **LoopStation Gain** should be listed
   with the Compatibility column showing **successfully validated**, and its **Use**
   box ticked.
   - If it says *failed validation*, select it and click **Reset & Rescan Selection**.
     If it still fails, run `auval` (step 4) and send me the output.
4. Test it: on an audio track, click an empty **Audio FX** slot and choose
   **Audio Units ▸ LoopStation ▸ LoopStation Gain**. At 0 dB the sound should be
   unchanged. Turning the knob down should make it quieter.

## Updating to a newer build

1. Quit Logic.
2. Delete the old copy:
   `rm -rf ~/Library/Audio/Plug-Ins/Components/LoopStationGain.component`
3. Repeat steps 1–4, then open Logic. If Logic still runs the old build, go to the
   Plug-in Manager and click **Reset & Rescan Selection** for LoopStation Gain.
