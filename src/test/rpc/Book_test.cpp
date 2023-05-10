//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.
    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/beast/unit_test.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/Tuning.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>
#include <boost/json.hpp>

namespace ripple {
namespace test {

class Book_test : public beast::unit_test::suite
{
    std::string
    getBookDir(jtx::Env& env, Issue const& in, Issue const& out)
    {
        std::string dir;
        auto uBookBase = getBookBase({in, out});
        auto uBookEnd = getQualityNext(uBookBase);
        auto view = env.closed();
        auto key = view->succ(uBookBase, uBookEnd);
        if (key)
        {
            auto sleOfferDir = view->read(keylet::page(key.value()));
            uint256 offerIndex;
            unsigned int bookEntry;
            cdirFirst(
                *view, sleOfferDir->key(), sleOfferDir, bookEntry, offerIndex);
            auto sleOffer = view->read(keylet::offer(offerIndex));
            dir = to_string(sleOffer->getFieldH256(sfBookDirectory));
        }
        return dir;
    }

public:
    void
    testOneSideEmptyBook()
    {
        testcase("One Side Empty Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()].emplace_array();
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array().size() == 0);
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](boost::json::value & jv) {
                auto & t = jv.as_object().at(jss::transaction.c_str()).as_object();
                return t.at(jss::TransactionType.c_str()) == jss::OfferCreate.c_str() &&
                    t.at(jss::TakerGets.c_str()) ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t.at(jss::TakerPays.c_str()) ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 2)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testOneSideOffersInBook()
    {
        testcase("One Side Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)), require(owners("alice", 1)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)), require(owners("alice", 2)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array().size() == 1);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array()[0u].as_object()[jss::TakerGets.c_str()] ==
                XRP(200).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array()[0u].as_object()[jss::TakerPays.c_str()] ==
                USD(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 4)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testBothSidesEmptyBook()
    {
        testcase("Both Sides Empty Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::both.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array().size() == 0);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array().size() == 0);
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 2)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    XRP(75).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testBothSidesOffersInBook()
    {
        testcase("Both Sides Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)), require(owners("alice", 1)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)), require(owners("alice", 2)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::both.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array().size() == 1);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array().size() == 1);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array()[0u].as_object()[jss::TakerGets.c_str()] ==
                USD(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array()[0u].as_object()[jss::TakerPays.c_str()] ==
                XRP(500).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array()[0u].as_object()[jss::TakerGets.c_str()] ==
                XRP(200).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array()[0u].as_object()[jss::TakerPays.c_str()] ==
                USD(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 4)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    XRP(75).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testMultipleBooksOneSideEmptyBook()
    {
        testcase("Multiple Books, One Side Empty");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "CNY";
                j[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "JPY";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array().size() == 0);
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 2)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    JPY(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    CNY(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)), require(owners("alice", 4)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testMultipleBooksOneSideOffersInBook()
    {
        testcase("Multiple Books, One Side Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)), require(owners("alice", 1)));

        // Create an ask: TakerPays 500/CNY, TakerGets 100/JPY
        env(offer("alice", CNY(500), JPY(100)), require(owners("alice", 2)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)), require(owners("alice", 3)));

        // Create a bid: TakerPays 100/JPY, TakerGets 200/CNY
        env(offer("alice", JPY(100), CNY(200)), require(owners("alice", 4)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "CNY";
                j[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "JPY";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array().size() == 2);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array()[0u].as_object()[jss::TakerGets.c_str()] ==
                XRP(200).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array()[0u].as_object()[jss::TakerPays.c_str()] ==
                USD(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array()[1u].as_object()[jss::TakerGets.c_str()] ==
                CNY(200).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array()[1u].as_object()[jss::TakerPays.c_str()] ==
                JPY(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 5)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 6)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 7)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    JPY(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    CNY(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)), require(owners("alice", 8)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testMultipleBooksBothSidesEmptyBook()
    {
        testcase("Multiple Books, Both Sides Empty Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::both.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::both.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "CNY";
                j[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "JPY";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array().size() == 0);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array().size() == 0);
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 2)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    XRP(75).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    JPY(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    CNY(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)), require(owners("alice", 4)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    CNY(75).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    JPY(100).value().getJson(JsonOptions::none);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testMultipleBooksBothSidesOffersInBook()
    {
        testcase("Multiple Books, Both Sides Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)), require(owners("alice", 1)));

        // Create an ask: TakerPays 500/CNY, TakerGets 100/JPY
        env(offer("alice", CNY(500), JPY(100)), require(owners("alice", 2)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)), require(owners("alice", 3)));

        // Create a bid: TakerPays 100/JPY, TakerGets 200/CNY
        env(offer("alice", JPY(100), CNY(200)), require(owners("alice", 4)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::both.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }
            // RPC subscribe to books stream
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::both.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "CNY";
                j[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "JPY";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array().size() == 2);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array().size() == 2);
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array()[0u].as_object()[jss::TakerGets.c_str()] ==
                USD(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array()[0u].as_object()[jss::TakerPays.c_str()] ==
                XRP(500).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array()[1u].as_object()[jss::TakerGets.c_str()] ==
                JPY(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::asks.c_str()].as_array()[1u].as_object()[jss::TakerPays.c_str()] ==
                CNY(500).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array()[0u].as_object()[jss::TakerGets.c_str()] ==
                XRP(200).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array()[0u].as_object()[jss::TakerPays.c_str()] ==
                USD(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array()[1u].as_object()[jss::TakerGets.c_str()] ==
                CNY(200).value().getJson(JsonOptions::none));
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object()[jss::bids.c_str()].as_array()[1u].as_object()[jss::TakerPays.c_str()] ==
                JPY(100).value().getJson(JsonOptions::none));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 5)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    XRP(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)), require(owners("alice", 6)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    XRP(75).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    USD(100).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 7)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    JPY(100).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    CNY(700).value().getJson(JsonOptions::none);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)), require(owners("alice", 8)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jv) {
                auto & t = jv.as_object()[jss::transaction.c_str()].as_object();
                return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                    t[jss::TakerGets.c_str()] ==
                    CNY(75).value().getJson(JsonOptions::none) &&
                    t[jss::TakerPays.c_str()] ==
                    JPY(100).value().getJson(JsonOptions::none);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
    }

    void
    testTrackOffers()
    {
        testcase("TrackOffers");
        using namespace jtx;
        Env env(*this);
        Account gw{"gw"};
        Account alice{"alice"};
        Account bob{"bob"};
        auto wsc = makeWSClient(env.app().config());
        env.fund(XRP(20000), alice, bob, gw);
        env.close();
        auto USD = gw["USD"];

        boost::json::object books;
        {
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = true;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
                BEAST_EXPECT(
                    jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
                BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
            }
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
            BEAST_EXPECT(
                jv.as_object()[jss::result.c_str()].as_object().contains(jss::offers.c_str()) &&
                jv.as_object()[jss::result.c_str()].as_object()[jss::offers.c_str()].as_array().size() == 0);
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::asks.c_str()));
            BEAST_EXPECT(!jv.as_object()[jss::result.c_str()].as_object().contains(jss::bids.c_str()));
        }

        env(rate(gw, 1.1));
        env.close();
        env.trust(USD(1000), alice);
        env.trust(USD(1000), bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(50)));
        env(offer(alice, XRP(4000), USD(10)));
        env.close();

        boost::json::object jvParams;
        jvParams[jss::taker.c_str()] = env.master.human();
        jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
        jvParams[jss::ledger_index.c_str()] = "validated";
        jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
        jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();

        auto jv = wsc->invoke("book_offers", jvParams);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
        auto jrr = jv.as_object()[jss::result.c_str()];

        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].is_array());
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].as_array().size() == 1);
        auto jrOffer = jrr.as_object()[jss::offers.c_str()].as_array()[0u];
        BEAST_EXPECT(jrOffer.as_object()[sfAccount.fieldName.c_str()].as_string() == alice.human());
        BEAST_EXPECT(
            jrOffer.as_object()[sfBookDirectory.fieldName.c_str()].as_string() ==
            getBookDir(env, XRP, USD.issue()));
        BEAST_EXPECT(jrOffer.as_object()[sfBookNode.fieldName.c_str()] == "0");
        BEAST_EXPECT(jrOffer.as_object()[jss::Flags.c_str()] == 0);
        BEAST_EXPECT(jrOffer.as_object()[sfLedgerEntryType.fieldName.c_str()].as_string() == jss::Offer.c_str());
        BEAST_EXPECT(jrOffer.as_object()[sfOwnerNode.fieldName.c_str()] == "0");
        BEAST_EXPECT(jrOffer.as_object()[sfSequence.fieldName.c_str()] == 5);
        BEAST_EXPECT(
            jrOffer.as_object()[jss::TakerGets.c_str()] ==
            USD(10).value().getJson(JsonOptions::none));
        BEAST_EXPECT(
            jrOffer.as_object()[jss::TakerPays.c_str()] ==
            XRP(4000).value().getJson(JsonOptions::none));
        BEAST_EXPECT(jrOffer.as_object()[jss::owner_funds.c_str()] == "100");
        BEAST_EXPECT(jrOffer.as_object()[jss::quality.c_str()] == "400000000");

        using namespace std::chrono_literals;
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jval) {
            auto & t = jval.as_object()[jss::transaction.c_str()].as_object();
            return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                t[jss::TakerGets.c_str()] ==
                USD(10).value().getJson(JsonOptions::none) &&
                t[jss::owner_funds.c_str()] == "100" &&
                t[jss::TakerPays.c_str()] ==
                XRP(4000).value().getJson(JsonOptions::none);
        }));

        env(offer(bob, XRP(2000), USD(5)));
        env.close();

        BEAST_EXPECT(wsc->findMsg(5s, [&](auto & jval) {
            auto & t = jval.as_object()[jss::transaction.c_str()].as_object();
            return t[jss::TransactionType.c_str()] == jss::OfferCreate.c_str() &&
                t[jss::TakerGets.c_str()] ==
                USD(5).value().getJson(JsonOptions::none) &&
                t[jss::owner_funds.c_str()] == "50" &&
                t[jss::TakerPays.c_str()] ==
                XRP(2000).value().getJson(JsonOptions::none);
        }));

        jv = wsc->invoke("book_offers", jvParams);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
        jrr = jv.as_object()[jss::result.c_str()];

        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].is_array());
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].as_array().size() == 2);
        auto jrNextOffer = jrr.as_object()[jss::offers.c_str()].as_array()[1u].as_object();
        BEAST_EXPECT(jrNextOffer[sfAccount.fieldName.c_str()].as_string() == bob.human());
        BEAST_EXPECT(
            jrNextOffer[sfBookDirectory.fieldName.c_str()].as_string() ==
            getBookDir(env, XRP, USD.issue()));
        BEAST_EXPECT(jrNextOffer[sfBookNode.fieldName.c_str()] == "0");
        BEAST_EXPECT(jrNextOffer[jss::Flags.c_str()] == 0);
        BEAST_EXPECT(jrNextOffer[sfLedgerEntryType.fieldName.c_str()].as_string() == jss::Offer.c_str());
        BEAST_EXPECT(jrNextOffer[sfOwnerNode.fieldName.c_str()] == "0");
        BEAST_EXPECT(jrNextOffer[sfSequence.fieldName.c_str()] == 5);
        BEAST_EXPECT(
            jrNextOffer[jss::TakerGets.c_str()] ==
            USD(5).value().getJson(JsonOptions::none));
        BEAST_EXPECT(
            jrNextOffer[jss::TakerPays.c_str()] ==
            XRP(2000).value().getJson(JsonOptions::none));
        BEAST_EXPECT(jrNextOffer[jss::owner_funds.c_str()] == "50");
        BEAST_EXPECT(jrNextOffer[jss::quality.c_str()] == "400000000");

        jv = wsc->invoke("unsubscribe", books);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(
                jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object()[jss::jsonrpc.c_str()] == "2.0");
            BEAST_EXPECT(
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object()[jss::ripplerpc.c_str()] == "2.0");
            BEAST_EXPECT(jv.as_object().contains(jss::id.c_str()) && jv.as_object()[jss::id.c_str()] == 5);
        }
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
    }

    // Check that a stream only sees the given OfferCreate once
    static bool
    offerOnlyOnceInStream(
        std::unique_ptr<WSClient> const& wsc,
        std::chrono::milliseconds const& timeout,
        jtx::PrettyAmount const& takerGets,
        jtx::PrettyAmount const& takerPays)
    {
        auto maybeJv = wsc->getMsg(timeout);
        // No message
        if (!maybeJv)
            return false;
        // wrong message
        if (!(*maybeJv).as_object().contains(jss::transaction.c_str()))
            return false;
        auto & t = (*maybeJv).as_object()[jss::transaction.c_str()].as_object();
        if (t[jss::TransactionType.c_str()] != jss::OfferCreate.c_str() ||
            t[jss::TakerGets.c_str()] != takerGets.value().getJson(JsonOptions::none) ||
            t[jss::TakerPays.c_str()] != takerPays.value().getJson(JsonOptions::none))
            return false;
        // Make sure no other message is waiting
        return wsc->getMsg(timeout) == std::nullopt;
    }

    void
    testCrossingSingleBookOffer()
    {
        testcase("Crossing single book offer");

        // This was added to check that an OfferCreate transaction is only
        // published once in a stream, even if it updates multiple offer
        // ledger entries

        using namespace jtx;
        Env env(*this);

        // Scenario is:
        //  - Alice and Bob place identical offers for USD -> XRP
        //  - Charlie places a crossing order that takes both Alice and Bob's

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const USD = gw["USD"];

        env.fund(XRP(1000000), gw, alice, bob, charlie);
        env.close();

        env(trust(alice, USD(500)));
        env(trust(bob, USD(500)));
        env.close();

        env(pay(gw, alice, USD(500)));
        env(pay(gw, bob, USD(500)));
        env.close();

        // Alice and Bob offer $500 for 500 XRP
        env(offer(alice, XRP(500), USD(500)));
        env(offer(bob, XRP(500), USD(500)));
        env.close();

        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;
        {
            // RPC subscribe to books stream
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = false;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
        }

        // Charlie places an offer that crosses Alice and Charlie's offers
        env(offer(charlie, USD(1000), XRP(1000)));
        env.close();
        env.require(offers(alice, 0), offers(bob, 0), offers(charlie, 0));
        using namespace std::chrono_literals;
        BEAST_EXPECT(offerOnlyOnceInStream(wsc, 1s, XRP(1000), USD(1000)));

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
    }

    void
    testCrossingMultiBookOffer()
    {
        testcase("Crossing multi-book offer");

        // This was added to check that an OfferCreate transaction is only
        // published once in a stream, even if it auto-bridges across several
        // books that are under subscription

        using namespace jtx;
        Env env(*this);

        // Scenario is:
        //  - Alice has 1 USD and wants 100 XRP
        //  - Bob has 100 XRP and wants 1 EUR
        //  - Charlie has 1 EUR and wants 1 USD and should auto-bridge through
        //    Alice and Bob

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        env.fund(XRP(1000000), gw, alice, bob, charlie);
        env.close();

        for (auto const& account : {alice, bob, charlie})
        {
            for (auto const& iou : {USD, EUR})
            {
                env(trust(account, iou(1)));
            }
        }
        env.close();

        env(pay(gw, alice, USD(1)));
        env(pay(gw, charlie, EUR(1)));
        env.close();

        env(offer(alice, XRP(100), USD(1)));
        env(offer(bob, EUR(1), XRP(100)));
        env.close();

        auto wsc = makeWSClient(env.app().config());
        boost::json::object books;

        {
            // RPC subscribe to multiple book streams
            books[jss::books.c_str()] = Json::arrayValue;
            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = false;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
                j[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            }

            {
                auto& j = books[jss::books.c_str()].as_array().emplace_back(boost::json::object()).as_object();
                j[jss::snapshot.c_str()] = false;
                j[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "EUR";
                j[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
                j[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success"))
                return;
        }

        // Charlies places an on offer for EUR -> USD that should auto-bridge
        env(offer(charlie, USD(1), EUR(1)));
        env.close();
        using namespace std::chrono_literals;
        BEAST_EXPECT(offerOnlyOnceInStream(wsc, 1s, EUR(1), USD(1)));

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv.as_object()[jss::status.c_str()] == "success");
    }

    void
    testBookOfferErrors()
    {
        testcase("BookOffersRPC Errors");
        using namespace jtx;
        Env env(*this);
        Account gw{"gw"};
        Account alice{"alice"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        auto USD = gw["USD"];

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = 10u;
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "lgrNotFound");
            BEAST_EXPECT(jrr.as_object()[jss::error_message.c_str()] == "ledgerNotFound");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] == "Missing field 'taker_pays'.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()] = boost::json::object();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] == "Missing field 'taker_gets'.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()] = "not an object";
            jvParams[jss::taker_gets.c_str()] = boost::json::object();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_pays', not object.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()] = boost::json::object();
            jvParams[jss::taker_gets.c_str()] = "not an object";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_gets', not object.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()] = boost::json::object();
            jvParams[jss::taker_gets.c_str()] = boost::json::object();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Missing field 'taker_pays.currency'.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = 1;
            jvParams[jss::taker_gets.c_str()] = boost::json::object();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_pays.currency', not string.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()] = boost::json::object();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Missing field 'taker_gets.currency'.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = 1;
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_gets.currency', not string.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "NOT_VALID";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "srcCurMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_pays.currency', bad currency.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "NOT_VALID";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "dstAmtMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_gets.currency', bad currency.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = 1;
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_gets.issuer', not string.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = 1;
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_pays.issuer', not string.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = gw.human() + "DEAD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_pays.issuer', bad issuer.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = toBase58(noAccount());
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_pays.issuer', bad issuer account one.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human() + "DEAD";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_gets.issuer', bad issuer.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = toBase58(noAccount());
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_gets.issuer', bad issuer account one.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = alice.human();
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Unneeded field 'taker_pays.issuer' "
                "for XRP currency specification.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = toBase58(xrpAccount());
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_pays.issuer', expected non-XRP issuer.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker.c_str()] = 1;
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker', not string.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker.c_str()] = env.master.human() + "DEAD";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(jrr.as_object()[jss::error_message.c_str()] == "Invalid field 'taker'.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker.c_str()] = env.master.human();
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "badMarket");
            BEAST_EXPECT(jrr.as_object()[jss::error_message.c_str()] == "No such market.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker.c_str()] = env.master.human();
            jvParams[jss::limit.c_str()] = "0";  // NOT an integer
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "invalidParams");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'limit', not unsigned integer.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Invalid field 'taker_gets.issuer', "
                "expected non-XRP issuer.");
        }

        {
            boost::json::object jvParams;
            jvParams[jss::ledger_index.c_str()] = "validated";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "USD";
            jvParams[jss::taker_pays.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "XRP";
            jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
            auto jrr = env.rpc(
                "json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
            BEAST_EXPECT(jrr.as_object()[jss::error.c_str()] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr.as_object()[jss::error_message.c_str()] ==
                "Unneeded field 'taker_gets.issuer' "
                "for XRP currency specification.");
        }
    }

    void
    testBookOfferLimits(bool asAdmin)
    {
        testcase("BookOffer Limits");
        using namespace jtx;
        Env env{*this, asAdmin ? envconfig() : envconfig(no_admin)};
        Account gw{"gw"};
        env.fund(XRP(200000), gw);
        // Note that calls to env.close() fail without admin permission.
        if (asAdmin)
            env.close();

        auto USD = gw["USD"];

        for (auto i = 0; i <= RPC::Tuning::bookOffers.rmax; i++)
            env(offer(gw, XRP(50 + 1 * i), USD(1.0 + 0.1 * i)));

        if (asAdmin)
            env.close();

        boost::json::object jvParams;
        jvParams[jss::limit.c_str()] = 1;
        jvParams[jss::ledger_index.c_str()] = "validated";
        jvParams[jss::taker_pays.c_str()].as_object()[jss::currency.c_str()] = "XRP";
        jvParams[jss::taker_gets.c_str()].as_object()[jss::currency.c_str()] = "USD";
        jvParams[jss::taker_gets.c_str()].as_object()[jss::issuer.c_str()] = gw.human();
        auto jrr =
            env.rpc("json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].is_array());
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].as_array().size() == (asAdmin ? 1u : 0u));
        // NOTE - a marker field is not returned for this method

        jvParams[jss::limit.c_str()] = 0u;
        jrr = env.rpc("json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].is_array());
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].as_array().size() == 0u);

        jvParams[jss::limit.c_str()] = RPC::Tuning::bookOffers.rmax + 1;
        jrr = env.rpc("json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].is_array());
        BEAST_EXPECT(
            jrr.as_object()[jss::offers.c_str()].as_array().size() ==
            (asAdmin ? RPC::Tuning::bookOffers.rmax + 1 : 0u));

        jvParams[jss::limit.c_str()] = Json::nullValue;
        jrr = env.rpc("json", "book_offers", serialize(jvParams)).as_object()[jss::result.c_str()];
        BEAST_EXPECT(jrr.as_object()[jss::offers.c_str()].is_array());
        BEAST_EXPECT(
            jrr.as_object()[jss::offers.c_str()].as_array().size() ==
            (asAdmin ? RPC::Tuning::bookOffers.rdefault : 0u));
    }

    void
    run() override
    {
        testOneSideEmptyBook();
        testOneSideOffersInBook();
        testBothSidesEmptyBook();
        testBothSidesOffersInBook();
        testMultipleBooksOneSideEmptyBook();
        testMultipleBooksOneSideOffersInBook();
        testMultipleBooksBothSidesEmptyBook();
        testMultipleBooksBothSidesOffersInBook();
        testTrackOffers();
        testCrossingSingleBookOffer();
        testCrossingMultiBookOffer();
        testBookOfferErrors();
        testBookOfferLimits(true);
        testBookOfferLimits(false);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Book, app, ripple, 1);

bool fg(boost::json::value const& jv) {
    auto & t = jv.as_object().at(jss::transaction.c_str()).as_object();
    return t.at(jss::TransactionType.c_str()) == jss::OfferCreate.c_str();

}
}  // namespace test
}  // namespace ripple


