#ifndef RIPPLE_APP_DIVIDEND_H_INCLUDED
#define RIPPLE_APP_DIVIDEND_H_INCLUDED

namespace ripple {

class DividendVote
{
public:

    virtual ~DividendVote() {}

    virtual void doValidation (Ledger::ref lastClosedLedger,
                               STObject& baseValidation) = 0;

    virtual void doVoting (Ledger::ref lastClosedLedger,
                           SHAMap::ref initialPosition) = 0;

    void setTargetDividendLedger(std::uint32_t t)
    {
        mTargetDividendLedger = t;
    }

    std::uint32_t getTargetDividendLedger()
    {
        return mTargetDividendLedger;
    }

protected:
    std::uint32_t mTargetDividendLedger;
};

std::unique_ptr<DividendVote>
make_DividendVote(std::uint32_t targetDividendLedger, beast::Journal journal);

}

#endif
