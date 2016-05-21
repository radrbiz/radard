#ifndef RIPPLE_CORE_SIGNALS_H_INCLUDED
#define RIPPLE_CORE_SIGNALS_H_INCLUDED

#include <boost/signals2/signal.hpp>

namespace ripple
{
struct AbortOnFalse
{
    typedef bool result_type;
    template <typename InputIterator>
    result_type operator() (InputIterator first, InputIterator last) const
    {
        for (; first != last; ++first)
        {
            if (!*first)
                return false;
        }
        return true;
    }
};
};

#endif