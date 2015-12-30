//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <cstdlib>
#include <iterator>
#include <memory>

// Some basic tests, to keep an eye on things and make sure these types work ok
// on all platforms.

static_assert (sizeof (std::intptr_t) == sizeof (void*), "std::intptr_t must be the same size as void*");

static_assert (sizeof (std::int8_t) == 1,   "std::int8_t must be exactly 1 byte!");
static_assert (sizeof (std::int16_t) == 2,  "std::int16_t must be exactly 2 bytes!");
static_assert (sizeof (std::int32_t) == 4,  "std::int32_t must be exactly 4 bytes!");
static_assert (sizeof (std::int64_t) == 8,  "std::int64_t must be exactly 8 bytes!");

static_assert (sizeof (std::uint8_t) == 1,  "std::uint8_t must be exactly 1 byte!");
static_assert (sizeof (std::uint16_t) == 2, "std::uint16_t must be exactly 2 bytes!");
static_assert (sizeof (std::uint32_t) == 4, "std::uint32_t must be exactly 4 bytes!");
static_assert (sizeof (std::uint64_t) == 8, "std::uint64_t must be exactly 8 bytes!");

namespace beast
{
//==============================================================================
#if BEAST_LINUX
namespace LinuxStatsHelpers
{
    String getCpuInfo (const char* const key)
    {
        StringArray lines;
        File cpuInfo ("/proc/cpuinfo");
        
        if (cpuInfo.existsAsFile())
        {
            FileInputStream in (cpuInfo);
            
            if (in.openedOk())
                lines.addLines (in.readEntireStreamAsString());
        }
        
        for (int i = lines.size(); --i >= 0;) // (NB - it's important that this runs in reverse order)
            if (lines[i].startsWithIgnoreCase (key))
                return lines[i].fromFirstOccurrenceOf (":", false, false).trim();
        
        return String::empty;
    }
}
#endif // BEAST_LINUX

struct CPUInformation
{
    CPUInformation() noexcept
        : hasMMX (false), hasSSE (false),
          hasSSE2 (false), hasSSE3 (false), has3DNow (false),
          hasSSE4 (false), hasAVX (false), hasAVX2 (false)
    {
#if BEAST_LINUX
        const String flags (LinuxStatsHelpers::getCpuInfo ("flags"));
        hasMMX = flags.contains ("mmx");
        hasSSE = flags.contains ("sse");
        hasSSE2 = flags.contains ("sse2");
        hasSSE3 = flags.contains ("sse3");
        has3DNow = flags.contains ("3dnow");
        hasSSE4 = flags.contains ("sse4_1") || flags.contains ("sse4_2");
        hasAVX = flags.contains ("avx");
        hasAVX2 = flags.contains ("avx2");
#endif // BEAST_LINUX
    }

    bool hasMMX, hasSSE, hasSSE2, hasSSE3, has3DNow, hasSSE4, hasAVX, hasAVX2;
};

static const CPUInformation& getCPUInformation() noexcept
{
    static CPUInformation info;
    return info;
}

bool hasMMX() noexcept { return getCPUInformation().hasMMX; }
bool hasSSE() noexcept { return getCPUInformation().hasSSE; }
bool hasSSE2() noexcept { return getCPUInformation().hasSSE2; }
bool hasSSE3() noexcept { return getCPUInformation().hasSSE3; }
bool has3DNow() noexcept { return getCPUInformation().has3DNow; }
bool hasSSE4() noexcept { return getCPUInformation().hasSSE4; }
bool hasAVX() noexcept { return getCPUInformation().hasAVX; }
bool hasAVX2() noexcept { return getCPUInformation().hasAVX2; }

//==============================================================================
std::vector <std::string>
getStackBacktrace()
{
    std::vector <std::string> result;

#if BEAST_ANDROID || BEAST_MINGW || BEAST_BSD
    bassertfalse; // sorry, not implemented yet!

#elif BEAST_WINDOWS
    HANDLE process = GetCurrentProcess();
    SymInitialize (process, nullptr, TRUE);

    void* stack[128];
    int frames = (int) CaptureStackBackTrace (0,
        std::distance(std::begin(stack), std::end(stack)),
        stack, nullptr);

    HeapBlock<SYMBOL_INFO> symbol;
    symbol.calloc (sizeof (SYMBOL_INFO) + 256, 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof (SYMBOL_INFO);

    for (int i = 0; i < frames; ++i)
    {
        DWORD64 displacement = 0;

        if (SymFromAddr (process, (DWORD64) stack[i], &displacement, symbol))
        {
            std::string frame;

            frame.append (std::to_string (i) + ": ");

            IMAGEHLP_MODULE64 moduleInfo;
            zerostruct (moduleInfo);
            moduleInfo.SizeOfStruct = sizeof (moduleInfo);

            if (::SymGetModuleInfo64 (process, symbol->ModBase, &moduleInfo))
            {
                frame.append (moduleInfo.ModuleName);
                frame.append (": ");
            }

            frame.append (symbol->Name);

            if (displacement)
            {
                frame.append ("+");
                frame.append (std::to_string (displacement));
            }

            result.push_back (frame);
        }
    }

#else
    void* stack[128];
    int frames = backtrace (stack,
        std::distance(std::begin(stack), std::end(stack)));

    std::unique_ptr<char*[], void(*)(void*)> frame (
        backtrace_symbols (stack, frames), std::free);

    for (int i = 0; i < frames; ++i)
        result.push_back (frame[i]);
#endif

    return result;
}

} // beast
