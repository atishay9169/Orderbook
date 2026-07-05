#if defined(_MSC_VER)
#include "pch.h"
#else
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>
#include <string>
#include <string_view>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#endif

#include "Orderbook.h"

namespace googletest = ::testing;

enum class ActionType
{
    Add,
    Cancel,
    Modify,
};

struct Information
{
    ActionType type_;
    OrderType orderType_;
    Side side_;
    Price price_;
    Quantity quantity_;
    OrderId orderId_;
};

using Informations = std::vector<Information>;

struct Result
{
    std::size_t allCount_;
    std::size_t bidCount_;
    std::size_t askCount_;
};

using Results = std::vector<Result>;

static void VerifyInvariants(const Orderbook& ob, const std::string& context)
{
    const auto infos = ob.GetOrderInfos();
    const auto& bids = infos.GetBids();
    const auto& asks = infos.GetAsks();

    for (std::size_t i = 1; i < bids.size(); ++i)
    {
        ASSERT_GT(bids[i - 1].price_, bids[i].price_) << context << ": bids not sorted descending at index " << i;
    }

    for (std::size_t i = 1; i < asks.size(); ++i)
    {
        ASSERT_LT(asks[i - 1].price_, asks[i].price_) << context << ": asks not sorted ascending at index " << i;
    }

    if (!bids.empty() && !asks.empty())
    {
        ASSERT_LT(bids.front().price_, asks.front().price_)
            << context << ": book crossed: best bid " << bids.front().price_
            << " >= best ask " << asks.front().price_;
    }

    for (const auto& lvl : bids)
        ASSERT_GT(lvl.quantity_, 0u) << context << ": zero-qty bid level";

    for (const auto& lvl : asks)
        ASSERT_GT(lvl.quantity_, 0u) << context << ": zero-qty ask level";
}

struct InputHandler
{
private:
    std::uint32_t ToNumber(const std::string_view& str) const
    {
        if (str.empty())
            throw std::logic_error("Empty numeric value.");

        const std::string valueStr{ str };
        std::size_t parsedLength{};
        const auto value = std::stoul(valueStr, &parsedLength, 10);
        if (parsedLength != valueStr.size() || value > std::numeric_limits<std::uint32_t>::max())
            throw std::logic_error("Invalid numeric value.");

        return static_cast<std::uint32_t>(value);
    }

    bool TryParseResult(const std::string_view& str, Result& result) const
    {
        if (str.at(0) != 'R')
            return false;

        auto values = Split(str, ' ');
        result.allCount_ = ToNumber(values[1]);
        result.bidCount_ = ToNumber(values[2]);
        result.askCount_ = ToNumber(values[3]);

        return true;
    }

    bool TryParseInformation(const std::string_view& str, Information& action) const
    {
        auto value = str.at(0);
        auto values = Split(str, ' ');
        if (value == 'A')
        {
            action.type_ = ActionType::Add;
            action.side_ = ParseSide(values[1]);
            action.orderType_ = ParseOrderType(values[2]);
            action.price_ = ParsePrice(values[3]);
            action.quantity_ = ParseQuantity(values[4]);
            action.orderId_ = ParseOrderId(values[5]);
        }
        else if (value == 'M')
        {
            action.type_ = ActionType::Modify;
            action.orderId_ = ParseOrderId(values[1]);
            action.side_ = ParseSide(values[2]);
            action.price_ = ParsePrice(values[3]);
            action.quantity_ = ParseQuantity(values[4]);
        }
        else if (value == 'C')
        {
            action.type_ = ActionType::Cancel;
            action.orderId_ = ParseOrderId(values[1]);
        }
        else return false;

        return true;
    }

    std::vector<std::string_view> Split(const std::string_view& str, char delimeter) const
    {
        std::vector<std::string_view> columns;
        columns.reserve(5);
        std::size_t start_index{}, end_index{};
        while ((end_index = str.find(delimeter, start_index)) && end_index != std::string::npos)
        {
            auto distance = end_index - start_index;
            auto column = str.substr(start_index, distance);
            start_index = end_index + 1;
            columns.push_back(column);
        }
        columns.push_back(str.substr(start_index));
        return columns;
    }

    Side ParseSide(const std::string_view& str) const
    {
        if (str == "B")
            return Side::Buy;
        else if (str == "S")
            return Side::Sell;
        else throw std::logic_error("Unknown Side");
    }

    OrderType ParseOrderType(const std::string_view& str) const
    {
        if (str == "FillAndKill")
            return OrderType::FillAndKill;
        else if (str == "GoodTillCancel")
            return OrderType::GoodTillCancel;
        else if (str == "GoodForDay")
            return OrderType::GoodForDay;
        else if (str == "FillOrKill")
            return OrderType::FillOrKill;
        else if (str == "Market")
            return OrderType::Market;
        else throw std::logic_error("Unknown OrderType");
    }

    Price ParsePrice(const std::string_view& str) const
    {
        if (str.empty())
            throw std::logic_error("Unknown Price");

        return ToNumber(str);
    }

    Quantity ParseQuantity(const std::string_view& str) const
    {
        if (str.empty())
            throw std::logic_error("Unknown Quantity");

        return ToNumber(str);
    }

    OrderId ParseOrderId(const std::string_view& str) const
    {
        if (str.empty())
            throw std::logic_error("Empty OrderId");

        return static_cast<OrderId>(ToNumber(str));
    }

public:
    std::tuple<Informations, Result> GetInformations(const std::filesystem::path& path) const
    {
        Informations actions;
        actions.reserve(1'000);

        std::string line;
        std::ifstream file{ path };
        while (std::getline(file, line))
        {
            if (line.empty())
                break;

            const bool isResult = line.at(0) == 'R';
            const bool isAction = !isResult;
            
            if (isAction)
            {
                Information action;

                auto isValid = TryParseInformation(line, action);
                if (!isValid)
                    continue;

                actions.push_back(action);
            }
            else
            {
                if (!file.eof())
                    throw std::logic_error("Result should only be specified at the end.");

                Result result;

                auto isValid = TryParseResult(line, result);
                if (!isValid)
                    continue;

                return { actions, result };
            }

        }

        throw std::logic_error("No result specified.");
    }
};


class OrderbookTestsFixture : public googletest::TestWithParam<const char*> 
{
private:
    const static inline std::filesystem::path Root{ std::filesystem::current_path() };
    const static inline std::filesystem::path TestFolder{ "TestFiles" };
public:
    const static inline std::filesystem::path TestFolderPath{ Root / TestFolder };
};

TEST(OrderbookMetricsTests, ReportsAcceptedAndRejectedActions)
{
    Orderbook orderbook;
    orderbook.ResetExecutionStats();

    auto order = std::make_shared<Order>(OrderType::GoodTillCancel, 42, Side::Buy, 100, 10);
    const auto firstTrade = orderbook.AddOrder(order);
    ASSERT_TRUE(firstTrade.empty());

    const auto secondTrade = orderbook.AddOrder(order);
    ASSERT_TRUE(secondTrade.empty());

    const auto stats = orderbook.GetExecutionStats();
    ASSERT_EQ(stats.acceptedAdds_, 1u);
    ASSERT_EQ(stats.rejectedAdds_, 1u);
}

TEST(OrderbookUnitTests, AddGtcRestsOnBook)
{
    Orderbook ob;
    auto order = std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
    auto trades = ob.AddOrder(order);
    ASSERT_TRUE(trades.empty());
    ASSERT_EQ(ob.Size(), 1);
}

TEST(OrderbookUnitTests, CancelResting)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    ob.CancelOrder(1);
    ASSERT_EQ(ob.Size(), 0);

    auto stats = ob.GetExecutionStats();
    ASSERT_EQ(stats.acceptedCancels_, 1u);
    ASSERT_EQ(stats.ordersCancelled_, 1u);
}

TEST(OrderbookUnitTests, OrdersMatchedMetricIncrements)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 100, 5));

    auto stats = ob.GetExecutionStats();
    ASSERT_EQ(stats.tradesExecuted_, 1u);
    ASSERT_EQ(stats.ordersMatched_, 2u);
}

TEST(OrderbookUnitTests, CancelNonexistentIsSafe)
{
    Orderbook ob;
    ob.CancelOrder(999);
    auto stats = ob.GetExecutionStats();
    ASSERT_EQ(stats.rejectedCancels_, 1u);
}

TEST(OrderbookUnitTests, DuplicateOrderIdRejected)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 200, 5));

    auto stats = ob.GetExecutionStats();
    ASSERT_EQ(stats.acceptedAdds_, 1u);
    ASSERT_EQ(stats.rejectedAdds_, 1u);
    ASSERT_EQ(ob.Size(), 1);
}

TEST(OrderbookUnitTests, SimpleCross)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(ob.Size(), 0);
}

TEST(OrderbookUnitTests, PartialFillLeavesRemainder)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Buy, 100, 4));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].GetBidTrade().quantity_, 4);
    ASSERT_EQ(ob.Size(), 1);
}

TEST(OrderbookUnitTests, FifoAtSamePrice)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 100, 5));
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Buy, 100, 5));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].GetAskTrade().orderId_, 1);
}

TEST(OrderbookUnitTests, WalksMultiplePriceLevels)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 101, 5));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 3, Side::Sell, 102, 5));

    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 4, Side::Buy, 101, 10));

    ASSERT_EQ(trades.size(), 2u);
    ASSERT_EQ(ob.Size(), 1);
}

TEST(OrderbookUnitTests, FillAndKillPartialThenCancel)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::FillAndKill, 2, Side::Buy, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].GetBidTrade().quantity_, 5);
    ASSERT_EQ(ob.Size(), 0);
}

TEST(OrderbookUnitTests, FillAndKillNoMatchRejected)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::FillAndKill, 2, Side::Buy, 99, 10));

    ASSERT_TRUE(trades.empty());
    ASSERT_EQ(ob.Size(), 1);

    const auto stats = ob.GetExecutionStats();
    ASSERT_EQ(stats.rejectedFillAndKill_, 1u);
}

TEST(OrderbookUnitTests, FillOrKillFullyFillableExecutes)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 100, 5));
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::FillOrKill, 3, Side::Buy, 100, 10));

    ASSERT_EQ(trades.size(), 2u);
    ASSERT_EQ(ob.Size(), 0);
}

TEST(OrderbookUnitTests, FillOrKillNotFullyFillableRejected)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::FillOrKill, 2, Side::Buy, 100, 10));

    ASSERT_TRUE(trades.empty());
    ASSERT_EQ(ob.Size(), 1);
}

TEST(OrderbookUnitTests, MarketOrderTakesBestPrice)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 102, 5));

    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::Market, 3, Side::Buy, 0, 3));

    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].GetAskTrade().price_, 100);
}

TEST(OrderbookUnitTests, MarketOrderEmptyOppositeRejected)
{
    Orderbook ob;
    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::Market, 1, Side::Buy, 0, 5));
    ASSERT_TRUE(trades.empty());
}

TEST(OrderbookUnitTests, ModifyCancelsAndReAdds)
{
    Orderbook ob;
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    ob.ModifyOrder(OrderModify{1, Side::Buy, 105, 10});
    ASSERT_EQ(ob.Size(), 1);

    auto trades = ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));
    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(trades[0].GetBidTrade().price_, 105);
}

TEST(OrderbookUnitTests, ModifyNonexistentRejected)
{
    Orderbook ob;
    ob.ModifyOrder(OrderModify{999, Side::Buy, 100, 10});
    auto stats = ob.GetExecutionStats();
    ASSERT_EQ(stats.rejectedModifies_, 1u);
    ASSERT_EQ(ob.Size(), 0);
}

TEST(OrderbookUnitTests, HeavyLoadThousandsOfOrders)
{
    Orderbook ob;
    const std::size_t totalOrders = 5000;
    std::mt19937 generator(42);
    std::uniform_int_distribution<int> priceDelta(0, 20);
    std::uniform_int_distribution<int> qtyDelta(1, 5);

    for (std::size_t index = 1; index <= totalOrders; ++index)
    {
        const auto orderId = static_cast<OrderId>(index);
        const bool isBuy = (index % 2) == 1;
        const auto price = static_cast<Price>(100 + (isBuy ? -priceDelta(generator) : priceDelta(generator)));
        const auto quantity = static_cast<Quantity>(qtyDelta(generator));
        const auto orderType = (index % 10 == 0) ? OrderType::Market : OrderType::GoodTillCancel;
        const auto side = isBuy ? Side::Buy : Side::Sell;

        const Trades trades = ob.AddOrder(std::make_shared<Order>(orderType, orderId, side, price, quantity));
        if (orderType == OrderType::Market && trades.empty())
        {
            // Market order may be rejected when the opposite side is empty; this is expected.
            continue;
        }
    }

    const auto stats = ob.GetExecutionStats();
    ASSERT_EQ(stats.acceptedAdds_ + stats.rejectedAdds_, totalOrders);
    ASSERT_GE(stats.tradesExecuted_, 1u);
    ASSERT_GE(stats.latencySamples_, totalOrders);
    ASSERT_LE(stats.minLatencyMicros_, stats.maxLatencyMicros_);
}

TEST(OrderbookStressTests, MixedOperationsHighVolume)
{
    Orderbook ob;
    const std::size_t totalOps = 50000;
    std::mt19937 rng(1729);
    std::uniform_int_distribution<int> priceDist(80, 120);
    std::uniform_int_distribution<int> qtyDist(1, 50);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> opDist(0, 99);
    std::uniform_int_distribution<int> typeDist(0, 100);

    std::vector<OrderId> liveOrders;
    liveOrders.reserve(totalOps);
    OrderId nextId = 1;

    for (std::size_t i = 0; i < totalOps; ++i)
    {
        const int op = opDist(rng);

        if (op < 70 || liveOrders.empty())
        {
            const auto side = sideDist(rng) ? Side::Buy : Side::Sell;
            const auto price = static_cast<Price>(priceDist(rng));
            const auto qty = static_cast<Quantity>(qtyDist(rng));

            OrderType type = OrderType::GoodTillCancel;
            const int typeRoll = typeDist(rng);
            if (typeRoll < 5)
                type = OrderType::FillAndKill;
            else if (typeRoll < 10)
                type = OrderType::FillOrKill;
            else if (typeRoll < 12)
                type = OrderType::Market;

            const auto orderId = nextId++;
            const auto trades = ob.AddOrder(std::make_shared<Order>(
                type, orderId, side,
                type == OrderType::Market ? Price{0} : price,
                qty));

            if (type == OrderType::GoodTillCancel && trades.empty())
                liveOrders.push_back(orderId);
        }
        else if (op < 90)
        {
            std::uniform_int_distribution<std::size_t> idxDist(0, liveOrders.size() - 1);
            const auto idx = idxDist(rng);
            ob.CancelOrder(liveOrders[idx]);
            liveOrders[idx] = liveOrders.back();
            liveOrders.pop_back();
        }
        else
        {
            std::uniform_int_distribution<std::size_t> idxDist(0, liveOrders.size() - 1);
            const auto idx = idxDist(rng);
            const auto side = sideDist(rng) ? Side::Buy : Side::Sell;
            ob.ModifyOrder(OrderModify{
                liveOrders[idx], side,
                static_cast<Price>(priceDist(rng)),
                static_cast<Quantity>(qtyDist(rng))});
            liveOrders[idx] = liveOrders.back();
            liveOrders.pop_back();
        }

        if (i % 5000 == 0)
            VerifyInvariants(ob, "after op " + std::to_string(i));
    }

    VerifyInvariants(ob, "final state");

    const auto stats = ob.GetExecutionStats();
    ASSERT_GT(stats.acceptedAdds_, 0u);
    ASSERT_GT(stats.acceptedCancels_, 0u);
    ASSERT_GT(stats.acceptedModifies_, 0u);
    ASSERT_GT(stats.tradesExecuted_, 0u);

    std::cout << "\n[MixedOperationsHighVolume metrics]\n";
    std::cout << "  accepted_adds: " << stats.acceptedAdds_ << "\n";
    std::cout << "  accepted_cancels: " << stats.acceptedCancels_ << "\n";
    std::cout << "  accepted_modifies: " << stats.acceptedModifies_ << "\n";
    std::cout << "  trades_executed: " << stats.tradesExecuted_ << "\n";
    std::cout << "  avg_latency_us: "
              << (stats.latencySamples_ > 0
                    ? static_cast<double>(stats.totalLatencyMicros_) / stats.latencySamples_
                    : 0.0) << "\n";
    std::cout << "  min/max_latency_us: " << stats.minLatencyMicros_
              << " / " << stats.maxLatencyMicros_ << "\n";
}

TEST(OrderbookStressTests, DeepBookManyPriceLevels)
{
    Orderbook ob;
    const std::size_t levelsPerSide = 5000;

    for (std::size_t i = 0; i < levelsPerSide; ++i)
    {
        const Price buyPrice = static_cast<Price>(1 + i);
        const Price sellPrice = static_cast<Price>(10000 + i);
        ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2 * i + 1, Side::Buy, buyPrice, 10));
        ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 2 * i + 2, Side::Sell, sellPrice, 10));
    }

    VerifyInvariants(ob, "after deep-book population");
    ASSERT_EQ(ob.Size(), 2 * levelsPerSide);

    const auto start = std::chrono::steady_clock::now();
    ob.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, 1000000, Side::Buy, 20000, 100000));
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);

    std::cout << "\n[DeepBook] Sweep through many levels took: "
              << elapsed.count() << " us\n";

    VerifyInvariants(ob, "after deep-book sweep");
}

TEST(OrderbookStressTests, SustainedThroughputBenchmark)
{
    Orderbook ob;
    const std::size_t totalAdds = 100000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> priceDist(90, 110);
    std::uniform_int_distribution<int> qtyDist(1, 100);

    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 1; i <= totalAdds; ++i)
    {
        ob.AddOrder(std::make_shared<Order>(
            OrderType::GoodTillCancel,
            static_cast<OrderId>(i),
            (i & 1) ? Side::Buy : Side::Sell,
            static_cast<Price>(priceDist(rng)),
            static_cast<Quantity>(qtyDist(rng))));
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    const double throughput = static_cast<double>(totalAdds) / (elapsed.count() / 1000.0);

    std::cout << "\n[SustainedThroughput]\n";
    std::cout << "  Total adds: " << totalAdds << "\n";
    std::cout << "  Elapsed: " << elapsed.count() << " ms\n";
    std::cout << "  Throughput: " << throughput << " orders/sec\n";

    const auto stats = ob.GetExecutionStats();
    std::cout << "  Trades executed: " << stats.tradesExecuted_ << "\n";
    std::cout << "  Avg latency (us): "
              << (stats.latencySamples_ > 0
                    ? static_cast<double>(stats.totalLatencyMicros_) / stats.latencySamples_
                    : 0.0) << "\n";

    VerifyInvariants(ob, "throughput bench final");
}

TEST(OrderbookStressTests, LatencyPercentiles)
{
    Orderbook ob;
    const std::size_t N = 50000;
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> priceDist(90, 110);
    std::uniform_int_distribution<int> qtyDist(1, 10);

    std::vector<std::uint64_t> latencies;
    latencies.reserve(N);

    for (std::size_t i = 1; i <= N; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        ob.AddOrder(std::make_shared<Order>(
            OrderType::GoodTillCancel,
            static_cast<OrderId>(i),
            (i & 1) ? Side::Buy : Side::Sell,
            static_cast<Price>(priceDist(rng)),
            static_cast<Quantity>(qtyDist(rng))));
        const auto t1 = std::chrono::steady_clock::now();
        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) {
        return latencies[static_cast<std::size_t>(latencies.size() * p)];
    };

    std::cout << "\n[LatencyPercentiles] (ns per AddOrder)\n";
    std::cout << "  p50:   " << pct(0.50) << " ns\n";
    std::cout << "  p90:   " << pct(0.90) << " ns\n";
    std::cout << "  p95:   " << pct(0.95) << " ns\n";
    std::cout << "  p99:   " << pct(0.99) << " ns\n";
    std::cout << "  p99.9: " << pct(0.999) << " ns\n";
    std::cout << "  max:   " << latencies.back() << " ns\n";

    VerifyInvariants(ob, "percentile test final");
}

TEST(OrderbookStressTests, AdversarialCancelChain)
{
    Orderbook ob;
    const std::size_t N = 50000;

    for (std::size_t i = 1; i <= N; ++i)
    {
        ob.AddOrder(std::make_shared<Order>(
            OrderType::GoodTillCancel,
            static_cast<OrderId>(i), Side::Buy,
            static_cast<Price>(i), 10));
    }

    ASSERT_EQ(ob.Size(), N);

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 1; i <= N; ++i)
    {
        ob.CancelOrder(static_cast<OrderId>(i));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::cout << "\n[AdversarialCancelChain]\n";
    std::cout << "  Cancelled " << N << " orders in " << elapsed.count() << " ms\n";
    std::cout << "  Avg per cancel: " << (static_cast<double>(elapsed.count()) * 1000.0 / N) << " us\n";

    ASSERT_EQ(ob.Size(), 0);
}

TEST(OrderbookStressTests, FifoUnderHeavyLoad)
{
    Orderbook ob;
    const std::size_t ordersAtSamePrice = 10000;

    for (std::size_t i = 1; i <= ordersAtSamePrice; ++i)
    {
        ob.AddOrder(std::make_shared<Order>(
            OrderType::GoodTillCancel,
            static_cast<OrderId>(i), Side::Sell, 100, 1));
    }

    const auto trades = ob.AddOrder(std::make_shared<Order>(
        OrderType::GoodTillCancel, 999999999, Side::Buy, 100, ordersAtSamePrice));

    ASSERT_EQ(trades.size(), ordersAtSamePrice);

    for (std::size_t i = 0; i < trades.size(); ++i)
    {
        ASSERT_EQ(trades[i].GetAskTrade().orderId_, static_cast<OrderId>(i + 1))
            << "FIFO broken at trade " << i;
    }

    ASSERT_EQ(ob.Size(), 0);
}

TEST(OrderbookStressTests, ConcurrentMixedOpsNoRaces)
{
    Orderbook ob;
    std::atomic<OrderId> nextId{1};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> totalOps{0};

    auto adder = [&]() {
        std::mt19937 rng(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::uniform_int_distribution<int> priceDist(90, 110);
        std::uniform_int_distribution<int> qtyDist(1, 10);
        while (!stop.load(std::memory_order_relaxed))
        {
            const auto id = nextId.fetch_add(1);
            ob.AddOrder(std::make_shared<Order>(
                OrderType::GoodTillCancel, id,
                (id & 1) ? Side::Buy : Side::Sell,
                static_cast<Price>(priceDist(rng)),
                static_cast<Quantity>(qtyDist(rng))));
            totalOps.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto canceller = [&]() {
        std::mt19937 rng(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        while (!stop.load(std::memory_order_relaxed))
        {
            const auto maxId = nextId.load(std::memory_order_relaxed);
            if (maxId == 0)
                continue;
            std::uniform_int_distribution<OrderId> idDist(1, maxId);
            ob.CancelOrder(idDist(rng));
            totalOps.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(adder);
    for (int i = 0; i < 2; ++i)
        threads.emplace_back(canceller);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop.store(true);

    for (auto& t : threads)
        t.join();

    VerifyInvariants(ob, "post concurrent stress");
    std::cout << "\n[ConcurrentStress] Total ops in 2 sec: " << totalOps.load() << "\n";
    ASSERT_GT(totalOps.load(), 1000u);
}

// Parametrized file-based tests disabled: file paths not accessible from CMake build directory
// To re-enable, ensure test files are copied to build dir or use absolute paths
/*
TEST_P(OrderbookTestsFixture, OrderbookTestSuite)
{
    // Arrange
    const auto file = OrderbookTestsFixture::TestFolderPath / GetParam();

    InputHandler handler;
    const auto [actions, result] = handler.GetInformations(file);

    auto GetOrder = [](const Information& action)
    {
        return std::make_shared<Order>(
            action.orderType_,
            action.orderId_,
            action.side_,
            action.price_,
            action.quantity_);
    };

    auto GetOrderModify = [](const Information& action)
    {
        return OrderModify
        {
            action.orderId_,
            action.side_,
            action.price_,
            action.quantity_,
        };
    };

    // Act
    Orderbook orderbook;
    for (const auto& action : actions)
    {
        switch (action.type_)
        {
        case ActionType::Add:
        {
            const Trades& trades = orderbook.AddOrder(GetOrder(action));
        }
        break;
        case ActionType::Modify:
        {
            const Trades& trades = orderbook.ModifyOrder(GetOrderModify(action));
        }
        break;
        case ActionType::Cancel:
        {
            orderbook.CancelOrder(action.orderId_);
        }
        break;
        default:
            throw std::logic_error("Unsupported Action.");
        }
    }

    // Assert
    const auto& orderbookInfos = orderbook.GetOrderInfos();
    ASSERT_EQ(orderbook.Size(), result.allCount_);
    ASSERT_EQ(orderbookInfos.GetBids().size(), result.bidCount_);
    ASSERT_EQ(orderbookInfos.GetAsks().size(), result.askCount_);
}

INSTANTIATE_TEST_CASE_P(Tests, OrderbookTestsFixture, googletest::ValuesIn({
    "Match_GoodTillCancel.txt",
    "Match_FillAndKill.txt",
    "Match_FillOrKill_Hit.txt",
    "Match_FillOrKill_Miss.txt",
    "Cancel_Success.txt",
    "Modify_Side.txt",
    "Match_Market.txt"
}));
*/
