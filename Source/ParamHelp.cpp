#include "ParamHelp.h"
#include <map>

// What each control does, shown by the settings panel's info button.
// Looked up as "<block type>/<control id>" first, then "<control id>" for controls that mean the
// same thing everywhere (mix, level, rate...). Numbered controls (IR 2 level, step 5 pitch) drop
// their number first.
namespace
{
using Map = std::map<juce::String, const char*>;

const Map& generic()
{
    static const Map m = {
        { "mix", "How much of the effect you hear against your dry signal. 0% is dry only, 100% is effect only." },
        { "level", "Output level of this block. Use it to match the volume with the block on and off." },
        { "volume", "Output volume of this block." },
        { "out", "Output level of this block, after everything else in it." },
        { "width", "Stereo width. 0% is mono, 100% is normal stereo, above 100% spreads it even wider." },
        { "rate", "How fast the effect moves (the speed of its modulation)." },
        { "depth", "How strong the movement is. Low is subtle, high is dramatic." },
        { "feedback", "How much of the output is fed back in. More gives more repeats or a stronger, more resonant effect." },
        { "tone", "Darker to brighter." },
        { "drive", "How hard the signal is pushed into saturation." },
        { "dry", "Level of your unaffected guitar mixed with the effect." },
        { "killDry", "Removes your dry signal so you only hear the effect. Use it when the dry sound comes through another path." },
        { "trails", "When on, echoes or the reverb tail keep ringing after you turn the block off (in a scene or by footswitch). When off, they stop with the block." },
        { "sync", "Locks the time or speed to the tempo (the BPM at the top) as a note value instead of milliseconds or Hz." },
        { "attack", "How quickly the effect reacts when you start a note." },
        { "decay", "How long the sound takes to die away." },
        { "release", "How quickly the effect lets go after the note gets quieter." },
        { "sens", "Sensitivity: how strongly the effect responds to your playing. Raise it if it isn't reacting, lower it if it reacts to everything." },
        { "gate", "Noise gate for tracking: signals quieter than this are ignored, so hum and string noise don't trigger notes." },
        { "glide", "Time it takes to slide from one note or pitch to the next." },
        { "lowCut", "Removes low end below this frequency. Raise it to tighten up boomy or muddy sounds." },
        { "highCut", "Removes high end above this frequency. Lower it to tame fizz and harshness." },
        { "swell", "Fades the effect in over this time after you play, so it blooms instead of starting at once." },
        { "shimmer", "Adds a shimmering octave-up layer." },
        { "detune", "Detunes the effected voices slightly for a thicker, chorus-like sound." },
        { "spread", "How far the voices are spread across the stereo field." },
        { "q", "Resonance (Q): how narrow and peaky the filter is. Higher is more vocal and pronounced." },
        { "position", "Pedal position. Map it to an expression pedal (right-click, MIDI learn)." },
        { "type", "Which kind (model) of this effect. Each has its own character." },
        { "mode", "Which mode the effect runs in." },
        { "clean", "Tracks the clean input (before the chain) instead of the signal arriving at this block, so drives and effects before it don't confuse the pitch tracking." },
        { "pitch", "Pitch shift in semitones." },
        { "size", "Size of the space or of the grains." },
        { "hold", "Holds (freezes) the sound. Map it to a footswitch." },
        { "play", "Starts or stops playback. Map it to a footswitch." },
        { "low", "Low frequencies." },
        { "high", "High frequencies." },
        { "mid", "Midrange frequencies." },
        { "octave", "Shifts the pitch up or down by octaves." },
        { "sustain", "How long notes keep going." },
        { "threshold", "Level above which the effect starts working." },
        { "ratio", "How strongly the level is reduced above the threshold." },
        { "makeup", "Turns the level back up after compression." },
        { "wave", "Shape of the modulation wave: smooth (sine), even (triangle) or choppy (square)." },
        { "time", "Delay time." },
        { "modDepth", "How much the delay time wobbles, for a chorused, tape-like repeat." },
        { "modRate", "Speed of that wobble." },
        { "diffusion", "How smeared and dense the sound is. Low keeps separate echoes, high blurs them into a wash." },
        { "polarity", "Flips the signal's polarity. Use it if two signals cancel each other and sound thin." },
        { "pan", "Left / right position." },
        { "align", "Delays this IR by a few samples to line up its phase with the others." },
        { "mute", "Silences this IR." },
        { "key", "The musical key." },
        { "scale", "The musical scale used to pick in-key notes." },
        { "speed", "Playback speed." },
        { "loop", "Loops playback." },
        { "fill", "Plays a fill." },
    };
    return m;
}

const Map& specific()
{
    static const Map m = {
        // NAM amp / pedal
        { "NAM amp/drive", "Boosts or cuts the signal going into the amp capture, like turning a guitar volume or a boost up and down. More drive = more gain." },
        { "NAM amp/tight", "Cuts low end before the amp (like a Tube Screamer in front). Raise it for tighter, less flubby high gain." },
        { "NAM amp/preMid", "Boosts or cuts mids before the amp, changing how it breaks up." },
        { "NAM amp/low", "Bass after the amp." },
        { "NAM amp/mid", "Mids after the amp." },
        { "NAM amp/high", "Treble after the amp." },
        { "NAM amp/out", "Output level of the amp capture." },
        { "NAM pedal/drive", "Level going into the pedal capture: more input means more of the pedal's drive." },
        { "NAM pedal/out", "Output level of the pedal capture." },
        // cab
        { "Cab/level", "Level of this IR in the blend." },
        { "Cab/pan", "Left / right position of this IR." },
        { "Cab/polarity", "Flips this IR's polarity. Try it when two IRs blended together sound thin or hollow." },
        { "Cab/align", "Delays this IR by up to 200 samples to line up two mics, like moving a mic slightly further from the speaker." },
        { "Cab/lowCut", "Removes low end below this frequency, after the IRs." },
        { "Cab/highCut", "Removes high end above this frequency, after the IRs." },
        // gate
        { "Noise gate/threshold", "Signals below this level are turned down. Set it just above your hiss and hum." },
        { "Noise gate/attack", "How quickly the gate opens when you play." },
        { "Noise gate/hold", "How long the gate stays open after the signal drops, before it starts closing." },
        { "Noise gate/release", "How quickly the gate closes." },
        { "Noise gate/range", "How far the gate turns things down when closed. Less depth sounds more natural." },
        // compressor
        { "Compressor/model", "Studio compressor, or an FET compressor in the style of the Origin Effects Cali76." },
        { "Compressor/knee", "How gently compression starts around the threshold. Soft knee is smoother." },
        { "Compressor/in", "Input level into the FET compressor: more input = more compression (the 1176-style way)." },
        { "Compressor/fetRatio", "FET ratio: 4:1 gentle, up to all-buttons for extreme squash." },
        { "Compressor/fetAttack", "FET attack: fast catches the pick attack, slow lets it through for snap." },
        { "Compressor/fetRelease", "FET release: how quickly it recovers." },
        { "Compressor/dry", "Blends in your uncompressed signal (parallel compression) to keep the pick attack." },
        // drive
        { "Drive/type", "Which overdrive or distortion it's modelled on: Tube Screamer style, Klon style, boost, RAT style or fuzz." },
        { "Drive/drive", "Amount of gain." },
        { "Drive/tone", "Treble of the pedal." },
        { "@ Drive/drive", "Gain, from a light push to a thick, singing lead (based on the Andy Timmons @ Drive)." },
        { "@ Drive/eq", "The pedal's tone control: darker to brighter." },
        { "@ Drive/air", "Adds openness and top end sparkle." },
        { "@ Drive/headroom", "How much clean headroom before it compresses: low is squishier, high is more open and dynamic." },
        { "@ Drive/boost", "Engages the Tube Screamer-style boost side for extra mid push." },
        // fuzz
        { "Fuzz/type", "Big Muff (thick, sustaining), silicon Fuzz Face (bright, aggressive) or germanium Fuzz Face in the style of the Buffalo FX Germanium Fuzz (warm, huge, cleans up with your guitar volume)." },
        { "Fuzz/fuzz", "Big Muff: Sustain, how much gain and sustain. Fuzz Face: Fuzz, how hard Q2 is driven." },
        { "Fuzz/tone", "Big Muff tone stack: left is dark and thick, right is bright and thin. Around noon gives the classic mid scoop." },
        { "Fuzz/bias", "The transistor's operating point. Around 5 is the smooth, symmetrical sweet spot. Lower starves it: gated, sputtery, velcro. Higher is cleaner and fatter." },
        { "Fuzz/pregain", "The Buffalo's P knob: how hard the first transistor is pushed. Up for more saturation and compression." },
        { "Fuzz/version", "Which Big Muff: Ram's Head (Gilmour, Animals and The Wall), Civil War (Gilmour, Pulse), Black Russian (Frusciante, Californication), NYC (Frusciante, By the Way / Stadium Arcadium) or Triangle (the first one)." },
        { "Fuzz/mids", "Fills in the Big Muff's mid scoop so it cuts through a band mix." },
        { "Fuzz/toneBypass", "Skips the tone stack: flat, mid-heavy and louder, like the tone bypass switch on later Muffs." },
        // wah / volume
        { "Wah/position", "The rocker position. Map it to an expression pedal." },
        { "Wah/low", "The frequency at the heel (rocked back)." },
        { "Wah/high", "The frequency at the toe (rocked forward)." },
        { "Wah/q", "How vocal and peaky the wah is." },
        { "Volume/position", "Volume pedal position. Map it to an expression pedal." },
        { "Volume/min", "The level with the pedal all the way back." },
        { "Volume/taper", "How the volume changes across the pedal's travel (linear or audio taper)." },
        // EQ
        { "Graphic EQ/b", "Boosts or cuts this frequency band." },
        { "Parametric EQ/hpOn", "Turns the low cut filter on." },
        { "Parametric EQ/hpFreq", "Low cut frequency: everything below is removed." },
        { "Parametric EQ/lpOn", "Turns the high cut filter on." },
        { "Parametric EQ/lpFreq", "High cut frequency: everything above is removed." },
        { "Parametric EQ/f", "Centre frequency of this band. You can also drag the dot on the graph." },
        { "Parametric EQ/g", "Boost or cut of this band." },
        { "Parametric EQ/q", "Width of this band: low Q is broad and gentle, high Q is narrow and surgical." },
        // delay
        { "Delay/type", "Delay model: digital, analog, tape, reverse, ping-pong, and more." },
        { "Delay/time", "Time between repeats." },
        { "Delay/feedback", "Number of repeats. High settings can run away into self-oscillation." },
        { "Delay/wow", "Slow, wobbly pitch drift like an old tape machine." },
        { "Delay/flutter", "Fast, fluttery pitch wobble of worn tape." },
        { "Delay/saturation", "Tape or analog saturation on the repeats." },
        { "Delay/age", "Darker, grittier, more worn repeats as it goes up." },
        { "Delay/ratio", "Right side's time relative to the left, for rhythmic stereo patterns." },
        { "Delay/spread", "How far the repeats bounce across the stereo field." },
        { "Delay/pitch", "Shifts the pitch of each repeat, so repeats climb or fall." },
        { "Delay/pitchMix", "How much of the pitch-shifted repeat you hear." },
        // reverb
        { "Reverb/type", "Reverb model: room, hall, plate, spring, shimmer, swell and others." },
        { "Reverb/decay", "How long the reverb rings." },
        { "Reverb/predelay", "A short gap before the reverb starts, so your note stays clear in front of it." },
        { "Reverb/size", "How big the space is." },
        { "Reverb/mod", "Gentle movement in the tail for a lusher, chorused reverb." },
        { "Reverb/pitch", "Interval of the shimmer: octave, fifth and others." },
        { "Reverb/shimmer", "How much shimmer (pitch-shifted reverb) is in the tail." },
        { "Reverb/dwell", "Spring reverb: how hard the springs are driven." },
        { "Reverb/drip", "Spring reverb: the 'drippy' boing of the springs." },
        // modulation
        { "Chorus/detune", "How far the chorus voices are detuned." },
        { "Chorus/mode", "Mono, stereo or wide chorus voicing." },
        { "Tri-Chorus/delay", "Base delay of the three chorus voices." },
        { "Tri-Chorus/depthL", "Movement of the left voice." },
        { "Tri-Chorus/depthC", "Movement of the centre voice." },
        { "Tri-Chorus/depthR", "Movement of the right voice." },
        { "Flanger/manual", "Where the flange sweep is centred. With the filter matrix on, this sets the fixed position." },
        { "Flanger/matrix", "Stops the sweep and holds the flanger at the Manual position, like the Electric Mistress filter matrix." },
        { "Phaser/stages", "Number of phaser stages: fewer is subtle and vintage, more is deeper and swirlier." },
        { "Tremolo/wave", "Tremolo type: optical (smooth, vintage amp), triangle, choppy (square) or harmonic (Brownface style)." },
        { "Tremolo/width", "Turns it into a stereo pan tremolo." },
        { "Vibrato/depth", "How far the pitch wobbles." },
        { "Rotary/mode", "Mic setup and stereo width of the rotating speaker." },
        { "Rotary/fast", "Switches between slow (chorale) and fast (tremolo) rotor speeds. Map it to a footswitch." },
        { "Rotary/brake", "Stops the rotors." },
        { "Rotary/rise", "How long the rotors take to speed up or slow down when you switch." },
        { "Rotary/slow", "Slow rotor speed." },
        { "Rotary/fastHz", "Fast rotor speed." },
        { "Rotary/balance", "Balance between the bass drum (low rotor) and the horn (high rotor)." },
        { "Rotary/drive", "Overdrive of the speaker cabinet's amp." },
        { "Rotary/effect", "Level of the rotating speaker sound." },
        { "Rotary/direct", "Blends in your unrotated signal (centred)." },
        { "Vibe/mode", "Chorus (vibe mixed with dry, the Uni-Vibe classic) or Vibrato (pitch wobble only)." },
        { "Vibe/rate", "Speed of the vibe." },
        { "Vibe/depth", "Intensity of the vibe." },
        // pitch
        { "Whammy/type", "The interval the pedal sweeps to, up or down (up to two octaves)." },
        { "Whammy/position", "The treadle: heel is unshifted, toe is the full interval. Map it to an expression pedal." },
        { "Whammy/mode", "Classic (shifted only, like the Whammy's whammy mode) or harmony (shifted plus your dry note)." },
        { "Whammy/glide", "Smooths fast pedal moves." },
        { "Whammy/shiftLevel", "Level of the shifted note." },
        { "Octave/mode", "Micro POG (sub, up) or POG2 (adds two octaves down and up)." },
        { "Octave/sub", "One octave down." },
        { "Octave/up", "One octave up." },
        { "Octave/sub2", "Two octaves down." },
        { "Octave/up2", "Two octaves up." },
        { "Octave/attack", "Fades the octave voices in, for organ-like swells." },
        { "Octave/filter", "Low-pass filter on the octave voices." },
        { "Octave/filterQ", "Resonance of that filter." },
        { "Octave/engine", "Single notes (cleanest, fastest tracking) or Chorus (polyphonic, works on chords)." },
        { "Harmonizer/v1", "Interval of the first harmony voice, kept in your key and scale." },
        { "Harmonizer/v1Level", "Level of the first harmony voice." },
        { "Harmonizer/v2", "Interval of the second harmony voice." },
        { "Harmonizer/v2Level", "Level of the second harmony voice." },
        // envelope / swell / doubler
        { "Envelope filter/mode", "Filter shape: low-pass, band-pass or high-pass." },
        { "Envelope filter/dir", "Whether harder playing opens the filter up or sweeps it down." },
        { "Envelope filter/low", "Where the sweep starts." },
        { "Envelope filter/high", "Where the sweep ends." },
        { "Auto-swell/attack", "How long each note takes to fade in (violin-like swells)." },
        { "Auto-swell/thresh", "How hard you need to pick before a new swell starts." },
        { "Auto-swell/curve", "Shape of the fade-in." },
        { "Doubler/time", "How late the double is, like a second take played slightly behind." },
        { "Doubler/mix", "Level of the double." },
        // freeze
        { "Freeze/hold", "Freezes what you're playing into an endless sound. Map it to a footswitch." },
        { "Freeze/mode", "Freeze (hold one moment) or Infinite (keeps adding what you play while held)." },
        { "Freeze/source", "What gets frozen." },
        { "Freeze/pitch", "Shifts the frozen sound up or down." },
        { "Freeze/quant", "Snaps the pitch to semitones, or lets it move freely." },
        { "Freeze/fadeIn", "How long the frozen sound takes to swell in." },
        { "Freeze/fadeOut", "How long it takes to fade away after you release Hold." },
        { "Freeze/set", "Which notes the freeze keeps: all notes, a chord shape, a scale, or your own selection (Custom)." },
        { "Freeze/root", "Root note of the note set." },
        { "Freeze/n", "Keeps or removes this note from the frozen sound (Custom note set)." },
        { "Freeze/lv", "Level of this note in the frozen sound." },
        { "Freeze/mod", "Slow, gentle movement in the pad, so it breathes." },
        { "Freeze/seqOn", "Turns on the step sequencer, which changes the frozen sound's pitch and level in time." },
        { "Freeze/seqLength", "Number of steps before the pattern repeats (1 to 16)." },
        { "Freeze/seqRate", "Length of each step, locked to the tempo." },
        { "Freeze/seqGlide", "Slides between step pitches instead of jumping." },
        { "Freeze/seqRun", "Whether the sequence runs while Hold is on, or follows MIDI start/stop (or Logic's transport)." },
        { "Freeze/sp", "Pitch of this step, in semitones." },
        { "Freeze/sv", "Level of this step." },
        { "Freeze/space", "Reverb-like space around the frozen sound." },
        { "Freeze/spacelive", "How much of that space is also added to what you're playing live." },
        // looper
        { "Looper/rec", "Press to record, press again to play, press again to overdub. Map it to a footswitch." },
        { "Looper/stop", "Stops the loop, or starts it again from the top." },
        { "Looper/clear", "Erases the selected track." },
        { "Looper/track", "Which of the 4 tracks you record or overdub onto." },
        { "Looper/next", "Moves to the next track (you can switch while overdubbing)." },
        { "Looper/undo", "Erases the last track you recorded." },
        { "Looper/reverse", "Plays the loops backwards." },
        { "Looper/half", "Plays the loops at half speed (an octave down)." },
        { "Looper/sync", "Free length, or rounds the first loop to whole bars of the tempo so it stays in time with the drums." },
        { "Looper/after", "What happens when you finish the first recording: play it back, or keep overdubbing." },
        { "Looper/feedback", "How much of the existing loop is kept each time you overdub over it. Below 100% older layers slowly fade." },
        { "Looper/lv", "Level of this track." },
        { "Looper/mix", "Level of all the loops together." },
        { "Looper/dry", "Lets you hear your live playing as well as the loops." },
        { "Looper/autosave", "Saves each loop as an uncompressed WAV in the Loops folder on your SSD." },
        { "Looper/save", "Saves the loops now." },
        // drums
        { "Drums/play", "Starts or stops the drums. Map it to a footswitch." },
        { "Drums/groove", "The beat: rock, funk, blues shuffle, jazz, metal and more." },
        { "Drums/kit", "Which drum kit: recorded acoustic, 808, or the built-in electronic kit." },
        { "Drums/swing", "Pushes the off-beats later for a swung, shuffled feel." },
        { "Drums/human", "Small timing and velocity variations so it sounds less like a machine." },
        { "Drums/fill", "Plays a fill into the next bar." },
        { "Drums/runs", "What starts the drums: the Start/stop control, MIDI start/stop (or Logic), or the looper." },
        { "Drums/source", "Play a built-in groove, or one of your own drum loops." },
        { "Drums/loop", "Which of your drum loops (from the Drums/Loops folder on the SSD)." },
        { "Drums/fit", "Speeds the loop up or down so it fits the tempo." },
        { "Drums/kickLv", "Kick level." },
        { "Drums/snareLv", "Snare level." },
        { "Drums/hatLv", "Hi-hat level." },
        { "Drums/cymLv", "Ride and crash level." },
        { "Drums/tomLv", "Tom level." },
        // saxophone
        { "Saxophone/model", "Which sax: soprano, alto, tenor or baritone." },
        { "Saxophone/character", "Overall playing style, from sweet and breathy to bright and edgy." },
        { "Saxophone/octave", "Which octave the sax plays in, relative to your guitar." },
        { "Saxophone/autoRange", "Folds notes back into the real instrument's range, so it never plays notes a real sax can't." },
        { "Saxophone/dynamics", "How much your pick strength changes the sax's loudness and tone." },
        { "Saxophone/sustain", "How long the sax holds a note while your guitar note fades." },
        { "Saxophone/attack", "How hard and biting the start of each note is." },
        { "Saxophone/legato", "Glide time when you change notes without re-picking (slides, hammer-ons)." },
        { "Saxophone/tongue", "How clearly re-picking the same note re-articulates it, like tonguing." },
        { "Saxophone/bright", "Brightness of the horn." },
        { "Saxophone/breath", "Amount of breath noise in the tone." },
        { "Saxophone/growl", "Growl and grit, for R&B and rock sax." },
        { "Saxophone/subtone", "Soft, airy, dark subtone (low and breathy)." },
        { "Saxophone/body", "Warmth and body of the horn." },
        { "Saxophone/clicks", "The sound of the keys clicking on note changes." },
        { "Saxophone/scoop", "Bends up into each note from slightly below." },
        { "Saxophone/fall", "Falls off at the end of notes." },
        { "Saxophone/pitchMode", "Follows your exact pitch (bends and vibrato) or snaps to the nearest note." },
        { "Saxophone/guitarVib", "How much of your own finger vibrato and bends the sax follows." },
        { "Saxophone/vibMode", "Adds the sax's own vibrato: off, always, delayed, or from an expression pedal." },
        { "Saxophone/vibAmount", "How much the expression pedal adds vibrato." },
        { "Saxophone/vibDepth", "Depth of the sax's vibrato." },
        { "Saxophone/vibRate", "Speed of the sax's vibrato." },
        { "Saxophone/vibDelay", "How long a note sustains before the vibrato starts." },
        { "Saxophone/sound", "Recorded (real sax recordings) or Modeled (synthesized)." },
        { "Saxophone/land", "Gently pulls the pitch into tune at the end of bends and slides." },
        { "Saxophone/normalize", "Evens out the loudness between notes and registers." },
        // PhatTrack
        { "PhatTrack/gate", "Tracking noise gate: signals below this don't trigger synth notes." },
        { "PhatTrack/dry", "Level of your guitar mixed with the synth." },
        { "PhatTrack/retrig", "Restarts the envelopes on every pick, even on the same note." },
        { "PhatTrack/tune", "Fine-tunes the synth in cents." },
        { "PhatTrack/glideOn", "Turns portamento (sliding between notes) on." },
        { "PhatTrack/o1oct", "Oscillator 1 octave." },
        { "PhatTrack/o1wave", "Oscillator 1 waveform: saw, square, triangle, sine and others." },
        { "PhatTrack/o1semi", "Oscillator 1 semitone offset." },
        { "PhatTrack/o1det", "Oscillator 1 detune in cents." },
        { "PhatTrack/o1lvl", "Oscillator 1 level." },
        { "PhatTrack/o2oct", "Oscillator 2 octave." },
        { "PhatTrack/o2wave", "Oscillator 2 waveform." },
        { "PhatTrack/o2semi", "Oscillator 2 semitone offset (7 = a fifth up)." },
        { "PhatTrack/o2det", "Oscillator 2 detune in cents: a little detune makes it fatter." },
        { "PhatTrack/o2lvl", "Oscillator 2 level." },
        { "PhatTrack/sync", "Syncs oscillator 2 to oscillator 1 for hard, tearing sync leads." },
        { "PhatTrack/sub", "Sub oscillator one octave below." },
        { "PhatTrack/noise", "Noise mixed into the oscillators." },
        { "PhatTrack/cutoff", "Filter cutoff: lower is darker." },
        { "PhatTrack/res", "Filter resonance: a peak at the cutoff, up to squelchy." },
        { "PhatTrack/keytrk", "How much the filter opens as you play higher notes." },
        { "PhatTrack/fenvamt", "How much the filter envelope sweeps the cutoff." },
        { "PhatTrack/fatk", "Filter envelope attack." },
        { "PhatTrack/fdec", "Filter envelope decay." },
        { "PhatTrack/fsus", "Filter envelope sustain level." },
        { "PhatTrack/frel", "Filter envelope release." },
        { "PhatTrack/aatk", "Volume envelope attack: slow for pads, fast for plucks." },
        { "PhatTrack/adec", "Volume envelope decay." },
        { "PhatTrack/asus", "Volume envelope sustain level." },
        { "PhatTrack/arel", "Volume envelope release: how long the synth rings after the note." },
        { "PhatTrack/lforate", "LFO speed." },
        { "PhatTrack/modsrc", "What modulates: the LFO, the envelope, or your pick velocity." },
        { "PhatTrack/moddst", "What gets modulated: pitch, filter, volume or pulse width." },
        { "PhatTrack/modamt", "How much it's modulated." },
        // granular
        { "Granular/type", "Granular mode: clouds, glitch, reverse and others." },
        { "Granular/density", "How many grains play at once: sparse to a dense cloud." },
        { "Granular/smear", "How much the grains are scattered in time." },
        { "Granular/reverse", "Chance that a grain plays backwards." },
        { "Granular/chance", "How often it glitches by itself." },
        { "Granular/repeats", "How many times a glitch repeats." },
        { "Granular/playback", "How the grains are played back." },
        // 12-string / acoustic / backing
        { "12-string/octave", "Level of the octave strings that make it a 12-string." },
        { "12-string/split", "Which strings get the octave (on a real 12-string the top two strings get a unison instead)." },
        { "12-string/spread", "Strum timing between each string and its octave partner." },
        { "Acoustic sim/type", "Which acoustic guitar body it imitates." },
        { "Acoustic sim/body", "Amount of body resonance." },
        { "Acoustic sim/top", "Top-end sparkle and string detail." },
        { "Acoustic sim/room", "Room ambience, like a mic in a room." },
        { "Acoustic sim/roomSize", "Size of that room." },
        { "Backing track/track", "Which backing track to play." },
        { "Backing track/restart", "Starts the track again from the beginning." },
        { "Backing track/key", "Shifts the track's key without changing its speed." },
        { "Backing track/loopSection", "Loops a section of the track for practising." },
        { "Backing track/loopStart", "Where the loop section starts." },
        { "Backing track/loopEnd", "Where the loop section ends." },
        { "Backing track/setStart", "Sets the loop start to where the track is now." },
        { "Backing track/setEnd", "Sets the loop end to where the track is now." },
        { "Backing track/back", "Jumps back 5 seconds." },
        { "Backing track/forward", "Jumps forward 5 seconds." },
        // utility
        { "Utility/level", "Level change. Turn it up here for a clean solo boost at the end of the chain." },
        { "Utility/mono", "Sums the signal to mono." },
    };
    return m;
}

juce::String stripNumber (const juce::String& id)
{
    // "level2" -> "level", "sp12" -> "sp", "b4" -> "b"
    int end = id.length();
    while (end > 1 && juce::CharacterFunctions::isDigit (id[end - 1])) --end;
    return id.substring (0, end);
}
}

juce::String paramHelp (const Block& b, const Param& prm)
{
    const auto type = Block::typeName (b.getType());
    const auto& sp = specific();
    const auto& ge = generic();
    for (const auto& id : { prm.id, stripNumber (prm.id) })
    {
        if (auto it = sp.find (type + "/" + id); it != sp.end()) return it->second;
        if (auto it = ge.find (id); it != ge.end()) return it->second;
    }
    return {};
}

juce::String blockHelp (const Block& b)
{
    switch (b.getType())
    {
        case Block::Type::NamAmp: return "An amp capture (NAM). Drag captures onto it from the library. Its colour shows how much gain the capture has.";
        case Block::Type::NamPedal: return "A pedal capture (NAM), like a drive or boost.";
        case Block::Type::CabStudio: return "A cabinet from your IR library with two mics you place on the speaker in 3D. Measured spots play the real IRs; anything in between is blended or modelled, and the editor says which.";
        case Block::Type::Cab: return "Speaker cabinet IRs. Blend up to three, each with its own level, pan, polarity and alignment.";
        case Block::Type::Freeze: return "Freezes what you play into an endless pad (Strymon Night Sky style), with note selection, shimmer and a step sequencer.";
        case Block::Type::Looper: return "A 4-track looper in the style of the Guitargate Teleport. Loops are saved as WAVs on your SSD.";
        case Block::Type::Drums: return "A drum machine with genre grooves, fills and your own drum loops.";
        case Block::Type::Fuzz: return "Big Muff, silicon Fuzz Face and germanium Fuzz Face (Buffalo FX style) fuzzes.";
        case Block::Type::Rotary: return "A rotating speaker (Leslie) in the style of the BOSS RT-20.";
        case Block::Type::Vibe: return "A Uni-Vibe style photocell phaser/vibrato.";
        case Block::Type::Octave: return "Octave pedal in the style of the Micro POG and POG2.";
        case Block::Type::Whammy: return "Pitch-shift pedal in the style of the DigiTech Whammy.";
        default: return {};
    }
}
