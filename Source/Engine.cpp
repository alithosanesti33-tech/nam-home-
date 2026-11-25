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
    loadMeasurer.reset (sr, maxBlock);
    for (auto& row : cells)
        for (auto& b : row)
            if (b != nullptr) b->prepareBlock (sr, maxBlock);
    clickSamples = (int) (0.025 * sr);
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
    std::vector<std::shared_ptr<Block>> alive;
    for (auto& row : cells) for (auto& b : row) if (b != nullptr) alive.push_back (b);

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

void Engine::move (int r1, int c1, int r2, int c2)
{
    std::swap (cells[(size_t) r1][(size_t) c1], cells[(size_t) r2][(size_t) c2]);
    publish();
}

void Engine::clearAll()
{
    for (auto& row : cells) for (auto& b : row) b = nullptr;
    for (auto& m : mappings) m = {};
    publish();
}

std::shared_ptr<Block> Engine::findById (int id) const
{
    for (auto& row : cells) for (auto& b : row) if (b != nullptr && b->id == id) return b;
    return nullptr;
}

bool Engine::findCell (int id, int& r, int& c) const
{
    for (r = 0; r < rows; ++r)
        for (c = 0; c < cols; ++c)
            if (cells[(size_t) r][(size_t) c] != nullptr && cells[(size_t) r][(size_t) c]->id == id) return true;
    return false;
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
    learnedCc = -1;
    learnArmed = true;
}

void Engine::clearMappingsFor (int blockId, int param)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& m : mappings)
        if (m.blockId == blockId && m.param == param) m = {};
}

juce::String Engine::describeMapping (int blockId, int param) const
{
    for (auto& m : mappings)
        if (m.cc >= 0 && m.blockId == blockId && m.param == param) return "CC " + juce::String (m.cc);
    return {};
}

void Engine::pollMessageThread()
{
    const int cc = learnedCc.exchange (-1);
    if (cc >= 0 && learnArmed.load())
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& m : mappings)   // one control per CC target: replace an existing mapping of this control
            if (m.blockId == learnBlock.load() && m.param == learnParam.load()) m = {};
        for (auto& m : mappings)
            if (m.cc < 0) { m.cc = cc; m.blockId = learnBlock.load(); m.param = learnParam.load(); break; }
        learnArmed = false;
    }
    if (tapFromMidi.exchange (false)) tapTempo();
    for (auto& row : cells)
        for (auto& b : row)
            if (auto* cab = dynamic_cast<CabBlock*> (b.get())) cab->updateAlignment();
}

void Engine::handleMidi (juce::MidiBuffer& midi, const Plan& plan)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isProgramChange()) { pendingProgram = msg.getProgramChangeNumber(); continue; }
        if (! msg.isController()) continue;
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();
        lastCc = cc;
        if (learnArmed.load()) { learnedCc = cc; continue; }

        for (auto& m : mappings)
        {
            if (m.cc != cc) continue;
            if (m.blockId == -1) { if (val >= 64) tapFromMidi = true; continue; }
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    Block* b = plan.cell[r][c];
                    if (b == nullptr || b->id != m.blockId) continue;
                    if (m.param < 0) b->enabled = val >= 64;
                    else if (m.param < (int) b->params.size()) b->params[(size_t) m.param]->setNorm ((float) val / 127.0f);
                }
        }
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

    if (tunerOn.load())
    {
        const int ch = juce::jlimit (0, juce::jmax (0, numIn - 1), tunerInput.load() - 1);
        int w = tunerWrite.load();
        for (int i = 0; i < n; ++i) { tunerBuf[w] = numIn > 0 ? buffer.getReadPointer (ch)[i] : 0.0f; w = (w + 1) & (tunerSize - 1); }
        tunerWrite = w;
    }

    bool independent[rows];
    for (int r = 0; r < rows; ++r) independent[r] = rowIsIndependent (r);

    // main row (and any row with its own input) starts from its interface input
    for (int r = 0; r < rows; ++r)
    {
        if (! independent[r] || (r > 0 && ! plan.active[r])) continue;
        const int input = juce::jlimit (1, 2, rowInput[r].load());
        float* l = buf[r][0].data();
        float* rr = buf[r][1].data();
        for (int i = 0; i < n; ++i) l[i] = rr[i] = inputSample (input, i);   // a guitar input is mono
    }

    for (int c = 0; c < cols; ++c)
    {
        for (int r = 1; r < rows; ++r)
            if (! independent[r] && plan.active[r] && plan.first[r] == c)
            {
                std::copy (mL, mL + n, buf[r][0].begin());
                std::copy (mR, mR + n, buf[r][1].begin());
            }

        if (auto* b = plan.cell[0][c]) b->run (mL, mR, n);
        for (int r = 1; r < rows; ++r)
        {
            if (! plan.active[r] || plan.cell[r][c] == nullptr) continue;
            if (independent[r] || (c >= plan.first[r] && c <= plan.last[r]))
                plan.cell[r][c]->run (buf[r][0].data(), buf[r][1].data(), n);
        }

        bool mainScaled = false;
        for (int r = 1; r < rows; ++r)
        {
            if (independent[r] || ! plan.active[r] || plan.last[r] != c) continue;
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

    // output
    const float outG = dbToGain (outputGainDb.load());
    const bool mono = monoOut.load();
    float pkL = 0, pkR = 0;
    for (int i = 0; i < n; ++i)
    {
        float l = mL[i] * outG, r = mR[i] * outG;
        if (mono) l = r = 0.5f * (l + r);
        // safety: transparent below -1 dBFS, rounds off anything hotter instead of hard clipping
        auto safe = [] (float v) { const float a = std::abs (v); return a < 0.89f ? v : std::copysign (0.89f + 0.11f * fastTanh ((a - 0.89f) / 0.11f), v); };
        l = safe (l); r = safe (r);
        mL[i] = l; mR[i] = r;
        pkL = std::max (pkL, std::abs (l)); pkR = std::max (pkR, std::abs (r));
    }
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

    if (tunerOn.load() && tunerMute.load()) { std::fill (mL, mL + n, 0.0f); std::fill (mR, mR + n, 0.0f); }

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
    juce::Array<juce::var> ins;
    for (int r = 0; r < rows; ++r) ins.add (rowInput[r].load());
    o->setProperty ("rowInputs", ins);

    juce::Array<juce::var> maps;
    for (auto& m : mappings)
        if (m.cc >= 0)
        {
            auto* mo = new juce::DynamicObject();
            mo->setProperty ("cc", m.cc);
            mo->setProperty ("block", m.blockId);
            mo->setProperty ("param", m.param);
            maps.add (juce::var (mo));
        }
    o->setProperty ("midi", maps);
    return juce::var (o);
}

void Engine::fromVar (const juce::var& v)
{
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
        for (int r = 0; r < rows && r < ins->size(); ++r) rowInput[r] = juce::jlimit (r == 0 ? 1 : 0, 2, (int) (*ins)[r]);
    else for (int r = 0; r < rows; ++r) rowInput[r] = r == 0 ? 1 : 0;
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
                mappings[(size_t) k++] = { (int) mv.getProperty ("cc", -1), bid, (int) mv.getProperty ("param", 0) };
            }
        }
    }
    publish();
}
