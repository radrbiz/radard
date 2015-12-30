#ifndef RIPPLE_TX_ISSUEASSET_H_INCLUDED
#define RIPPLE_TX_ISSUEASSET_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

class IssueAsset
    : public Transactor
{
public:
    IssueAsset (ApplyContext& ctx)
        : Transactor(ctx)
    {
    }

    static
    TER
    preflight (PreflightContext const& ctx);

    static
    TER
    preclaim(PreclaimContext const& ctx);

    TER doApply () override;
};

} // ripple

#endif
