#include "MidiMap.h"

namespace loopstation
{

MidiBinding MidiBinding::unpack (uint32_t v) noexcept
{
    MidiBinding b;
    const auto t = (v >> 8) & 0xff;
    b.type = (t == 1) ? Type::Note : (t == 2) ? Type::CC : Type::None;
    b.number = static_cast<uint8_t> (v & 0x7f);
    return b;
}

std::string MidiBinding::describe() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    switch (type)
    {
        case Type::Note: return std::string ("Note ") + names[number % 12] + std::to_string (number / 12 - 2);
        case Type::CC:   return "CC " + std::to_string (number);
        case Type::None: break;
    }
    return "-";
}

MidiBinding MidiMap::defaultBinding (int action) noexcept
{
    // C1, D1, E1 in Logic's naming (notes 36, 38, 40).
    static const uint8_t notes[numLoopActions] = { 36, 38, 40 };
    MidiBinding b;
    b.type = MidiBinding::Type::Note;
    b.number = notes[action];
    return b;
}

MidiMap::MidiMap() noexcept
{
    for (int a = 0; a < numLoopActions; ++a)
        bindings[a].store (defaultBinding (a).pack());
}

MidiBinding MidiMap::get (int action) const noexcept
{
    return MidiBinding::unpack (bindings[action].load());
}

void MidiMap::set (int action, MidiBinding binding) noexcept
{
    if (action < 0 || action >= numLoopActions)
        return;

    if (binding.type != MidiBinding::Type::None)
        for (int a = 0; a < numLoopActions; ++a)
            if (a != action && get (a) == binding)
                bindings[a].store (MidiBinding().pack()); // one control, one action

    bindings[action].store (binding.pack());
}

int MidiMap::handleMessage (uint8_t status, uint8_t data1, uint8_t data2) noexcept
{
    const uint8_t kind = status & 0xf0;
    const bool noteOn = (kind == 0x90 && data2 > 0);
    const bool isCC   = (kind == 0xb0);

    if (! noteOn && ! isCC)
        return -1;

    const int learn = learnTarget.load();
    if (learn >= 0 && learn < numLoopActions)
    {
        if (noteOn || (isCC && data2 >= 64))
        {
            MidiBinding b;
            b.type = noteOn ? MidiBinding::Type::Note : MidiBinding::Type::CC;
            b.number = data1 & 0x7f;
            set (learn, b);
            if (isCC)
                ccHigh[learn] = true; // this press is consumed; wait for release
            learnTarget.store (-1);
        }
        return -1;
    }

    for (int a = 0; a < numLoopActions; ++a)
    {
        const auto b = get (a);

        if (noteOn && b.type == MidiBinding::Type::Note && b.number == data1)
            return a;

        if (isCC && b.type == MidiBinding::Type::CC && b.number == data1)
        {
            const bool high = data2 >= 64;
            const bool rising = high && ! ccHigh[a];
            ccHigh[a] = high;
            if (rising)
                return a;
            return -1;
        }
    }

    return -1;
}

} // namespace loopstation
