#ifndef RIPPLE_APP_DIVIDEND_H_INCLUDED
#define RIPPLE_APP_DIVIDEND_H_INCLUDED

namespace ripple {

class DividendVote
{
public:

    virtual ~DividendVote() {}
    
    virtual bool isStartLedger(Ledger::ref ledger) = 0;

    virtual void doStartValidation (Ledger::ref lastClosedLedger,
                                    STObject& baseValidation) = 0;

    virtual void doStartVoting (Ledger::ref lastClosedLedger,
                                SHAMap::ref initialPosition) = 0;
    
    virtual bool isApplyLedger(Ledger::ref ledger) = 0;
    
    virtual void doApplyValidation (Ledger::ref lastClosedLedger,
                                    STObject& baseValidation) = 0;
    
    virtual bool doApplyVoting (Ledger::ref lastClosedLedger,
                                SHAMap::ref initialPosition) = 0;

};

std::unique_ptr<DividendVote>
make_DividendVote(beast::Journal journal);

}

#endif
