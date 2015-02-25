#ifndef RIPPLE_NULLDATABASE_H_INCLUDED
#define RIPPLE_NULLDATABASE_H_INCLUDED

#include <beast/utility/LeakChecked.h>
#include <ripple/app/data/DatabaseCon.h>

namespace ripple {

class NullDatabase
    : public Database
    , private beast::LeakChecked <NullDatabase>
{
public:
    explicit NullDatabase ();
    ~NullDatabase ();

    void connect (){};
    void disconnect (){};

    // returns true if the query went ok
    bool executeSQL (const char* sql, bool fail_okay);
    bool executeSQLBatch();
    
    bool batchStart() override;
    bool batchCommit() override;

    // tells you how many rows were changed by an update or insert
    std::uint64_t getNumRowsAffected ();

    // returns false if there are no results
    bool startIterRows (bool finalize);
    void endIterRows ();

    // call this after you executeSQL
    // will return false if there are no more rows
    bool getNextRow (bool finalize);
    
    bool beginTransaction() override;
    bool endTransaction() override;

    bool getNull (int colIndex);
    char* getStr (int colIndex, std::string& retStr);
    std::int32_t getInt (int colIndex);
    float getFloat (int colIndex);
    bool getBool (int colIndex);
    // returns amount stored in buf
    int getBinary (int colIndex, unsigned char* buf, int maxSize);
    Blob getBinary (int colIndex);
    std::uint64_t getBigInt (int colIndex);
};
    
class NullDatabaseCon
    : public DatabaseCon
{
public:
    NullDatabaseCon()
    {
        mDatabase = new NullDatabase();
    }
};

} // ripple

#endif
