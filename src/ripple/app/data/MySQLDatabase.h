#ifndef RIPPLE_MYSQLDATABASE_H_INCLUDED
#define RIPPLE_MYSQLDATABASE_H_INCLUDED

#include <mysql.h>

namespace ripple {

class MySQLStatement;
    
class MySQLDatabase
    : public Database
    , private beast::LeakChecked <MySQLDatabase>
{
public:
    explicit MySQLDatabase (char const* host, std::uint32_t port, char const* username, char const* password, char const* database);
    ~MySQLDatabase ();

    void connect ();
    void disconnect ();

    // returns true if the query went ok
    bool executeSQL (const char* sql, bool fail_okay);

    // tells you how many rows were changed by an update or insert
    std::uint64_t getNumRowsAffected ();

    // returns false if there are no results
    bool startIterRows (bool finalize);
    void endIterRows ();

    // call this after you executeSQL
    // will return false if there are no more rows
    bool getNextRow (bool finalize);
    
    virtual bool beginTransaction() override;
    virtual bool endTransaction() override;

    bool getNull (int colIndex);
    char* getStr (int colIndex, std::string& retStr);
    std::int32_t getInt (int colIndex);
    float getFloat (int colIndex);
    bool getBool (int colIndex);
    // returns amount stored in buf
    int getBinary (int colIndex, unsigned char* buf, int maxSize);
    Blob getBinary (int colIndex);
    std::uint64_t getBigInt (int colIndex);
    
private:
    MySQLStatement *getStatement();
    //CARL: use this as temp, need a thread safe version
    MySQLStatement *mStmtTemp;
    
    MYSQL *mConnection;
    std::uint32_t mPort;
    std::string mUsername;
    std::string mPassword;
    std::string mDatabase;

    MySQLStatement *mCurrentStmt;
};
    
class MySQLStatement
{
public:
    MySQLStatement();
    ~MySQLStatement();

    MySQLDatabase* mDatabase;
    bool mMoreRows;
    std::vector <std::string> mColNameTable;
    MYSQL_RES *mResult;
    MYSQL_ROW mCurRow;
//    std::uint64_t mAffectedRows;
};

} // ripple

#endif
