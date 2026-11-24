# NAM Home

A private-use Mac app: a home for your own NAM captures (.nam, A1 and A2) and cabinet IRs (.wav),
with a block-style signal chain. No captures or IRs are included; every capture shows who made it.

## Install / update (on your Mac)

One time:

    curl -fsSL https://raw.githubusercontent.com/alithosanesti33-tech/nam-home-/main/scripts/namupdate.sh -o ~/namupdate.sh && chmod +x ~/namupdate.sh

Then, any time a new build is ready:

    ~/namupdate.sh

## Library

Your library lives in `Music/NAM Home`. New `.nam` files, short `.wav` IRs (2 seconds or less) and `.zip`
files containing them are picked up from your Downloads folder automatically (turn this off in Settings).

## Build stages

1. Sound engine: NAM captures (A1 + A2), IR blend, audio settings, library + Downloads watcher  ← now
2. Chain screen: 4 x 10 grid, parallel paths, pop-out library
3. Effects: EQs, gate, compressor, drives, delays, reverbs, modulation, tri-stereo chorus, metronome
4. Presets, scenes and MIDI
5. Polish and the Logic plugin (AU)

Built with JUCE 8 and NeuralAmpModelerCore (MIT).
