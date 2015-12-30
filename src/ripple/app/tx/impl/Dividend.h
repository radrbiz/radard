#ifndef RIPPLE_TX_DIVIDEND_H_INCLUDED
#define RIPPLE_TX_DIVIDEND_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

class Dividend
    : public Transactor
{
public:
    Dividend (ApplyContext& ctx)
        : Transactor(ctx)
    {
    }

    static
    TER
    preflight (PreflightContext const& ctx);

    TER doApply () override;
    void preCompute() override;

    static
    std::uint64_t
    calculateBaseFee (
        PreclaimContext const& ctx)
    {
        return 0;
    }

    static
    TER
    preclaim(PreclaimContext const &ctx);
    
private:
    TER startCalc ();
    TER applyTx ();
    TER doneApply ();

    bool updateDividendMap ();
};

} // ripple

#endif
