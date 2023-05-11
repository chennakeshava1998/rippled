////------------------------------------------------------------------------------
///*
//    This file is part of rippled: https://github.com/ripple/rippled
//    Copyright (c) 2016 Ripple Labs Inc.
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
//
//#include <ripple/json/json_reader.h>
//#include <ripple/json/json_value.h>
//#include <ripple/json/serialize.h>
//#include <ripple/protocol/jss.h>
//#include <test/jtx.h>
//
//#include <boost/utility/string_ref.hpp>
//
//#include <algorithm>
//
//namespace ripple {
//namespace test {
//
//static char const* bobs_account_objects[] = {
//    R"json({
//  "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
//  "BookDirectory" : "50AD0A9E54D2B381288D535EB724E4275FFBF41580D28A925D038D7EA4C68000",
//  "BookNode" : "0",
//  "Flags" : 65536,
//  "LedgerEntryType" : "Offer",
//  "OwnerNode" : "0",
//  "Sequence" : 6,
//  "TakerGets" : {
//    "currency" : "USD",
//    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
//    "value" : "1"
//  },
//  "TakerPays" : "100000000",
//  "index" : "29665262716C19830E26AEEC0916E476FC7D8EF195FF3B4F06829E64F82A3B3E"
//})json",
//    R"json({
//    "Balance" : {
//        "currency" : "USD",
//        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
//        "value" : "-1000"
//    },
//    "Flags" : 131072,
//    "HighLimit" : {
//        "currency" : "USD",
//        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
//        "value" : "1000"
//    },
//    "HighNode" : "0",
//    "LedgerEntryType" : "RippleState",
//    "LowLimit" : {
//        "currency" : "USD",
//        "issuer" : "r9cZvwKU3zzuZK9JFovGg1JC5n7QiqNL8L",
//        "value" : "0"
//    },
//    "LowNode" : "0",
//    "index" : "D13183BCFFC9AAC9F96AEBB5F66E4A652AD1F5D10273AEB615478302BEBFD4A4"
//})json",
//    R"json({
//    "Balance" : {
//        "currency" : "USD",
//        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
//        "value" : "-1000"
//    },
//    "Flags" : 131072,
//    "HighLimit" : {
//        "currency" : "USD",
//        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
//        "value" : "1000"
//    },
//    "HighNode" : "0",
//    "LedgerEntryType" : "RippleState",
//    "LowLimit" : {
//        "currency" : "USD",
//        "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
//        "value" : "0"
//    },
//    "LowNode" : "0",
//    "index" : "D89BC239086183EB9458C396E643795C1134963E6550E682A190A5F021766D43"
//})json",
//    R"json({
//    "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
//    "BookDirectory" : "B025997A323F5C3E03DDF1334471F5984ABDE31C59D463525D038D7EA4C68000",
//    "BookNode" : "0",
//    "Flags" : 65536,
//    "LedgerEntryType" : "Offer",
//    "OwnerNode" : "0",
//    "Sequence" : 7,
//    "TakerGets" : {
//        "currency" : "USD",
//        "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
//        "value" : "1"
//    },
//    "TakerPays" : "100000000",
//    "index" : "F03ABE26CB8C5F4AFB31A86590BD25C64C5756FCE5CE9704C27AFE291A4A29A1"
//})json"};
//
//class AccountObjects_test : public beast::unit_test::suite
//{
//public:
//    void
//    testErrors()
//    {
//        testcase("error cases");
//
//        using namespace jtx;
//        Env env(*this);
//
//        // test error on no account
//        {
//            auto resp = env.rpc("json", "account_objects");
//            BEAST_EXPECT(resp.as_object()[jss::error_message.c_str()] == "Syntax error.");
//        }
//        // test error on  malformed account string.
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] =
//                "n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV";
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] == "Disallowed seed.");
//        }
//        // test error on account that's not in the ledger.
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = Account{"bogie"}.human();
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] == "Account not found.");
//        }
//        Account const bob{"bob"};
//        // test error on large ledger_index.
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::ledger_index.c_str()] = 10;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] == "ledgerNotFound");
//        }
//
//        env.fund(XRP(1000), bob);
//        // test error on type param not a string
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::type.c_str()] = 10;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'type', not string.");
//        }
//        // test error on type param not a valid type
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::type.c_str()] = "expedited";
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'type'.");
//        }
//        // test error on limit -ve
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::limit.c_str().c_str()] = -1;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'limit', not unsigned integer.");
//        }
//        // test errors on marker
//        {
//            Account const gw{"G"};
//            env.fund(XRP(1000), gw);
//            auto const USD = gw["USD"];
//            env.trust(USD(1000), bob);
//            env(pay(gw, bob, XRP(1)));
//            env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));
//
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::limit.c_str().c_str()] = 1;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//
//            auto resume_marker = resp.as_object()[jss::result.c_str()].as_object()[jss::marker.c_str().c_str()];
//            std::string mark = serialize(resume_marker);
//            params[jss::marker.c_str().c_str()] = 10;
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'marker', not string.");
//
//            params[jss::marker.c_str().c_str()] = "This is a string with no comma";
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'marker'.");
//
//            params[jss::marker.c_str().c_str()] = "This string has a comma, but is not hex";
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'marker'.");
//
//            params[jss::marker.c_str().c_str()] = std::string(&mark[1U], 64);
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'marker'.");
//
//            params[jss::marker.c_str().c_str()] = std::string(&mark[1U], 65);
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'marker'.");
//
//            params[jss::marker.c_str().c_str()] = std::string(&mark[1U], 65) + "not hex";
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(
//                resp.as_object()[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                "Invalid field 'marker'.");
//
//            // Should this be an error?
//            // A hex digit is absent from the end of marker.
//            // No account objects returned.
//            params[jss::marker.c_str().c_str()] = std::string(&mark[1U], 128);
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array().size() == 0);
//        }
//    }
//
//    void
//    testUnsteppedThenStepped()
//    {
//        testcase("unsteppedThenStepped");
//
//        using namespace jtx;
//        Env env(*this);
//
//        Account const gw1{"G1"};
//        Account const gw2{"G2"};
//        Account const bob{"bob"};
//
//        auto const USD1 = gw1["USD"];
//        auto const USD2 = gw2["USD"];
//
//        env.fund(XRP(1000), gw1, gw2, bob);
//        env.trust(USD1(1000), bob);
//        env.trust(USD2(1000), bob);
//
//        env(pay(gw1, bob, USD1(1000)));
//        env(pay(gw2, bob, USD2(1000)));
//
//        env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));
//        env(offer(bob, XRP(100), USD1(1)), txflags(tfPassive));
//
//        boost::json::object bobj[4];
//        for (int i = 0; i < 4; ++i)
//            Json::Reader{}.parse(bobs_account_objects[i], bobj[i]);
//
//        // test 'unstepped'
//        // i.e. request account objects without explicit limit/marker paging
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//
//            BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array().size() == 4);
//            for (int i = 0; i < 4; ++i)
//            {
//                auto& aobj = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array()[i];
//                aobj.as_object().erase("PreviousTxnID");
//                aobj.as_object().erase("PreviousTxnLgrSeq");
//                BEAST_EXPECT(aobj == bobj[i]);
//            }
//        }
//        // test request with type parameter as filter, unstepped
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::type.c_str()] = jss::state;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//
//            BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array().size() == 2);
//            for (int i = 0; i < 2; ++i)
//            {
//                auto& aobj = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array()[i];
//                aobj.as_object().erase("PreviousTxnID");
//                aobj.as_object().erase("PreviousTxnLgrSeq");
//                BEAST_EXPECT(aobj == bobj[i + 1]);
//            }
//        }
//        // test stepped one-at-a-time with limit=1, resume from prev marker
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::limit.c_str().c_str()] = 1;
//            for (int i = 0; i < 4; ++i)
//            {
//                auto resp =
//                    env.rpc("json", "account_objects", serialize(params));
//                auto& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//                BEAST_EXPECT(aobjs.as_array().size() == 1);
//                auto& aobj = aobjs.as_array()[0U];
//                if (i < 3)
//                    BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::limit.c_str().c_str()] == 1);
//                else
//                    BEAST_EXPECT(!resp.as_object()[jss::result.c_str()].as_object().contains(jss::limit.c_str()));
//
//                aobj.as_object().erase("PreviousTxnID");
//                aobj.as_object().erase("PreviousTxnLgrSeq");
//
//                BEAST_EXPECT(aobj == bobj[i]);
//
//                params[jss::marker.c_str().c_str()] = resp.as_object()[jss::result.c_str()].as_object()[jss::marker.c_str().c_str()];
//            }
//        }
//    }
//
//    void
//    testUnsteppedThenSteppedWithNFTs()
//    {
//        // The preceding test case, unsteppedThenStepped(), found a bug in the
//        // support for NFToken Pages.  So we're leaving that test alone when
//        // adding tests to exercise NFTokenPages.
//        testcase("unsteppedThenSteppedWithNFTs");
//
//        using namespace jtx;
//        Env env(*this);
//
//        Account const gw1{"G1"};
//        Account const gw2{"G2"};
//        Account const bob{"bob"};
//
//        auto const USD1 = gw1["USD"];
//        auto const USD2 = gw2["USD"];
//
//        env.fund(XRP(1000), gw1, gw2, bob);
//        env.close();
//
//        // Check behavior if there are no account objects.
//        {
//            // Unpaged
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//            BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array().size() == 0);
//
//            // Limit == 1
//            params[jss::limit.c_str().c_str()] = 1;
//            resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//            BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array().size() == 0);
//        }
//
//        // Check behavior if there are only NFTokens.
//        env(token::mint(bob, 0u), txflags(tfTransferable));
//        env.close();
//
//        // test 'unstepped'
//        // i.e. request account objects without explicit limit/marker paging
//        boost::json::value unpaged;
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//
//            unpaged = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//            BEAST_EXPECT(unpaged.as_array().size() == 1);
//        }
//        // test request with type parameter as filter, unstepped
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::type.c_str()] = jss::nft_page;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//            boost::json::object& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//            BEAST_EXPECT(aobjs.as_array().size() == 1);
//            BEAST_EXPECT(
//                aobjs[0u].as_object()[sfLedgerEntryType.jsonName.c_str()] == jss::NFTokenPage);
//            BEAST_EXPECT(aobjs[0u].as_object()[sfNFTokens.jsonName.c_str()].as_array().size() == 1);
//        }
//        // test stepped one-at-a-time with limit=1, resume from prev marker
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::limit.c_str().c_str()] = 1;
//
//            boost::json::object resp =
//                env.rpc("json", "account_objects", serialize(params));
//            boost::json::object& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//            BEAST_EXPECT(aobjs.as_array().size() == 1);
//            auto& aobj = aobjs[0U];
//            BEAST_EXPECT(!resp.as_object()[jss::result.c_str()].as_object().contains(jss::limit.c_str()));
//            BEAST_EXPECT(!resp.as_object()[jss::result.c_str()].as_object().contains(jss::marker.c_str()));
//
//            BEAST_EXPECT(aobj == unpaged[0u]);
//        }
//
//        // Add more objects in addition to the NFToken Page.
//        env.trust(USD1(1000), bob);
//        env.trust(USD2(1000), bob);
//
//        env(pay(gw1, bob, USD1(1000)));
//        env(pay(gw2, bob, USD2(1000)));
//
//        env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));
//        env(offer(bob, XRP(100), USD1(1)), txflags(tfPassive));
//        env.close();
//
//        // test 'unstepped'
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//
//            unpaged = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//            BEAST_EXPECT(unpaged.as_array().size() == 5);
//        }
//        // test request with type parameter as filter, unstepped
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::type.c_str()] = jss::nft_page;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//            boost::json::object& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//            BEAST_EXPECT(aobjs.as_array().size() == 1);
//            BEAST_EXPECT(
//                aobjs[0u][sfLedgerEntryType.jsonName.c_str()] == jss::NFTokenPage);
//            BEAST_EXPECT(aobjs[0u][sfNFTokens.jsonName.c_str()].as_array().size() == 1);
//        }
//        // test stepped one-at-a-time with limit=1, resume from prev marker
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::limit.c_str().c_str()] = 1;
//            for (int i = 0; i < 5; ++i)
//            {
//                boost::json::object resp =
//                    env.rpc("json", "account_objects", serialize(params));
//                boost::json::object& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//                BEAST_EXPECT(aobjs.as_array().size() == 1);
//                auto& aobj = aobjs[0U];
//                if (i < 4)
//                {
//                    BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::limit.c_str().c_str()] == 1);
//                    BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object().contains(jss::marker.c_str()));
//                }
//                else
//                {
//                    BEAST_EXPECT(!resp.as_object()[jss::result.c_str()].as_object().contains(jss::limit.c_str()));
//                    BEAST_EXPECT(!resp.as_object()[jss::result.c_str()].as_object().contains(jss::marker.c_str()));
//                }
//
//                BEAST_EXPECT(aobj == unpaged[i]);
//
//                params[jss::marker.c_str().c_str()] = resp.as_object()[jss::result.c_str()].as_object()[jss::marker.c_str().c_str()];
//            }
//        }
//
//        // Make sure things still work if there is more than 1 NFT Page.
//        for (int i = 0; i < 32; ++i)
//        {
//            env(token::mint(bob, 0u), txflags(tfTransferable));
//            env.close();
//        }
//        // test 'unstepped'
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//
//            unpaged = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//            BEAST_EXPECT(unpaged.as_array().size() == 6);
//        }
//        // test request with type parameter as filter, unstepped
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::type.c_str()] = jss::nft_page;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//            BEAST_EXPECT(!resp.as_object().contains(jss::marker.c_str()));
//            boost::json::object& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//            BEAST_EXPECT(aobjs.as_array().size() == 2);
//        }
//        // test stepped one-at-a-time with limit=1, resume from prev marker
//        {
//            boost::json::object params;
//            params[jss::account.c_str()] = bob.human();
//            params[jss::limit.c_str().c_str()] = 1;
//            for (int i = 0; i < 6; ++i)
//            {
//                boost::json::object resp =
//                    env.rpc("json", "account_objects", serialize(params));
//                boost::json::object& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//                BEAST_EXPECT(aobjs.as_array().size() == 1);
//                auto& aobj = aobjs[0U];
//                if (i < 5)
//                {
//                    BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object()[jss::limit.c_str().c_str()] == 1);
//                    BEAST_EXPECT(resp.as_object()[jss::result.c_str()].as_object().contains(jss::marker.c_str()));
//                }
//                else
//                {
//                    BEAST_EXPECT(!resp.as_object()[jss::result.c_str()].as_object().contains(jss::limit.c_str()));
//                    BEAST_EXPECT(!resp.as_object()[jss::result.c_str()].as_object().contains(jss::marker.c_str()));
//                }
//
//                BEAST_EXPECT(aobj == unpaged[i]);
//
//                params[jss::marker.c_str().c_str()] = resp.as_object()[jss::result.c_str()].as_object()[jss::marker.c_str().c_str()];
//            }
//        }
//    }
//
//    void
//    testObjectTypes()
//    {
//        testcase("object types");
//
//        // Give gw a bunch of ledger objects and make sure we can retrieve
//        // them by type.
//        using namespace jtx;
//
//        Account const alice{"alice"};
//        Account const gw{"gateway"};
//        auto const USD = gw["USD"];
//
//        Env env(*this);
//
//        // Make a lambda we can use to get "account_objects" easily.
//        auto acct_objs = [&env.c_str()](Account const& acct, char const* type) {
//            boost::json::object params;
//            params[jss::account.c_str()] = acct.human();
//            params[jss::type.c_str()] = type;
//            params[jss::ledger_index.c_str()] = "validated";
//            return env.rpc("json", "account_objects", serialize(params));
//        };
//
//        // Make a lambda that easily identifies the size of account objects.
//        auto acct_objs_is_size = [](boost::json::object const& resp, unsigned size) {
//            return resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].isArray() &&
//                (resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()].as_array().size() == size);
//        };
//
//        env.fund(XRP(10000), gw, alice);
//        env.close();
//
//        // Since the account is empty now, all account objects should come
//        // back empty.
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::account), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::amendments), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::check), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::deposit_preauth), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::directory), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::escrow), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::fee), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::hashes), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::nft_page), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::offer), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::payment_channel), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::signer_list), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::state), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::ticket), 0));
//
//        // gw mints an NFT so we can find it.
//        uint256 const nftID{token::getNextID(env, gw, 0u, tfTransferable)};
//        env(token::mint(gw, 0u), txflags(tfTransferable));
//        env.close();
//        {
//            // Find the NFToken page and make sure it's the right one.
//            boost::json::object const resp = acct_objs(gw, jss::nft_page);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& nftPage = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(nftPage[sfNFTokens.jsonName.c_str()].as_array().size() == 1);
//            BEAST_EXPECT(
//                nftPage[sfNFTokens.jsonName.c_str()][0u][sfNFToken.jsonName.c_str()]
//                       [sfNFTokenID.jsonName.c_str()] == serialize(nftID));
//        }
//
//        // Set up a trust line so we can find it.
//        env.trust(USD(1000), alice);
//        env.close();
//        env(pay(gw, alice, USD(5)));
//        env.close();
//        {
//            // Find the trustline and make sure it's the right one.
//            boost::json::object const resp = acct_objs(gw, jss::state);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& state = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(state[sfBalance.jsonName.c_str()].as_object()[jss::value.c_str()].asInt() == -5);
//            BEAST_EXPECT(
//                state[sfHighLimit.jsonName.c_str()].as_object()[jss::value.c_str()].asUInt() == 1000);
//        }
//        // gw writes a check for USD(10) to alice.
//        env(check::create(gw, alice, USD(10)));
//        env.close();
//        {
//            // Find the check.
//            boost::json::object const resp = acct_objs(gw, jss::check);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& check = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(check[sfAccount.jsonName.c_str()] == gw.human());
//            BEAST_EXPECT(check[sfDestination.jsonName.c_str()] == alice.human());
//            BEAST_EXPECT(check[sfSendMax.jsonName.c_str()].as_object()[jss::value.c_str()].asUInt() == 10);
//        }
//        // gw preauthorizes payments from alice.
//        env(deposit::auth(gw, alice));
//        env.close();
//        {
//            // Find the preauthorization.
//            boost::json::object const resp = acct_objs(gw, jss::deposit_preauth);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& preauth = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(preauth[sfAccount.jsonName.c_str()] == gw.human());
//            BEAST_EXPECT(preauth[sfAuthorize.jsonName.c_str()] == alice.human());
//        }
//        {
//            // gw creates an escrow that we can look for in the ledger.
//            boost::json::object jvEscrow;
//            jvEscrow[jss::TransactionType.c_str()] = jss::EscrowCreate;
//            jvEscrow[jss::Flags.c_str()] = tfUniversal;
//            jvEscrow[jss::Account.c_str()] = gw.human();
//            jvEscrow[jss::Destination.c_str()] = gw.human();
//            jvEscrow[jss::Amount.c_str()] = XRP(100).value().getJson(JsonOptions::none);
//            jvEscrow[sfFinishAfter.jsonName.c_str()] =
//                env.now().time_since_epoch().count() + 1;
//            env(jvEscrow);
//            env.close();
//        }
//        {
//            // Find the escrow.
//            boost::json::object const resp = acct_objs(gw, jss::escrow);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& escrow = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(escrow[sfAccount.jsonName.c_str()] == gw.human());
//            BEAST_EXPECT(escrow[sfDestination.jsonName.c_str()] == gw.human());
//            BEAST_EXPECT(escrow[sfAmount.jsonName.c_str()].asUInt() == 100'000'000);
//        }
//        // gw creates an offer that we can look for in the ledger.
//        env(offer(gw, USD(7), XRP(14)));
//        env.close();
//        {
//            // Find the offer.
//            boost::json::object const resp = acct_objs(gw, jss::offer);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& offer = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(offer[sfAccount.jsonName.c_str()] == gw.human());
//            BEAST_EXPECT(offer[sfTakerGets.jsonName.c_str()].asUInt() == 14'000'000);
//            BEAST_EXPECT(offer[sfTakerPays.jsonName.c_str()].as_object()[jss::value.c_str()].asUInt() == 7);
//        }
//        {
//            // Create a payment channel from qw to alice that we can look for.
//            boost::json::object jvPayChan;
//            jvPayChan[jss::TransactionType.c_str()] = jss::PaymentChannelCreate;
//            jvPayChan[jss::Flags.c_str()] = tfUniversal;
//            jvPayChan[jss::Account.c_str()] = gw.human();
//            jvPayChan[jss::Destination.c_str()] = alice.human();
//            jvPayChan[jss::Amount.c_str()] =
//                XRP(300).value().getJson(JsonOptions::none);
//            jvPayChan[sfSettleDelay.jsonName.c_str()] = 24 * 60 * 60;
//            jvPayChan[sfPublicKey.jsonName.c_str()] = strHex(gw.pk().slice());
//            env(jvPayChan);
//            env.close();
//        }
//        {
//            // Find the payment channel.
//            boost::json::object const resp = acct_objs(gw, jss::payment_channel);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& payChan = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(payChan[sfAccount.jsonName.c_str()] == gw.human());
//            BEAST_EXPECT(payChan[sfAmount.jsonName.c_str()].asUInt() == 300'000'000);
//            BEAST_EXPECT(
//                payChan[sfSettleDelay.jsonName.c_str()].asUInt() == 24 * 60 * 60);
//        }
//        // Make gw multisigning by adding a signerList.
//        env(signers(gw, 6, {{alice, 7}}));
//        env.close();
//        {
//            // Find the signer list.
//            boost::json::object const resp = acct_objs(gw, jss::signer_list);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& signerList =
//                resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(signerList[sfSignerQuorum.jsonName.c_str()] == 6);
//            auto const& entry = signerList[sfSignerEntries.jsonName.c_str()][0u]
//                                          [sfSignerEntry.jsonName.c_str()];
//            BEAST_EXPECT(entry[sfAccount.jsonName.c_str()] == alice.human());
//            BEAST_EXPECT(entry[sfSignerWeight.jsonName.c_str()].asUInt() == 7);
//        }
//        // Create a Ticket for gw.
//        env(ticket::create(gw, 1));
//        env.close();
//        {
//            // Find the ticket.
//            boost::json::object const resp = acct_objs(gw, jss::ticket);
//            BEAST_EXPECT(acct_objs_is_size(resp, 1));
//
//            auto const& ticket = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()][0u];
//            BEAST_EXPECT(ticket[sfAccount.jsonName.c_str()] == gw.human());
//            BEAST_EXPECT(ticket[sfLedgerEntryType.jsonName.c_str()] == jss::Ticket);
//            BEAST_EXPECT(ticket[sfTicketSequence.jsonName.c_str()].asUInt() == 13);
//        }
//        {
//            // See how "deletion_blockers_only" handles gw's directory.
//            boost::json::object params;
//            params[jss::account.c_str()] = gw.human();
//            params[jss::deletion_blockers_only.c_str()] = true;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//
//            std::vector<std::string> const expectedLedgerTypes = [] {
//                std::vector<std::string> v{
//                    jss::Escrow.c_str(),
//                    jss::Check.c_str(),
//                    jss::NFTokenPage.c_str(),
//                    jss::RippleState.c_str(),
//                    jss::PayChannel.c_str()};
//                std::sort(v.begin(), v.end());
//                return v;
//            }();
//
//            std::uint32_t const expectedAccountObjects{
//                static_cast<std::uint32_t>(std::size(expectedLedgerTypes))};
//
//            if (BEAST_EXPECT(acct_objs_is_size(resp, expectedAccountObjects)))
//            {
//                auto const& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//                std::vector<std::string> gotLedgerTypes;
//                gotLedgerTypes.reserve(expectedAccountObjects);
//                for (std::uint32_t i = 0; i < expectedAccountObjects; ++i)
//                {
//                    gotLedgerTypes.push_back(
//                        aobjs[i]["LedgerEntryType"].asString());
//                }
//                std::sort(gotLedgerTypes.begin(), gotLedgerTypes.end());
//                BEAST_EXPECT(gotLedgerTypes == expectedLedgerTypes);
//            }
//        }
//        {
//            // See how "deletion_blockers_only" with `type` handles gw's
//            // directory.
//            boost::json::object params;
//            params[jss::account.c_str()] = gw.human();
//            params[jss::deletion_blockers_only.c_str()] = true;
//            params[jss::type.c_str()] = jss::escrow;
//            auto resp = env.rpc("json", "account_objects", serialize(params));
//
//            if (BEAST_EXPECT(acct_objs_is_size(resp, 1u)))
//            {
//                auto const& aobjs = resp.as_object()[jss::result.c_str()].as_object()[jss::account_objects.c_str()];
//                BEAST_EXPECT(aobjs[0u]["LedgerEntryType"] == jss::Escrow);
//            }
//        }
//
//        // Run up the number of directory entries so gw has two
//        // directory nodes.
//        for (int d = 1'000'032; d >= 1'000'000; --d)
//        {
//            env(offer(gw, USD(1), drops(d)));
//            env.close();
//        }
//
//        // Verify that the non-returning types still don't return anything.
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::account), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::amendments), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::directory), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::fee), 0));
//        BEAST_EXPECT(acct_objs_is_size(acct_objs(gw, jss::hashes), 0));
//    }
//
//    void
//    run() override
//    {
//        testErrors();
//        testUnsteppedThenStepped();
//        testUnsteppedThenSteppedWithNFTs();
//        testObjectTypes();
//    }
//};
//
//BEAST_DEFINE_TESTSUITE(AccountObjects, app, ripple);
//
//}  // namespace test
//}  // namespace ripple
