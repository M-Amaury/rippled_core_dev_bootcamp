#include <test/jtx.h>
#include <xrpl/beast/unit_test.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>

namespace ripple {
namespace test {

class MyTest_test : public beast::unit_test::suite
{

    
    void
    testSql(FeatureBitset features)
    {
        testcase("SQL Test");

        using namespace jtx;
        
        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(5000), alice, bob);
        env.close();

        auto const db = dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase());
        auto const ledgerSeq = db->getMaxLedgerSeq();
        BEAST_EXPECT(*ledgerSeq == 3);
    }

    void
    testSqlTxCount(FeatureBitset features)
    {
        testcase("SQL Test");

        using namespace jtx;
        
        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(5000), alice, bob);
        env.close();

        auto const db = dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase());
        auto const txCount = db->getTransactionCount();
        BEAST_EXPECT(txCount == 4);
    }



    void 
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        // testProtocol(sa);
        // testTraceTransactor(sa);
        testSql(sa);
        testSqlTxCount(sa);
    }

};
BEAST_DEFINE_TESTSUITE(MyTest, bootcamp, ripple);

}}