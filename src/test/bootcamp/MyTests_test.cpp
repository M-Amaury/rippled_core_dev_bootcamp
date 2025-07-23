#include <test/jtx.h>
#include <xrpl/beast/unit_test.h>
#include "test/jtx/escrow.h"
#include <xrpl/basics/chrono.h>

namespace ripple {
namespace test {

class MyTests_test : public beast::unit_test::suite
{
    void
    testProtocol(FeatureBitset const& features)
    {
        // add tests
        using namespace test::jtx;
        using namespace std::chrono_literals;
        Account const alice("alice");
        Account const bob("bob");

        Env env{*this, features};
        env.fund(XRP(10000), alice, bob);

        auto const preAlice = env.balance(alice);
        std::cout << "Pre-balance: " << preAlice << std::endl;
        env.close();

        auto jt = env.jt(escrow::create(alice, bob, XRP(1)),
            escrow::finish_time(env.now() + 3s),
            escrow::cancel_time(env.now() + 4s));
        Serializer s;
        jt.stx->add(s);
        auto const seq = jt.stx->getFieldU32(sfSequence);
        std::cout << "Serialized: " << seq << std::endl;
        BEAST_EXPECT(seq == 4);

        auto const amt = jt.stx->getFieldAmount(sfAmount);
        std::cout << "Amount: " << amt.getJson(JsonOptions::none) << std::endl;
        BEAST_EXPECT(amt == XRP(1));
        std::cout << "Transaction: " << jt.jv<< std::endl;
        env(jt);
        env.close();

        std::cout << "Post-balance: " << env.balance(alice) << std::endl;

        // auto const k = keylet::escrow(alice, seq);
        // auto const sle = env.current()->read(k);
        // BEAST_EXPECT(sle);

        // auto amtSle = sle->getFieldAmount(sfAmount);
        // std::cout << "SLE Amount: " << amtSle.getJson(JsonOptions::none) << std::endl;
        // BEAST_EXPECT(amtSle == XRP(1));

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }
    }

    void
    testSignatures(FeatureBitset features)
    {
        testcase("Signatures");

        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(5000), alice, bob, carol);
        env.close();

        std::cout << "START" << std::endl;

        // Bad Permission (tefBAD_AUTH)
        // Sign the transaction with a key that does not match the account.
        env(pay(alice, bob, XRP(1)), sig(bob), ter(tefBAD_AUTH));
        env.close();

        // Bad Permission (tefMASTER_DISABLED)
        // Disable the master key and try to pay.
        env(regkey(alice, bob));
        env.close();
        env(fset(alice, asfDisableMaster), sig(alice));
        env.close();
        env(pay(alice, bob, XRP(1)), sig(alice), ter(tefMASTER_DISABLED));
        env.close();

        // Bad Permission (tefBAD_AUTH)
        // Set a regular key for carol to bob and try to sign with alice's key.
        env(regkey(carol, bob));
        env.close();
        env(pay(carol, alice, XRP(1)), sig(alice), ter(tefBAD_AUTH));
        env.close();

        // Bad Signature (invalidTransaction)
        // Sign the transaction with a key that does not match the account.
        // Hint: Search the tests for "// Flip some bits in the signature."
        JTx jt = env.jt(pay(alice, bob, XRP(1)), sig(alice));
        STTx local = *(jt.stx);
        auto badSig = local.getFieldVL(sfTxnSignature);
        badSig[20] ^= 0xAA;
        local.setFieldVL(sfTxnSignature, badSig);

        Json::Value jvResult;
        jvResult[jss::tx_blob] = strHex(local.getSerializer().slice());
        auto const jrr = env.rpc("json", "submit", to_string(jvResult));
        BEAST_EXPECT(jrr[jss::result][jss::error] == "invalidTransaction");
    
    }

    void 
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        //testProtocol(sa);
        testSignatures(sa);
    }
};

BEAST_DEFINE_TESTSUITE(MyTests, bootcamp, ripple);
}
}
