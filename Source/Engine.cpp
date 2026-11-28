#include "Engine.h"

using namespace ndsp;

Engine::Engine()
{
    loadMeasurer.reset (48000.0, 512);
}

Engine::~Engine() = default;

void Engine::prepare (double sr, int mb)
{
    sampleRate = sr;
    maxBlock = juce::jmax (32, mb);
    for (auto& r : buf) for (auto& c : r) c.assign ((size_t) maxBlock, 0.0f);
    for (auto& c : cleanBuf) c.assign ((size_t) maxBlock, 0.0f);
    for (auto& c : spillBuf) c.assign ((size_t) maxBlock, 0.0f);
    loadMeasurer.reset (sr, maxBlock);
    for (auto& row : cells)
        for (auto& b : row)
            if (b != nullptr) b->prepareBlock (sr, maxBlock);
    if (cabStudio != nullptr) cabStudio->prepareBlock (sr, maxBlock);
    clickSamples = (int) (0.025 * sr);
    resetLimiter (sr);
    for (auto& s : shifter) s.prepare ((float) sr);
}

//==============================================================================
void Engine::publish()
{
    Plan p;
    for (int r = 0; r < rows; ++r)
    {
        p.first[r] = cols; p.last[r] = -1;
        for (int c = 0; c < cols; ++c)
        {
            p.cell[r][c] = cells[(size_t) r][(size_t) c].get();
            if (p.cell[r][c] != nullptr) { p.first[r] = std::min (p.first[r], c); p.last[r] = std::max (p.last[r], c); }
        }
        p.active[r] = r == 0 || p.last[r] >= 0;
    }
    p.numSpill = 0;
    for (auto& b : spillBlocks) if (p.numSpill < 8) p.spill[p.numSpill++] = b.get();
    // Cab Studio goes right after the first amp (the main row first); with no amp, after the last
    // pedal on the main row, or at the start
    p.rigCab = cabStudio.get();
    {
        bool placed = false;
        for (int r = 0; r < rows && ! placed; ++r)
            for (int c = 0; c < cols && ! placed; ++c)
                if (cells[(size_t) r][(size_t) c] != nullptr && cells[(size_t) r][(size_t) c]->getType() == Block::Type::NamAmp) { p.cabRow = r; p.cabCol = c; placed = true; }
        if (! placed)
        {
            p.cabRow = 0; p.cabCol = -1;
            for (int c = 0; c < cols; ++c)
                if (auto& b = cells[0][(size_t) c]; b != nullptr && (Block::categoryName (b->getType()) == "Pedals" || Block::categoryName (b->getType()) == "Guitar sims")) p.cabCol = c;
        }
    }
    std::vector<std::shared_ptr<Block>> alive;
    for (auto& row : cells) for (auto& b : row) if (b != nullptr) alive.push_back (b);
    for (auto& b : spillBlocks) alive.push_back (b);
    if (cabStudio != nullptr) alive.push_back (cabStudio);

    std::vector<std::shared_ptr<Block>> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        live = p;
        old.swap (keepAlive);
        keepAlive = std::move (alive);
    }
    // blocks that were removed are destroyed here, on the message thread
}

void Engine::set (int r, int c, std::shared_ptr<Block> b)
{
    if (b != nullptr) b->prepareBlock (sampleRate, maxBlock);
    cells[(size_t) r][(size_t) c] = std::move (b);
    publish();
}

std::shared_ptr<Block> Engine::addBlock (Block::Type t, int r, int c)
{
    std::shared_ptr<Block> b (Block::create (t).release());
    set (r, c, b);
    return b;
}

std::shared_ptr<Block> Engine::rigCab (bool create)
{
    if (cabStudio == nullptr && create)
    {
        std::shared_ptr<Block> b (Block::create (Block::Type::CabStudio).release());
        b->prepareBlock (sampleRate, maxBlock);
        cabStudio = b;
        // scenes made before it existed start with it as it is now
        publish();
    }
    return cabStudio;
}

void Engine::move (int r1, int c1, int r2, int c2)
{
    std::swap (cells[(size_t) r1][(size_t) c1], cells[(size_t) r2][(size_t) c2]);
    publish();
}

void Engine::clearAll()
{
    for (auto& row : cells) for (auto& b : row) b = nullptr;
    cabStudio = nullptr;
    for (auto& m : mappings) m = {};
    resetScenes();
    publish();
}

//==============================================================================
void Engine::captureScene (SceneState& into) const
{
    into.blocks.clear();
    for (auto& row : cells)
        for (auto& b : row)
            if (b != nullptr)
            {
                std::vector<float> vals;
                vals.reserve (b->params.size());
                for (auto& prm : b->params) vals.push_back (prm->get());
                into.blocks[b->id] = { b->enabled.load(), std::move (vals) };
            }
    if (cabStudio != nullptr)
    {
        std::vector<float> vals;
        for (auto& prm : cabStudio->params) vals.push_back (prm->get());
        into.blocks[cabStudio->id] = { cabStudio->enabled.load(), std::move (vals) };
    }
    into.valid = true;
}

void Engine::applyScene (const SceneState& sc)
{
    std::vector<std::shared_ptr<Block>> all;
    for (auto& row : cells) for (auto& b : row) all.push_back (b);
    all.push_back (cabStudio);
    for (auto& b : all)
        {
            if (b == nullptr) continue;
            auto it = sc.blocks.find (b->id);
            if (it == sc.blocks.end()) continue;   // added after this scene was made: leave as it is
            b->enabled = it->second.first;
            const auto& vals = it->second.second;
            for (size_t i = 0; i < b->params.size() && i < vals.size(); ++i)
                if (! b->params[i]->momentary && b->params[i]->get() != vals[i]) b->params[i]->set (vals[i]);
        }
}

void Engine::selectScene (int s)
{
    s = juce::jlimit (0, numScenes - 1, s);
    const int cur = currentScene.load();
    if (s == cur) return;
    captureScene (scenes[(size_t) cur]);
    currentScene = s;
    if (scenes[(size_t) s].valid) applyScene (scenes[(size_t) s]);
    else captureScene (scenes[(size_t) s]);   // a new scene starts as a copy of where you are
    if (cutTailsOnScene.load())
        for (auto& row : cells)
            for (auto& b : row)
                if (b != nullptr && b->hasTails()) b->cutTail = true;
    ++sceneVersion;
}

void Engine::copyScene (int from, int to)
{
    if (from == to) return;
    const int cur = currentScene.load();
    if (from == cur) captureScene (scenes[(size_t) from]);
    const auto name = scenes[(size_t) to].name;
    scenes[(size_t) to] = scenes[(size_t) from];
    scenes[(size_t) to].name = name;
    if (to == cur) applyScene (scenes[(size_t) to]);
    ++sceneVersion;
}

void Engine::resetScenes()
{
    for (auto& sc : scenes) sc = {};
    currentScene = 0;
    pendingScene = -1;
    ++sceneVersion;
}

juce::String Engine::sceneLabel (int s) const
{
    const juce::String letter = juce::String::charToString ((juce::juce_wchar) ('A' + s));
    const auto& n = scenes[(size_t) juce::jlimit (0, numScenes - 1, s)].name;
    return n.isEmpty() ? letter : letter + "  " + n;
}

std::shared_ptr<Block> Engine::findById (int id) const
{
    for (auto& row : cells) for (auto& b : row) if (b != nullptr && b->id == id) return b;
    if (cabStudio != nullptr && cabStudio->id == id) return cabStudio;
    return nullptr;
}

bool Engine::findCell (int id, int& r, int& c) const
{
    for (r = 0; r < rows; ++r)
        for (c = 0; c < cols; ++c)
            if (cells[(size_t) r][(size_t) c] != nullptr && cells[(size_t) r][(size_t) c]->id == id) return true;
    return false;
}

bool Engine::insertInRow (int r, int index, std::shared_ptr<Block> b)
{
    auto& row = cells[(size_t) r];
    if (row[(size_t) (cols - 1)] != nullptr)
    {
        // full at the end: only possible if there is a hole at or after the insert point
        int hole = -1;
        for (int c = index; c < cols; ++c) if (row[(size_t) c] == nullptr) { hole = c; break; }
        if (hole < 0) return false;
        for (int c = hole; c > index; --c) row[(size_t) c] = row[(size_t) (c - 1)];
    }
    else
        for (int c = cols - 1; c > index; --c) row[(size_t) c] = row[(size_t) (c - 1)];
    row[(size_t) index] = std::move (b);
    publish();
    return true;
}

void Engine::removeFromRow (int r, int c, bool closeGap)
{
    auto& row = cells[(size_t) r];
    row[(size_t) c] = nullptr;
    if (closeGap)
        for (int k = c; k < cols - 1; ++k) { row[(size_t) k] = row[(size_t) (k + 1)]; row[(size_t) (k + 1)] = nullptr; }
    publish();
}

void Engine::sceneToNewRig (int s)
{
    s = juce::jlimit (0, numScenes - 1, s);
    selectScene (s);                         // the blocks now sound like scene s
    SceneState keep;
    captureScene (keep);
    keep.name = scenes[(size_t) s].name;
    keep.trimDb = scenes[(size_t) s].trimDb;
    for (auto& sc : scenes) sc = {};
    scenes[0] = keep;
    currentScene = 0;
    pendingScene = -1;
    for (int m = 0; m <= presetMeter; ++m) resetLoudness (m);
    ++sceneVersion;
}

void Engine::resetLimiter (double sr)
{
    limLen = juce::jlimit (8, limMax - 1, (int) std::lround (0.001 * sr));   // 1 ms look-ahead
    for (int k = 0; k < limMax; ++k) { limL[k] = limR[k] = 0.0f; limMin[k] = 1.0f; }
    for (auto& q : limReq) q = 1.0f;
    limPos = limReqPos = 0;
    limBox = limLen;
    limRel = 1.0f;
    limRelCoef = 1.0f - std::exp (-1.0f / (0.06f * (float) sr));
}

float Engine::measuredLoudness (int meter, int* seconds) const
{
    const auto& h = loudHist[juce::jlimit (0, presetMeter, meter)];
    auto mean = [&h] (float gate, int& count)
    {
        double e = 0; count = 0;
        for (int b = 0; b < loudBins; ++b)
        {
            const float lv = -70.0f + (b + 0.5f) * 0.5f;
            const int c = h[b].load();
            if (c == 0 || lv < gate) continue;
            e += c * std::pow (10.0, lv / 10.0); count += c;
        }
        return count > 0 ? (float) (10.0 * std::log10 (e / count)) : -100.0f;
    };
    int c1 = 0, c2 = 0;
    const float ungated = mean (-60.0f, c1);           // absolute gate: ignore silence and hum
    const float integrated = mean (ungated - 10.0f, c2);  // relative gate: ignore pauses and decays
    if (seconds != nullptr) *seconds = c2 / 10;
    return c2 >= 20 ? integrated : -100.0f;            // needs about 2 s of playing
}

bool Engine::sceneIsUsed (int s) const { return s == currentScene.load() || scenes[(size_t) juce::jlimit (0, numScenes - 1, s)].valid; }

void Engine::resetLoudness (int meter)
{
    for (auto& b : loudHist[juce::jlimit (0, presetMeter, meter)]) b = 0;
}

float Engine::sceneTrim (int s) const { return scenes[(size_t) juce::jlimit (0, numScenes - 1, s)].trimDb; }
void Engine::setSceneTrim (int s, float db) { scenes[(size_t) juce::jlimit (0, numScenes - 1, s)].trimDb = juce::jlimit (-24.0f, 24.0f, db); }

const Engine::Mapping* Engine::findMapping (int blockId, int param) const
{
    for (auto& m : mappings) if (m.kind >= 0 && m.blockId == blockId && m.param == param) return &m;
    return nullptr;
}
void Engine::setMappingEnd (int blockId, int param, bool toe, float norm)
{
    for (auto& m : mappings) if (m.kind >= 0 && m.blockId == blockId && m.param == param) (toe ? m.hi : m.lo) = juce::jlimit (0.0f, 1.0f, norm);
}
void Engine::setMappingCurve (int blockId, int param, int curve)
{
    for (auto& m : mappings) if (m.kind >= 0 && m.blockId == blockId && m.param == param) m.curve = curve;
}
void Engine::resetMappingRange (int blockId, int param)
{
    for (auto& m : mappings) if (m.kind >= 0 && m.blockId == blockId && m.param == param) { m.lo = 0; m.hi = 1; m.curve = 0; }
}

bool Engine::setRow (int r, int startCol, const std::vector<std::shared_ptr<Block>>& blocks)
{
    if (startCol < 0 || startCol + (int) blocks.size() > cols) return false;
    auto& row = cells[(size_t) r];
    for (auto& c : row) c = nullptr;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        auto b = blocks[i];
        if (b != nullptr && ! b->isPrepared()) b->prepareBlock (sampleRate, maxBlock);
        row[(size_t) startCol + i] = b;
    }
    publish();
    return true;
}

std::vector<std::shared_ptr<Block>> Engine::rowBlocks (int r) const
{
    std::vector<std::shared_ptr<Block>> v;
    for (auto& c : cells[(size_t) r]) if (c != nullptr) v.push_back (c);
    return v;
}

bool Engine::rowSpan (int r, int& first, int& last) const
{
    first = cols; last = -1;
    for (int c = 0; c < cols; ++c)
        if (cells[(size_t) r][(size_t) c] != nullptr) { first = std::min (first, c); last = std::max (last, c); }
    return last >= 0;
}

//==============================================================================
void Engine::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapTimes.isEmpty() && now - tapTimes.getLast() > 2000.0) tapTimes.clear();
    tapTimes.add (now);
    if (tapTimes.size() > 5) tapTimes.remove (0);
    if (tapTimes.size() >= 2)
    {
        const double avg = (tapTimes.getLast() - tapTimes.getFirst()) / (tapTimes.size() - 1);
        setBpm ((float) (60000.0 / avg));
    }
}

void Engine::readTuner (float* dest, int n) const
{
    int r = (tunerWrite.load() - n) & (tunerSize - 1);
    for (int i = 0; i < n; ++i) { dest[i] = tunerBuf[r]; r = (r + 1) & (tunerSize - 1); }
}

void Engine::armLearn (int blockId, int param)
{
    learnBlock = blockId;
    learnParam = param;
    learnStage = 0;
    learnSawRelease = false;
    learnFirstSeen = 0;
    learnArmed = true;
}

void Engine::clearMappingsFor (int blockId, int param)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& m : mappings)
        if (m.blockId == blockId && m.param == param) m = {};
}

static juce::String midiName (int kind, int num)
{
    switch (kind)
    {
        case Engine::kindCC:      return "CC " + juce::String (num);
        case Engine::kindNote:    return "Note " + juce::MidiMessage::getMidiNoteName (num, true, true, 4);
        case Engine::kindProgram: return "Program " + juce::String (num + 1);
        default: break;
    }
    return {};
}

juce::String Engine::describeMapping (int blockId, int param) const
{
    for (auto& m : mappings)
        if (m.kind >= 0 && m.blockId == blockId && m.param == param)
            return midiName (m.kind, m.number) + (m.toggle ? " (toggle)" : "");
    return {};
}

juce::String Engine::lastMidiText() const
{
    const int k = lastKind.load();
    if (k < 0) return "none yet";
    juce::String s = midiName (k, lastNum.load());
    if (k != kindProgram) s << " = " << lastVal.load();
    s << "  (channel " << lastChannel.load() << ")";
    return s;
}

void Engine::pollMessageThread()
{
    if (! spillBlocks.empty())
    {
        const double t = (juce::Time::getMillisecondCounterHiRes() - spillStarted) / 1000.0;
        spillGain = t < 4.0 ? 1.0f : (float) juce::jmax (0.0, 1.0 - (t - 4.0) / 2.5);
        if (t > 6.6) { spillBlocks.clear(); spillGain = 0.0f; publish(); }
    }
    if (learnArmed.load() && learnStage.load() == 1)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (learnFirstSeen == 0) learnFirstSeen = now;
        else if (now - learnFirstSeen > 650.0)   // wait briefly to see whether a switch also sends a release
        {
            const int kind = learnedKind.load(), num = learnedNum.load();
            const int bid = learnBlock.load(), prm = learnParam.load();
            bool switchTarget = prm < 0 || bid == -1 || bid == sceneMidiId || bid == setlistMidiId;
            if (! switchTarget)
                if (auto b = findById (bid))
                    if (prm < (int) b->params.size()) switchTarget = b->params[(size_t) prm]->kind != Param::Kind::Float;
            const bool momentary = learnSawRelease.load() || kind == kindProgram;
            Mapping nm;
            nm.kind = kind; nm.number = num; nm.blockId = bid; nm.param = prm;
            // a momentary switch toggles on each press; a latching switch or a pedal/knob follows its value
            nm.toggle = (momentary && (switchTarget || kind == kindProgram || kind == kindNote)) ? 1 : 0;
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                for (auto& m : mappings) if (m.blockId == bid && m.param == prm) m = {};
                for (auto& m : mappings) if (m.kind < 0) { m = nm; break; }
            }
            learnStage = 0;
            learnArmed = false;
        }
    }
    if (panicLearn.load() && panicLearnStage.load() == 1)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (panicLearnSeen == 0) panicLearnSeen = now;
        else if (now - panicLearnSeen > 650.0)   // wait briefly to see whether the switch also sends a release
        {
            const int kind = panicCandKind.load();
            panicKind = kind; panicNum = panicCandNum.load();
            panicLatch = kind == kindCC && ! panicSawRelease.load();
            panicLearnStage = 0; panicLearn = false; panicLearned = true;
        }
    }
    if (tapFromMidi.exchange (false)) tapTempo();
    const int ps = pendingScene.exchange (-1);
    if (ps >= 0) selectScene (ps);
    for (auto& row : cells)
        for (auto& b : row)
            if (auto* cab = dynamic_cast<CabBlock*> (b.get())) cab->updateAlignment();
}

void Engine::applyMapping (const Mapping& m, int value, bool press, const Plan& plan)
{
    if (m.blockId == -1) { if (press) tapFromMidi = true; return; }
    if (m.blockId == sceneMidiId) { if (press) pendingScene = m.param; return; }
    if (m.blockId == setlistMidiId) { if (press) setlistStepFromMidi = m.param == 0 ? -1 : 1; return; }
    Block* found = plan.rigCab != nullptr && plan.rigCab->id == m.blockId ? plan.rigCab : nullptr;
    for (int r = 0; r < rows && found == nullptr; ++r)
        for (int c = 0; c < cols && found == nullptr; ++c)
            if (plan.cell[r][c] != nullptr && plan.cell[r][c]->id == m.blockId) found = plan.cell[r][c];
    if (found == nullptr) return;
    {
            Block* b = found;
            if (m.param < 0)
            {
                if (m.toggle) { if (press) b->enabled = ! b->enabled.load(); }
                else b->enabled = value >= 64;
                return;
            }
            if (m.param >= (int) b->params.size()) return;
            auto& prm = *b->params[(size_t) m.param];
            if (m.toggle)
            {
                if (! press) return;
                if (prm.kind == Param::Kind::Toggle) prm.set (prm.on() ? 0.0f : 1.0f);
                else if (prm.kind == Param::Kind::Choice) prm.set ((float) ((prm.getInt() + 1) % juce::jmax (1, prm.choices.size())));
                else prm.setNorm (prm.getNorm() > 0.5f ? 0.0f : 1.0f);
            }
            else if (prm.kind == Param::Kind::Toggle) prm.set (value >= 64 ? 1.0f : 0.0f);
            else
            {
                float v = (float) value / 127.0f;
                if (m.curve == 1) v = v * v; else if (m.curve == 2) v = std::sqrt (v);
                prm.setNorm (m.lo + (m.hi - m.lo) * v);
            }
            return;
        }
}

void Engine::handleMidi (juce::MidiBuffer& midi, const Plan& plan)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isMidiStart()) { Block::context().running = true; ++Block::context().starts; continue; }
        if (msg.isMidiContinue()) { Block::context().running = true; continue; }
        if (msg.isMidiStop()) { Block::context().running = false; continue; }
        int kind = -1, num = 0, val = 0;
        if (msg.isController()) { kind = kindCC; num = msg.getControllerNumber(); val = msg.getControllerValue(); }
        else if (msg.isNoteOn()) { kind = kindNote; num = msg.getNoteNumber(); val = juce::jmax (64, (int) msg.getVelocity()); }
        else if (msg.isNoteOff()) { kind = kindNote; num = msg.getNoteNumber(); val = 0; }
        else if (msg.isProgramChange()) { kind = kindProgram; num = msg.getProgramChangeNumber(); val = 127; }
        if (kind < 0) continue;

        lastKind = kind; lastNum = num; lastVal = val; lastChannel = msg.getChannel();
        if (kind == kindCC) lastCc = num;

        // the emergency button's own switch (learned separately, so nothing else changes)
        if (panicLearn.load())
        {
            if (panicLearnStage.load() == 0) { panicCandKind = kind; panicCandNum = num; panicSawRelease = false; panicLearnStage = 1; }
            else if (panicCandKind.load() == kind && panicCandNum.load() == num && val < 64) panicSawRelease = true;
            continue;
        }
        const bool panicHit = kind == panicKind.load() && num == panicNum.load();
        if (panicHit)
        {
            if (panicLatch.load()) panicSetFromMidi = val >= 64 ? 1 : 0;
            else if (val >= 64) panicFromMidi = true;
        }

        if (learnArmed.load())
        {
            if (learnStage.load() == 0)
            {
                learnedKind = kind; learnedNum = num;
                learnSawRelease = false;
                learnStage = 1;
            }
            else if (learnedKind.load() == kind && learnedNum.load() == num && val < 64)
                learnSawRelease = true;
            continue;
        }

        bool used = false;
        for (auto& m : mappings)
            if (m.kind == kind && m.number == num) { applyMapping (m, val, val >= 64, plan); used = true; }
        if (kind == kindProgram && ! used && ! panicHit) pendingProgram = num;   // unlearned program changes switch presets
    }
}

//==============================================================================
void Engine::process (juce::AudioBuffer<float>& buffer, int numIn, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, n);
    if (n > maxBlock) { buffer.clear(); return; }

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked()) { buffer.clear(); return; }   // only while the chain is being edited
    const Plan& plan = live;
    handleMidi (midi, plan);

    float* mL = buf[0][0].data();
    float* mR = buf[0][1].data();
    const float inG = dbToGain (inputGainDb.load());
    // the tuner hears the real strings, before any transpose
    if (tunerOn.load())
    {
        const int ch = juce::jlimit (0, juce::jmax (0, numIn - 1), tunerInput.load() - 1);
        int w = tunerWrite.load();
        for (int i = 0; i < n; ++i) { tunerBuf[w] = numIn > 0 ? buffer.getReadPointer (ch)[i] : 0.0f; w = (w + 1) & (tunerSize - 1); }
        tunerWrite = w;
    }

    // transpose: shift each interface input once, before any chain reads it
    const float semis = (float) transposeSemis.load() + transposeCents.load() * 0.01f;
    if (std::abs (semis) > 0.001f && numIn > 0)
    {
        const float ratio = std::pow (2.0f, semis / 12.0f);
        for (int ch = 0; ch < juce::jmin (2, numIn); ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i) d[i] = shifter[ch].process (d[i], ratio);
        }
    }
    auto inputSample = [&] (int input, int i) -> float
    {
        const int ch = juce::jlimit (0, juce::jmax (0, numIn - 1), input - 1);
        return numIn > 0 ? buffer.getReadPointer (ch)[i] * inG : 0.0f;
    };
    for (int k = 0; k < 2; ++k)
    {
        float pk = 0;
        if (k < numIn) for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (buffer.getReadPointer (k)[i] * inG));
        inputPeaks[k] = std::max (pk, inputPeaks[k].load() * 0.9f);
    }
    inputPeak = std::max (inputPeaks[0].load(), inputPeaks[1].load());

    bool independent[rows];
    for (int r = 0; r < rows; ++r) independent[r] = rowIsIndependent (r);
    bool continues[rows];
    for (int r = 0; r < rows; ++r) continues[r] = rowContinues (r);

    // the clean interface inputs, for instrument blocks that track your dry guitar wherever they sit
    for (int k = 0; k < 2; ++k)
    {
        float* d = cleanBuf[k].data();
        for (int i = 0; i < n; ++i) d[i] = inputSample (k + 1, i);
    }
    const float* cleanFor[rows];
    for (int r = 0; r < rows; ++r)
    {
        const int input = independent[r] ? rowInput[r].load() : rowInput[0].load();
        cleanFor[r] = cleanBuf[juce::jlimit (1, 2, input) - 1].data();
    }
    for (int t = 0; t < 2; ++t)
    {
        auto& tap = SpectrumTap::get (t);
        if (tap.on.load() && tap.where.load() == SpectrumTap::Input) tap.push (cleanFor[0], cleanFor[0], n);
    }

    // main row (and any row with its own input) starts from its interface input
    for (int r = 0; r < rows; ++r)
    {
        if (! independent[r] || (r > 0 && ! plan.active[r])) continue;
        const int input = juce::jlimit (1, 2, rowInput[r].load());
        float* l = buf[r][0].data();
        float* rr = buf[r][1].data();
        for (int i = 0; i < n; ++i) l[i] = rr[i] = inputSample (input, i);   // a guitar input is mono
    }

    auto runCab = [&] (float* l, float* r_)
    {
        if (auto* cb = plan.rigCab) { cb->cleanInput = cleanFor[0]; cb->interfaceIn[0] = cleanBuf[0].data(); cb->interfaceIn[1] = cleanBuf[1].data(); cb->interfaceInputs = numIn; cb->run (l, r_, n); }
    };
    if (plan.cabRow == 0 && plan.cabCol < 0) runCab (mL, mR);
    for (int c = 0; c < cols; ++c)
    {
        for (int r = 1; r < rows; ++r)
            if (! independent[r] && ! continues[r] && plan.active[r] && plan.first[r] == c)
            {
                std::copy (mL, mL + n, buf[r][0].begin());
                std::copy (mR, mR + n, buf[r][1].begin());
            }

        if (auto* b = plan.cell[0][c]) { b->cleanInput = cleanFor[0]; b->interfaceIn[0] = cleanBuf[0].data(); b->interfaceIn[1] = cleanBuf[1].data(); b->interfaceInputs = numIn; b->run (mL, mR, n); }
        if (plan.cabRow == 0 && plan.cabCol == c) runCab (mL, mR);
        for (int r = 1; r < rows; ++r)
        {
            if (! plan.active[r] || plan.cell[r][c] == nullptr || continues[r]) continue;
            if (independent[r] || (c >= plan.first[r] && c <= plan.last[r]))
            {
                plan.cell[r][c]->cleanInput = cleanFor[r];
                plan.cell[r][c]->interfaceIn[0] = cleanBuf[0].data(); plan.cell[r][c]->interfaceIn[1] = cleanBuf[1].data();
                plan.cell[r][c]->interfaceInputs = numIn;
                plan.cell[r][c]->run (buf[r][0].data(), buf[r][1].data(), n);
                if (plan.cabRow == r && plan.cabCol == c) runCab (buf[r][0].data(), buf[r][1].data());
            }
        }

        bool mainScaled = false;
        for (int r = 1; r < rows; ++r)
        {
            if (independent[r] || continues[r] || ! plan.active[r] || plan.last[r] != c) continue;
            auto& m = merges[r];
            if (! mainScaled)
            {
                const float g = dbToGain (m.mainLevel.load()), pan = m.mainPan.load();
                const float gl = g * std::min (1.0f, 1.0f - pan), gr = g * std::min (1.0f, 1.0f + pan);
                for (int i = 0; i < n; ++i) { mL[i] *= gl; mR[i] *= gr; }
                mainScaled = true;
            }
            const float g = dbToGain (m.branchLevel.load()), pan = m.branchPan.load();
            const float gl = g * std::min (1.0f, 1.0f - pan), gr = g * std::min (1.0f, 1.0f + pan);
            const float* bl = buf[r][0].data();
            const float* br = buf[r][1].data();
            for (int i = 0; i < n; ++i) { mL[i] += bl[i] * gl; mR[i] += br[i] * gr; }
        }
    }

    // rows set to "continue": the main chain carries on through them, in order
    for (int r = 1; r < rows; ++r)
    {
        if (! continues[r] || ! plan.active[r]) continue;
        for (int c = 0; c < cols; ++c)
            if (auto* b = plan.cell[r][c]) { b->cleanInput = cleanFor[0]; b->interfaceIn[0] = cleanBuf[0].data(); b->interfaceIn[1] = cleanBuf[1].data(); b->interfaceInputs = numIn; b->run (mL, mR, n); if (plan.cabRow == r && plan.cabCol == c) runCab (mL, mR); }
    }

    // separate rigs (own input) are mixed in at the end with their level and pan
    for (int r = 1; r < rows; ++r)
    {
        if (! independent[r] || ! plan.active[r]) continue;
        auto& m = merges[r];
        const float g = dbToGain (m.branchLevel.load()), pan = m.branchPan.load();
        const float gl = g * std::min (1.0f, 1.0f - pan), gr = g * std::min (1.0f, 1.0f + pan);
        const float* bl = buf[r][0].data();
        const float* br = buf[r][1].data();
        for (int i = 0; i < n; ++i) { mL[i] += bl[i] * gl; mR[i] += br[i] * gr; }
    }

    // spillover tails from the previous preset, fed silence and faded out
    if (plan.numSpill > 0)
    {
        float* zl = spillBuf[0].data(); float* zr = spillBuf[1].data();
        for (int k = 0; k < plan.numSpill; ++k)
        {
            std::fill (zl, zl + n, 0.0f); std::fill (zr, zr + n, 0.0f);
            plan.spill[k]->run (zl, zr, n);
            const float g = spillGain;
            for (int i = 0; i < n; ++i) { mL[i] += zl[i] * g; mR[i] += zr[i] * g; }
        }
    }

    // global output EQ
    if (outEqVersion.load() != eqVersionSeen)
    {
        eqVersionSeen = outEqVersion.load();
        const float sr = (float) sampleRate;
        float v[7]; for (int k = 0; k < 7; ++k) v[k] = outEq[k].load();
        eqActive = v[0] > 21.0f || v[6] < 19500.0f;
        for (int k = 1; k <= 5; ++k) if (std::abs (v[k]) > 0.05f) eqActive = true;
        for (int c = 0; c < 2; ++c)
        {
            eqF[c][0].set (ndsp::Biquad::Type::HighPass, sr, std::max (20.0f, v[0]), 0.707f);
            eqF[c][1].set (ndsp::Biquad::Type::LowShelf, sr, 100.0f, 0.7f, v[1]);
            eqF[c][2].set (ndsp::Biquad::Type::Peak, sr, 400.0f, 0.9f, v[2]);
            eqF[c][3].set (ndsp::Biquad::Type::Peak, sr, 1500.0f, 0.9f, v[3]);
            eqF[c][4].set (ndsp::Biquad::Type::Peak, sr, 4000.0f, 0.9f, v[4]);
            eqF[c][5].set (ndsp::Biquad::Type::HighShelf, sr, 9000.0f, 0.7f, v[5]);
            eqF[c][6].set (ndsp::Biquad::Type::LowPass, sr, std::min (v[6], sr * 0.45f), 0.707f);
        }
    }
    if (eqActive)
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 7; ++k) { mL[i] = eqF[0][k].process (mL[i]); mR[i] = eqF[1][k].process (mR[i]); }

    // loudness, before any level-matching trim
    {
        if (kwRate != (float) sampleRate)
        {
            kwRate = (float) sampleRate;
            for (auto& c : kw) { c[0].set (ndsp::Biquad::Type::HighShelf, kwRate, 1681.0f, 0.707f, 4.0f); c[1].set (ndsp::Biquad::Type::HighPass, kwRate, 38.0f, 0.5f); }
        }
        const int sc = juce::jlimit (0, numScenes - 1, currentScene.load());
        if (sc != loudScene) { loudScene = sc; loudSkip = (int) (0.5 * sampleRate); subCount = 0; subAcc = 0; subN = 0; }   // skip the old scene's tails
        const int sub = (int) (0.1 * sampleRate);
        for (int i = 0; i < n; ++i)
        {
            const float l = kw[0][1].process (kw[0][0].process (mL[i])), r = kw[1][1].process (kw[1][0].process (mR[i]));
            if (loudSkip > 0) { --loudSkip; continue; }
            subAcc += (double) l * l + (double) r * r;   // sum of both channels, as BS.1770 does
            if (++subN < sub) continue;
            subE[subCount & 3] = subAcc / subN; ++subCount; subAcc = 0; subN = 0;
            if (subCount < 4) continue;
            const double e = 0.25 * (subE[0] + subE[1] + subE[2] + subE[3]);
            const float lufs = (float) (-0.691 + 10.0 * std::log10 (e + 1.0e-12));
            auto bin = [] (float v) { return juce::jlimit (0, loudBins - 1, (int) ((v + 70.0f) * 2.0f)); };
            if (lufs > -70.0f)
            {
                ++loudHist[sc][bin (lufs)];
                ++loudHist[presetMeter][bin (lufs + sceneTrim (sc))];
            }
        }
    }

    // output
    const float trimTarget = dbToGain (presetTrimDb.load() + sceneTrim (currentScene.load()));
    const float outG = dbToGain (outputGainDb.load());
    const bool mono = monoOut.load();
    float pkL = 0, pkR = 0;
    for (int i = 0; i < n; ++i)
    {
        trimSm += 0.0005f * (trimTarget - trimSm);
        float l = mL[i] * outG * trimSm, r = mR[i] * outG * trimSm;
        if (mono) l = r = 0.5f * (l + r);
        // look-ahead limiter: the gain comes down smoothly just before a peak arrives, so nothing clips
        {
            constexpr float ceiling = 0.841f;   // -1.5 dBFS
            const int D = limLen;
            const float pk = std::max (std::abs (l), std::abs (r));
            limReq[limReqPos] = pk > ceiling ? ceiling / pk : 1.0f;      // window of D + 1 gains
            limReqPos = (limReqPos + 1) % (D + 1);
            float mn = 1.0f;
            for (int k = 0; k <= D; ++k) mn = std::min (mn, limReq[k]);
            // average of the last D minimums: always at or below what the delayed sample needs
            limBox += mn - limMin[limPos];
            limMin[limPos] = mn;
            const float outL = limL[limPos], outR = limR[limPos];
            limL[limPos] = l; limR[limPos] = r;
            limPos = (limPos + 1) % D;
            const float g = (float) std::min (1.0, limBox / D);
            limRel = g < limRel ? g : limRel + (g - limRel) * limRelCoef;   // ~60 ms release
            l = outL * limRel; r = outR * limRel;
            l = juce::jlimit (-0.99f, 0.99f, l); r = juce::jlimit (-0.99f, 0.99f, r);   // last guard, never reached
        }
        mL[i] = l; mR[i] = r;
        pkL = std::max (pkL, std::abs (l)); pkR = std::max (pkR, std::abs (r));
    }
    limiterReductionDb = -juce::Decibels::gainToDecibels (limRel);
    limiterHoldDb = std::max (limiterHoldDb.load(), limiterReductionDb.load());
    inputPeakHold = std::max (inputPeakHold.load(), inputPeak.load());
    outputPeakL = std::max (pkL, outputPeakL.load() * 0.9f);
    outputPeakR = std::max (pkR, outputPeakR.load() * 0.9f);

    // metronome click (added after the meters so it never affects levels you set by ear)
    if (metronomeOn.load())
    {
        const double beatsPerSample = Block::context().bpm.load() / 60.0 / sampleRate;
        const float lvl = dbToGain (metronomeLevelDb.load());
        const int bpb = std::max (1, beatsPerBar.load());
        for (int i = 0; i < n; ++i)
        {
            const double before = beatPos;
            beatPos += beatsPerSample;
            if (std::floor (beatPos) != std::floor (before))
            {
                const int beat = (int) std::floor (beatPos) % bpb;
                clickFreq = beat == 0 ? 1600.0f : 1000.0f;
                clickLeft = clickSamples;
                clickPhase = 0;
            }
            if (clickLeft > 0)
            {
                const float env = (float) clickLeft / (float) clickSamples;
                const float s = std::sin (clickPhase) * env * env * lvl;
                clickPhase += twoPi * clickFreq / (float) sampleRate;
                --clickLeft;
                mL[i] += s; mR[i] += s;
            }
        }
    }
    else beatPos = 0;

    for (int t = 0; t < 2; ++t)
    {
        auto& tap = SpectrumTap::get (t);
        if (tap.on.load() && tap.where.load() == SpectrumTap::Output) tap.push (mL, mR, n);
    }
    if (tunerOn.load() && tunerMute.load()) { std::fill (mL, mL + n, 0.0f); std::fill (mR, mR + n, 0.0f); }
    // emergency bypass: your dry guitar straight to the output, or silence (short fades, no clicks)
    {
        const int pm = panic.load();
        const float tDry = pm == 1 ? 1.0f : 0.0f, tMute = pm == 2 ? 1.0f : 0.0f;
        if (pm != 0 || panicSm > 0.0001f || panicMuteSm > 0.0001f)
        {
            const float* dry = cleanFor[0];
            for (int i = 0; i < n; ++i)
            {
                panicSm += 0.003f * (tDry - panicSm);
                panicMuteSm += 0.003f * (tMute - panicMuteSm);
                const float keep = (1.0f - panicSm) * (1.0f - panicMuteSm);
                const float d = juce::jlimit (-0.99f, 0.99f, dry[i]) * panicSm;
                mL[i] = mL[i] * keep + d;
                mR[i] = mR[i] * keep + d;
            }
        }
    }
    OutputRing::get().push (mL, mR, n);   // what goes to the speakers (the talk box's echo cancellation)

    const int numOut = buffer.getNumChannels();
    if (numOut == 1)
    {
        auto* d = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) d[i] = 0.5f * (mL[i] + mR[i]);
    }
    else
    {
        std::copy (mL, mL + n, buffer.getWritePointer (0));
        std::copy (mR, mR + n, buffer.getWritePointer (1));
        for (int c = 2; c < numOut; ++c) buffer.clear (c, 0, n);
    }
}

//==============================================================================
juce::var Engine::toVar() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> blocks;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (auto& b = cells[(size_t) r][(size_t) c])
            {
                auto v = b->toVar();
                v.getDynamicObject()->setProperty ("row", r);
                v.getDynamicObject()->setProperty ("col", c);
                v.getDynamicObject()->setProperty ("id", b->id);
                blocks.add (v);
            }
    o->setProperty ("blocks", blocks);
    if (cabStudio != nullptr)
    {
        auto v = cabStudio->toVar();
        v.getDynamicObject()->setProperty ("id", cabStudio->id);
        o->setProperty ("cabStudio", v);
    }

    juce::Array<juce::var> ms;
    for (int r = 1; r < rows; ++r)
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("mainLevel", merges[r].mainLevel.load());
        m->setProperty ("mainPan", merges[r].mainPan.load());
        m->setProperty ("branchLevel", merges[r].branchLevel.load());
        m->setProperty ("branchPan", merges[r].branchPan.load());
        ms.add (juce::var (m));
    }
    o->setProperty ("merges", ms);
    o->setProperty ("bpm", getBpm());
    o->setProperty ("transposeSemis", transposeSemis.load());
    o->setProperty ("transposeCents", transposeCents.load());
    juce::Array<juce::var> ins;
    for (int r = 0; r < rows; ++r) ins.add (rowInput[r].load());
    o->setProperty ("rowInputs", ins);

    juce::Array<juce::var> maps;
    for (auto& m : mappings)
        if (m.kind >= 0)
        {
            auto* mo = new juce::DynamicObject();
            mo->setProperty ("kind", m.kind);
            mo->setProperty ("number", m.number);
            mo->setProperty ("toggle", m.toggle);
            mo->setProperty ("lo", m.lo); mo->setProperty ("hi", m.hi); mo->setProperty ("curve", m.curve);
            mo->setProperty ("block", m.blockId);
            mo->setProperty ("param", m.param);
            maps.add (juce::var (mo));
        }
    o->setProperty ("midi", maps);

    juce::Array<juce::var> scs;
    for (int i = 0; i < numScenes; ++i)
    {
        SceneState sc = scenes[(size_t) i];
        if (i == currentScene.load()) captureScene (sc);   // the scene you are on, as it is now
        auto* so = new juce::DynamicObject();
        so->setProperty ("valid", sc.valid);
        so->setProperty ("name", sc.name);
        so->setProperty ("trim", sc.trimDb);
        juce::Array<juce::var> bl;
        for (auto& [bid, st] : sc.blocks)
        {
            auto* bo = new juce::DynamicObject();
            bo->setProperty ("id", bid);
            bo->setProperty ("on", st.first);
            juce::Array<juce::var> vals;
            for (float f : st.second) vals.add (f);
            bo->setProperty ("v", vals);
            bl.add (juce::var (bo));
        }
        so->setProperty ("blocks", bl);
        scs.add (juce::var (so));
    }
    o->setProperty ("scenes", scs);
    o->setProperty ("scene", currentScene.load());
    o->setProperty ("presetTrim", presetTrimDb.load());
    return juce::var (o);
}

void Engine::fromVar (const juce::var& v)
{
    // spillover: the outgoing rig's delay / reverb / freeze tails keep ringing for a few seconds
    if (spilloverOn.load())
    {
        spillBlocks.clear();
        for (auto& row : cells)
            for (auto& b : row)
                if (b != nullptr && b->enabled.load() && b->hasTails() && spillBlocks.size() < 8) spillBlocks.push_back (b);
        spillStarted = juce::Time::getMillisecondCounterHiRes();
        spillGain = spillBlocks.empty() ? 0.0f : 1.0f;
    }
    for (int m = 0; m <= presetMeter; ++m) resetLoudness (m);
    for (auto& row : cells) for (auto& b : row) b = nullptr;
    std::map<int, int> idMap;   // saved id -> new id (MIDI mappings refer to blocks by id)

    if (auto* arr = v.getProperty ("blocks", {}).getArray())
        for (auto& bv : *arr)
        {
            const int r = juce::jlimit (0, rows - 1, (int) bv.getProperty ("row", 0));
            const int c = juce::jlimit (0, cols - 1, (int) bv.getProperty ("col", 0));
            auto made = Block::fromVar (bv);   // loadExtra needs a prepared block, so prepare first
            if (made == nullptr) continue;
            std::shared_ptr<Block> b (made.release());
            b->prepareBlock (sampleRate, maxBlock);
            b->loadExtra (bv.getProperty ("extra", {}));
            idMap[(int) bv.getProperty ("id", 0)] = b->id;
            cells[(size_t) r][(size_t) c] = b;
        }
    cabStudio = nullptr;
    {
        const auto cv = v.getProperty ("cabStudio", {});
        if (cv.isObject())
            if (auto made = Block::fromVar (cv))
            {
                std::shared_ptr<Block> b (made.release());
                b->prepareBlock (sampleRate, maxBlock);
                b->loadExtra (cv.getProperty ("extra", {}));
                idMap[(int) cv.getProperty ("id", 0)] = b->id;
                cabStudio = b;
            }
    }

    if (auto* arr = v.getProperty ("merges", {}).getArray())
        for (int r = 1; r < rows && r - 1 < arr->size(); ++r)
        {
            const juce::var m = (*arr)[r - 1];
            merges[r].mainLevel = (float) m.getProperty ("mainLevel", 0.0f);
            merges[r].mainPan = (float) m.getProperty ("mainPan", 0.0f);
            merges[r].branchLevel = (float) m.getProperty ("branchLevel", 0.0f);
            merges[r].branchPan = (float) m.getProperty ("branchPan", 0.0f);
        }
    if (auto* ins = v.getProperty ("rowInputs", {}).getArray())
        for (int r = 0; r < rows && r < ins->size(); ++r) rowInput[r] = juce::jlimit (r == 0 ? 1 : 0, r == 0 ? 2 : 3, (int) (*ins)[r]);
    else for (int r = 0; r < rows; ++r) rowInput[r] = r == 0 ? 1 : 0;
    transposeSemis = juce::jlimit (-12, 12, (int) v.getProperty ("transposeSemis", 0));
    transposeCents = juce::jlimit (-50.0f, 50.0f, (float) v.getProperty ("transposeCents", 0.0f));
    if (v.hasProperty ("bpm")) setBpm ((float) v.getProperty ("bpm", 120.0f));

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& m : mappings) m = {};
        if (auto* arr = v.getProperty ("midi", {}).getArray())
        {
            int k = 0;
            for (auto& mv : *arr)
            {
                if (k >= maxMappings) break;
                int bid = (int) mv.getProperty ("block", 0);
                if (bid > 0) { auto it = idMap.find (bid); if (it == idMap.end()) continue; bid = it->second; }
                Mapping m;
                if (mv.hasProperty ("kind")) { m.kind = (int) mv.getProperty ("kind", -1); m.number = (int) mv.getProperty ("number", 0); }
                else { m.kind = (int) mv.getProperty ("cc", -1) >= 0 ? kindCC : -1; m.number = (int) mv.getProperty ("cc", 0); }   // older presets
                m.blockId = bid;
                m.param = (int) mv.getProperty ("param", 0);
                m.toggle = (int) mv.getProperty ("toggle", 0);
                m.lo = (float) mv.getProperty ("lo", 0.0f); m.hi = (float) mv.getProperty ("hi", 1.0f); m.curve = (int) mv.getProperty ("curve", 0);
                mappings[(size_t) k++] = m;
            }
        }
    }
    resetScenes();
    presetTrimDb = (float) v.getProperty ("presetTrim", 0.0f);
    if (auto* arr = v.getProperty ("scenes", {}).getArray())
    {
        for (int i = 0; i < numScenes && i < arr->size(); ++i)
        {
            const juce::var so = (*arr)[i];
            auto& sc = scenes[(size_t) i];
            sc.valid = (bool) so.getProperty ("valid", false);
            sc.name = so.getProperty ("name", "").toString();
            sc.trimDb = (float) so.getProperty ("trim", 0.0f);
            if (auto* bl = so.getProperty ("blocks", {}).getArray())
                for (auto& bo : *bl)
                {
                    auto it = idMap.find ((int) bo.getProperty ("id", 0));
                    if (it == idMap.end()) continue;
                    std::vector<float> vals;
                    if (auto* va = bo.getProperty ("v", {}).getArray()) for (auto& f : *va) vals.push_back ((float) f);
                    sc.blocks[it->second] = { (bool) bo.getProperty ("on", true), std::move (vals) };
                }
        }
        currentScene = juce::jlimit (0, numScenes - 1, (int) v.getProperty ("scene", 0));
        if (scenes[(size_t) currentScene.load()].valid) applyScene (scenes[(size_t) currentScene.load()]);
    }
    publish();
}
