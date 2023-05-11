////------------------------------------------------------------------------------
///*
//    This file is part of rippled: https://github.com/ripple/rippled
//    Copyright (c) 2012-2016 Ripple Labs Inc.
//
//    Permission to use, copy, modify, and/or distribute this software for any
//    purpose  with  or without fee is hereby granted, provided that the above
//    copyright notice and this permission notice appear in all copies.
//
//    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
//    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
//    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
//    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
//    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
//    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
//    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//*/
////==============================================================================
//#include <ripple/protocol/AccountID.h>
//#include <ripple/protocol/Feature.h>
//#include <ripple/protocol/SField.h>
//#include <ripple/protocol/TxFlags.h>
//#include <ripple/protocol/jss.h>
//#include <test/jtx.h>
//
//namespace ripple {
//
//class Freeze_test : public beast::unit_test::suite
//{
//    static boost::json::value
//    getAccountLines(test::jtx::Env& env, test::jtx::Account const& account)
//    {
//        boost::json::object jq;
//        jq[jss::account.c_str()] = account.human();
//        return env.rpc("json", "account_lines", serialize(jq)).as_object()[jss::result.c_str()];
//    }
//
//    static boost::json::value
//    getAccountOffers(
//        test::jtx::Env& env,
//        test::jtx::Account const& account,
//        bool current = false)
//    {
//        boost::json::object jq;
//        jq[jss::account.c_str()] = account.human();
//        jq[jss::ledger_index.c_str()] = current ? "current" : "validated";
//        return env.rpc("json", "account_offers", serialize(jq)).as_object()[jss::result.c_str()];
//    }
//
//    static bool
//    checkArraySize(boost::json::value const& val, unsigned int size)
//    {
//        return val.is_array() && val.as_array().size() == size;
//    }
//
//    void
//    testRippleState(FeatureBitset features)
//    {
//        testcase("RippleState Freeze");
//
//        using namespace test::jtx;
//        Env env(*this, features);
//
//        Account G1{"G1"};
//        Account alice{"alice"};
//        Account bob{"bob"};
//
//        env.fund(XRP(1000), G1, alice, bob);
//        env.close();
//
//        env.trust(G1["USD"](100), bob);
//        env.trust(G1["USD"](100), alice);
//        env.close();
//
//        env(pay(G1, bob, G1["USD"](10)));
//        env(pay(G1, alice, G1["USD"](100)));
//        env.close();
//
//        env(offer(alice, XRP(500), G1["USD"](100)));
//        env.close();
//
//        {
//            auto lines = getAccountLines(env, bob).as_object();
//            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines.c_str()], 1u)))
//                return;
//            BEAST_EXPECT(lines[jss::lines.c_str()].as_array()[0u].as_object()[jss::account.c_str()].as_string() == G1.human());
//            BEAST_EXPECT(lines[jss::lines.c_str()].as_array()[0u].as_object()[jss::limit.c_str()] == "100");
//            BEAST_EXPECT(lines[jss::lines.c_str()].as_array()[0u].as_object()[jss::balance.c_str()] == "10");
//        }
//
//        {
//            auto lines = getAccountLines(env, alice).as_object();
//            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines.c_str()], 1u)))
//                return;
//            BEAST_EXPECT(lines[jss::lines.c_str()].as_array()[0u].as_object()[jss::account.c_str()].as_string() == G1.human());
//            BEAST_EXPECT(lines[jss::lines.c_str()].as_array()[0u].as_object()[jss::limit.c_str()] == "100");
//            BEAST_EXPECT(lines[jss::lines.c_str()].as_array()[0u].as_object()[jss::balance.c_str()] == "100");
//        }
//
//        {
//            // Account with line unfrozen (proving operations normally work)
//            //   test: can make Payment on that line
//            env(pay(alice, bob, G1["USD"](1)));
//
//            //   test: can receive Payment on that line
//            env(pay(bob, alice, G1["USD"](1)));
//            env.close();
//        }
//
//        {
//            // Is created via a TrustSet with SetFreeze flag
//            //   test: sets LowFreeze | HighFreeze flags
//            env(trust(G1, bob["USD"](0), tfSetFreeze));
//            auto affected = env.meta()->getJson(
//                JsonOptions::none).as_object()[sfAffectedNodes.fieldName.c_str()];
//            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
//                return;
//            auto ff =
//                affected.as_array()[1u].as_object()[sfModifiedNode.fieldName].as_object()[sfFinalFields.fieldName].as_object();
//            BEAST_EXPECT(
//                ff[sfLowLimit.fieldName] ==
//                G1["USD"](0).value().getJson(JsonOptions::none));
//            BEAST_EXPECT(ff[jss::Flags.c_str()].as_uint64() & lsfLowFreeze);
//            BEAST_EXPECT(!(ff[jss::Flags.c_str()].as_uint64() & lsfHighFreeze));
//            env.close();
//        }
//
//        {
//            // Account with line frozen by issuer
//            //    test: can buy more assets on that line
//            env(offer(bob, G1["USD"](5), XRP(25)));
//            auto affected = env.meta()->getJson(
//                JsonOptions::none).as_object()[sfAffectedNodes.fieldName];
//            if (!BEAST_EXPECT(checkArraySize(affected, 5u)))
//                return;
//            auto ff =
//                affected.as_array()[3u].as_object()[sfModifiedNode.fieldName].as_object()[sfFinalFields.fieldName].as_object();
//            BEAST_EXPECT(
//                ff[sfHighLimit.fieldName] ==
//                bob["USD"](100).value().getJson(JsonOptions::none));
//            auto amt = STAmount{Issue{to_currency("USD"), noAccount()}, -15}
//                           .value()
//                           .getJson(JsonOptions::none);
//            BEAST_EXPECT(ff[sfBalance.fieldName] == amt);
//            env.close();
//        }
//
//        {
//            //    test: can not sell assets from that line
//            env(offer(bob, XRP(1), G1["USD"](5)), ter(tecUNFUNDED_OFFER));
//
//            //    test: can receive Payment on that line
//            env(pay(alice, bob, G1["USD"](1)));
//
//            //    test: can not make Payment from that line
//            env(pay(bob, alice, G1["USD"](1)), ter(tecPATH_DRY));
//        }
//
//        {
//            // check G1 account lines
//            //    test: shows freeze
//            auto lines = getAccountLines(env, G1).as_object();
//            boost::json::object bobLine;
//            for (auto& it : lines[jss::lines.c_str()].as_array())
//            {
//                if (it.as_object()[jss::account.c_str()].as_string() == bob.human())
//                {
//                    bobLine = it.as_object();
//                    break;
//                }
//            }
//            if (!BEAST_EXPECT(!bobLine.empty()))
//                return;
//            BEAST_EXPECT(bobLine[jss::freeze.c_str()] == true);
//            BEAST_EXPECT(bobLine[jss::balance.c_str()] == "-16");
//        }
//
//        {
//            //    test: shows freeze peer
//            auto lines = getAccountLines(env, bob).as_object();
//            boost::json::object g1Line;
//            for (auto & it : lines[jss::lines.c_str()].as_array())
//            {
//                if (it.as_object()[jss::account.c_str()].as_string() == G1.human())
//                {
//                    g1Line = it.as_object();
//                    break;
//                }
//            }
//            if (!BEAST_EXPECT(!g1Line.empty()))
//                return;
//            BEAST_EXPECT(g1Line[jss::freeze_peer.c_str()] == true);
//            BEAST_EXPECT(g1Line[jss::balance.c_str()] == "16");
//        }
//
//        {
//            // Is cleared via a TrustSet with ClearFreeze flag
//            //    test: sets LowFreeze | HighFreeze flags
//            env(trust(G1, bob["USD"](0), tfClearFreeze));
//            auto affected = env.meta()->getJson(
//                JsonOptions::none).as_object()[sfAffectedNodes.fieldName];
//            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
//                return;
//            auto ff =
//                affected.as_array()[1u].as_object()[sfModifiedNode.fieldName].as_object()[sfFinalFields.fieldName].as_object();
//            BEAST_EXPECT(
//                ff[sfLowLimit.fieldName] ==
//                G1["USD"](0).value().getJson(JsonOptions::none));
//            BEAST_EXPECT(!(ff[jss::Flags.c_str()].as_uint64() & lsfLowFreeze));
//            BEAST_EXPECT(!(ff[jss::Flags.c_str()].as_uint64() & lsfHighFreeze));
//            env.close();
//        }
//    }
//
//    void
//    testGlobalFreeze(FeatureBitset features)
//    {
//        testcase("Global Freeze");
//
//        using namespace test::jtx;
//        Env env(*this, features);
//
//        Account G1{"G1"};
//        Account A1{"A1"};
//        Account A2{"A2"};
//        Account A3{"A3"};
//        Account A4{"A4"};
//
//        env.fund(XRP(12000), G1);
//        env.fund(XRP(1000), A1);
//        env.fund(XRP(20000), A2, A3, A4);
//        env.close();
//
//        env.trust(G1["USD"](1200), A1);
//        env.trust(G1["USD"](200), A2);
//        env.trust(G1["BTC"](100), A3);
//        env.trust(G1["BTC"](100), A4);
//        env.close();
//
//        env(pay(G1, A1, G1["USD"](1000)));
//        env(pay(G1, A2, G1["USD"](100)));
//        env(pay(G1, A3, G1["BTC"](100)));
//        env(pay(G1, A4, G1["BTC"](100)));
//        env.close();
//
//        env(offer(G1, XRP(10000), G1["USD"](100)), txflags(tfPassive));
//        env(offer(G1, G1["USD"](100), XRP(10000)), txflags(tfPassive));
//        env(offer(A1, XRP(10000), G1["USD"](100)), txflags(tfPassive));
//        env(offer(A2, G1["USD"](100), XRP(10000)), txflags(tfPassive));
//        env.close();
//
//        {
//            // Is toggled via AccountSet using SetFlag and ClearFlag
//            //    test: SetFlag GlobalFreeze
//            env.require(nflags(G1, asfGlobalFreeze));
//            env(fset(G1, asfGlobalFreeze));
//            env.require(flags(G1, asfGlobalFreeze));
//            env.require(nflags(G1, asfNoFreeze));
//
//            //    test: ClearFlag GlobalFreeze
//            env(fclear(G1, asfGlobalFreeze));
//            env.require(nflags(G1, asfGlobalFreeze));
//            env.require(nflags(G1, asfNoFreeze));
//        }
//
//        {
//            // Account without GlobalFreeze (proving operations normally work)
//            //    test: visible offers where taker_pays is unfrozen issuer
//            auto offers = env.rpc(
//                "book_offers",
//                std::string("USD/") + G1.human(),
//                "XRP").as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()];
//            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
//                return;
//            std::set<std::string> accounts;
//            for (auto & offer : offers.as_array())
//            {
//                accounts.insert(std::string{offer.as_object()[jss::Account.c_str()].as_string()});
//            }
//            BEAST_EXPECT(accounts.find(A2.human()) != std::end(accounts));
//            BEAST_EXPECT(accounts.find(G1.human()) != std::end(accounts));
//
//            //    test: visible offers where taker_gets is unfrozen issuer
//            offers = env.rpc(
//                "book_offers",
//                "XRP",
//                std::string("USD/") + G1.human()).as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()];
//            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
//                return;
//            accounts.clear();
//            for (auto & offer : offers.as_array())
//            {
//                accounts.insert(std::string{offer.as_object()[jss::Account.c_str()].as_string()});
//            }
//            BEAST_EXPECT(accounts.find(A1.human()) != std::end(accounts));
//            BEAST_EXPECT(accounts.find(G1.human()) != std::end(accounts));
//        }
//
//        {
//            // Offers/Payments
//            //    test: assets can be bought on the market
//            env(offer(A3, G1["BTC"](1), XRP(1)));
//
//            //    test: assets can be sold on the market
//            env(offer(A4, XRP(1), G1["BTC"](1)));
//
//            //    test: direct issues can be sent
//            env(pay(G1, A2, G1["USD"](1)));
//
//            //    test: direct redemptions can be sent
//            env(pay(A2, G1, G1["USD"](1)));
//
//            //    test: via rippling can be sent
//            env(pay(A2, A1, G1["USD"](1)));
//
//            //    test: via rippling can be sent back
//            env(pay(A1, A2, G1["USD"](1)));
//        }
//
//        {
//            // Account with GlobalFreeze
//            //  set GlobalFreeze first
//            //    test: SetFlag GlobalFreeze will toggle back to freeze
//            env.require(nflags(G1, asfGlobalFreeze));
//            env(fset(G1, asfGlobalFreeze));
//            env.require(flags(G1, asfGlobalFreeze));
//            env.require(nflags(G1, asfNoFreeze));
//
//            //    test: assets can't be bought on the market
//            env(offer(A3, G1["BTC"](1), XRP(1)), ter(tecFROZEN));
//
//            //    test: assets can't be sold on the market
//            env(offer(A4, XRP(1), G1["BTC"](1)), ter(tecFROZEN));
//        }
//
//        {
//            // offers are filtered (seems to be broken?)
//            //    test: account_offers always shows own offers
//            auto offers = getAccountOffers(env, G1).as_object()[jss::offers.c_str()];
//            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
//                return;
//
//            //    test: book_offers shows offers
//            //    (should these actually be filtered?)
//            offers = env.rpc(
//                "book_offers",
//                "XRP",
//                std::string("USD/") + G1.human()).as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()];
//            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
//                return;
//
//            offers = env.rpc(
//                "book_offers",
//                std::string("USD/") + G1.human(),
//                "XRP").as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()];
//            if (!BEAST_EXPECT(checkArraySize(offers, 2u)))
//                return;
//        }
//
//        {
//            // Payments
//            //    test: direct issues can be sent
//            env(pay(G1, A2, G1["USD"](1)));
//
//            //    test: direct redemptions can be sent
//            env(pay(A2, G1, G1["USD"](1)));
//
//            //    test: via rippling cant be sent
//            env(pay(A2, A1, G1["USD"](1)), ter(tecPATH_DRY));
//        }
//    }
//
//    void
//    testNoFreeze(FeatureBitset features)
//    {
//        testcase("No Freeze");
//
//        using namespace test::jtx;
//        Env env(*this, features);
//
//        Account G1{"G1"};
//        Account A1{"A1"};
//
//        env.fund(XRP(12000), G1);
//        env.fund(XRP(1000), A1);
//        env.close();
//
//        env.trust(G1["USD"](1000), A1);
//        env.close();
//
//        env(pay(G1, A1, G1["USD"](1000)));
//        env.close();
//
//        // TrustSet NoFreeze
//        //    test: should set NoFreeze in Flags
//        env.require(nflags(G1, asfNoFreeze));
//        env(fset(G1, asfNoFreeze));
//        env.require(flags(G1, asfNoFreeze));
//        env.require(nflags(G1, asfGlobalFreeze));
//
//        //    test: cannot be cleared
//        env(fclear(G1, asfNoFreeze));
//        env.require(flags(G1, asfNoFreeze));
//        env.require(nflags(G1, asfGlobalFreeze));
//
//        //    test: can set GlobalFreeze
//        env(fset(G1, asfGlobalFreeze));
//        env.require(flags(G1, asfNoFreeze));
//        env.require(flags(G1, asfGlobalFreeze));
//
//        //    test: cannot unset GlobalFreeze
//        env(fclear(G1, asfGlobalFreeze));
//        env.require(flags(G1, asfNoFreeze));
//        env.require(flags(G1, asfGlobalFreeze));
//
//        //    test: trustlines can't be frozen
//        env(trust(G1, A1["USD"](0), tfSetFreeze));
//        auto affected =
//            env.meta()->getJson(JsonOptions::none).as_object()[sfAffectedNodes.fieldName];
//        if (!BEAST_EXPECT(checkArraySize(affected, 1u)))
//            return;
//
//        auto let =
//            affected.as_array()[0u].as_object()[sfModifiedNode.fieldName].as_object()[sfLedgerEntryType.fieldName];
//        BEAST_EXPECT(let.as_string() == jss::AccountRoot.c_str());
//    }
//
//    void
//    testOffersWhenFrozen(FeatureBitset features)
//    {
//        testcase("Offers for Frozen Trust Lines");
//
//        using namespace test::jtx;
//        Env env(*this, features);
//
//        Account G1{"G1"};
//        Account A2{"A2"};
//        Account A3{"A3"};
//        Account A4{"A4"};
//
//        env.fund(XRP(1000), G1, A3, A4);
//        env.fund(XRP(2000), A2);
//        env.close();
//
//        env.trust(G1["USD"](1000), A2);
//        env.trust(G1["USD"](2000), A3);
//        env.trust(G1["USD"](2000), A4);
//        env.close();
//
//        env(pay(G1, A3, G1["USD"](2000)));
//        env(pay(G1, A4, G1["USD"](2000)));
//        env.close();
//
//        env(offer(A3, XRP(1000), G1["USD"](1000)), txflags(tfPassive));
//        env.close();
//
//        // removal after successful payment
//        //    test: make a payment with partially consuming offer
//        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
//        env.close();
//
//        //    test: offer was only partially consumed
//        auto offers = getAccountOffers(env, A3).as_object()[jss::offers.c_str()];
//        if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
//            return;
//        BEAST_EXPECT(
//            offers.as_array()[0u].as_object()[jss::taker_gets.c_str()] ==
//            G1["USD"](999).value().getJson(JsonOptions::none));
//
//        //    test: someone else creates an offer providing liquidity
//        env(offer(A4, XRP(999), G1["USD"](999)));
//        env.close();
//
//        //    test: owner of partially consumed offers line is frozen
//        env(trust(G1, A3["USD"](0), tfSetFreeze));
//        auto affected =
//            env.meta()->getJson(JsonOptions::none).as_object()[sfAffectedNodes.fieldName];
//        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
//            return;
//        auto ff =
//            affected.as_array()[1u].as_object()[sfModifiedNode.fieldName].as_object()[sfFinalFields.fieldName].as_object();
//        BEAST_EXPECT(
//            ff[sfHighLimit.fieldName] ==
//            G1["USD"](0).value().getJson(JsonOptions::none));
//        BEAST_EXPECT(!(ff[jss::Flags.c_str()].as_uint64() & lsfLowFreeze));
//        BEAST_EXPECT(ff[jss::Flags.c_str()].as_uint64() & lsfHighFreeze);
//        env.close();
//
//        // verify offer on the books
//        offers = getAccountOffers(env, A3).as_object()[jss::offers.c_str()];
//        if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
//            return;
//
//        //    test: Can make a payment via the new offer
//        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
//        env.close();
//
//        //    test: Partially consumed offer was removed by tes* payment
//        offers = getAccountOffers(env, A3).as_object()[jss::offers.c_str()];
//        if (!BEAST_EXPECT(checkArraySize(offers, 0u)))
//            return;
//
//        // removal buy successful OfferCreate
//        //    test: freeze the new offer
//        env(trust(G1, A4["USD"](0), tfSetFreeze));
//        affected =
//            env.meta()->getJson(JsonOptions::none).as_object()[sfAffectedNodes.fieldName];
//        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
//            return;
//        ff = affected.as_array()[0u].as_object()[sfModifiedNode.fieldName].as_object()[sfFinalFields.fieldName].as_object();
//        BEAST_EXPECT(
//            ff[sfLowLimit.fieldName] ==
//            G1["USD"](0).value().getJson(JsonOptions::none));
//        BEAST_EXPECT(ff[jss::Flags.c_str()].as_uint64() & lsfLowFreeze);
//        BEAST_EXPECT(!(ff[jss::Flags.c_str()].as_uint64() & lsfHighFreeze));
//        env.close();
//
//        //    test: can no longer create a crossing offer
//        env(offer(A2, G1["USD"](999), XRP(999)));
//        affected =
//            env.meta()->getJson(JsonOptions::none).as_object()[sfAffectedNodes.fieldName];
//        if (!BEAST_EXPECT(checkArraySize(affected, 8u)))
//            return;
//        auto created = affected.as_array()[0u].as_object()[sfCreatedNode.fieldName].as_object();
//        BEAST_EXPECT(
//            created[sfNewFields.fieldName].as_object()[jss::Account.c_str()].as_string() == A2.human());
//        env.close();
//
//        //    test: offer was removed by offer_createx
//        offers = getAccountOffers(env, A4).as_object()[jss::offers.c_str()];
//        if (!BEAST_EXPECT(checkArraySize(offers, 0u)))
//            return;
//    }
//
//public:
//    void
//    run() override
//    {
//        auto testAll = [this](FeatureBitset features) {
//            testRippleState(features);
//            testGlobalFreeze(features);
//            testNoFreeze(features);
//            testOffersWhenFrozen(features);
//        };
//        using namespace test::jtx;
//        auto const sa = supported_amendments();
//        testAll(sa - featureFlowCross);
//        testAll(sa);
//    }
//};
//
//BEAST_DEFINE_TESTSUITE(Freeze, app, ripple);
//}  // namespace ripple
