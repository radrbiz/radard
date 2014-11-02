namespace ripple {

class DividendVoteImpl : public DividendVote
{
private:
    template <typename Integer>
    class VotableInteger
    {
    public:
        VotableInteger (Integer current, Integer target)
            : mCurrent (current)
            , mTarget (target)
        {
            // Add our vote
            ++mVoteMap[mTarget];
        }

        bool
        mayVote () const
        {
            // If we love the current setting, we will not vote
            return mCurrent != mTarget;
        }

        void
        addVote (Integer vote)
        {
            ++mVoteMap[vote];
        }

        void
        noVote ()
        {
            addVote (mCurrent);
        }

        Integer
        getVotes ()
        {
            Integer ourVote = mCurrent;
            int weight = 0;

            typedef typename std::map<Integer, int>::value_type mapVType;
            for (auto const& e : mVoteMap)
            {
                // Take most voted value
                if (e.second > weight)
                {
                    ourVote = e.first;
                    weight = e.second;
                }
            }

            return ourVote;
        }

    private:
        Integer mCurrent;   // The current setting
        Integer mTarget;    // The setting we want
        std::map<Integer, int> mVoteMap;
    };

public:
    DividendVoteImpl(std::uint32_t targetDividendLedger, beast::Journal journal)
        : m_journal(journal)
    {
        mTargetDividendLedger = targetDividendLedger;
    }

    void doValidation(Ledger::ref lastClosedLedger, STObject& baseValidation) override
    {
        if (lastClosedLedger->getDividendLedger() != mTargetDividendLedger) {
            if (m_journal.info) m_journal.info <<
                "Voting for dividend ledger of " << mTargetDividendLedger;

            baseValidation.setFieldU32 (sfDividendLedger, mTargetDividendLedger);
        }
    }

    void doVoting(Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) override
    {
        std::uint32_t dividendLedger = lastClosedLedger->getDividendLedger();
        std::uint64_t dividendCoins = 0.003 * lastClosedLedger->getTotalCoins();

        VotableInteger<std::uint32_t> dividendLedgerVote(dividendLedger, mTargetDividendLedger);
        ValidationSet set = getApp().getValidations().getValidations(lastClosedLedger->getParentHash());

        SerializedTransaction trans(ttDIVIDEND);
        trans.setFieldAccount(sfAccount, Account());
        trans.setFieldU32(sfDividendLedger, dividendLedger);
        trans.setFieldU64(sfDividendCoins, dividendCoins);

        uint256 txID = trans.getTransactionID();

        if (m_journal.warning)
            m_journal.warning << "Vote: " << txID;

        Serializer s;
        trans.add (s, true);

        SHAMapItem::pointer tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());

        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            if (m_journal.warning) m_journal.warning <<
                "Ledger already had dividend";
        }
    }

private:
    beast::Journal m_journal;
};


std::unique_ptr<DividendVote>
make_DividendVote(std::uint32_t targetDividendLedger, beast::Journal journal)
{
    return std::make_unique<DividendVoteImpl>(targetDividendLedger, journal);
}

}
