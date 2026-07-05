#pragma once

#include <map>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "Usings.h"
#include "Order.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "Trade.h"

class Orderbook
{
public:
    struct ExecutionStats
    {
        std::size_t acceptedAdds_{0};
        std::size_t rejectedAdds_{0};
        std::size_t acceptedCancels_{0};
        std::size_t rejectedCancels_{0};
        std::size_t acceptedModifies_{0};
        std::size_t rejectedModifies_{0};
        std::size_t rejectedFillAndKill_{0};
        std::size_t rejectedFillOrKill_{0};
        std::size_t tradesExecuted_{0};
        std::size_t ordersCancelled_{0};
        std::size_t ordersMatched_{0};
        std::size_t latencySamples_{0};
        std::uint64_t totalLatencyMicros_{0};
        std::uint64_t minLatencyMicros_{0};
        std::uint64_t maxLatencyMicros_{0};
    };

private:

    struct OrderEntry
    {
        OrderPointer order_{ nullptr };
        OrderPointers::iterator location_;
    };

    struct LevelData
    {
        Quantity quantity_{ };
        Quantity count_{ };

        enum class Action
        {
            Add,
            Remove,
            Match,
        };
    };

    std::unordered_map<Price, LevelData> data_;
    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;
    mutable std::mutex ordersMutex_;
    std::thread ordersPruneThread_;
    std::condition_variable shutdownConditionVariable_;
    std::atomic<bool> shutdown_{ false };
    bool tracingEnabled_{ false };
    std::ostream* traceStream_{ nullptr };
    mutable ExecutionStats executionStats_{};

    void PruneGoodForDayOrders();

    void CancelOrders(OrderIds orderIds);
    void CancelOrderInternal(OrderId orderId);

    void OnOrderCancelled(OrderPointer order);
    void OnOrderAdded(OrderPointer order);
    void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
    void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

    bool CanFullyFill(Side side, Price price, Quantity quantity) const;
    bool CanMatch(Side side, Price price) const;
    Trades MatchOrders();
    void RecordOperation(const char* action, bool accepted, std::chrono::steady_clock::time_point start, std::size_t extraTrades = 0);

public:

    Orderbook();
    Orderbook(const Orderbook&) = delete;
    void operator=(const Orderbook&) = delete;
    Orderbook(Orderbook&&) = delete;
    void operator=(Orderbook&&) = delete;
    ~Orderbook();

    Trades AddOrder(OrderPointer order);
    void CancelOrder(OrderId orderId);
    Trades ModifyOrder(OrderModify order);

    void EnableTracing(bool enable);
    void SetTraceStream(std::ostream& stream);
    void ResetExecutionStats();
    ExecutionStats GetExecutionStats() const;
    bool WriteCsvReport(const std::string& path) const;

    std::size_t Size() const;
    OrderbookLevelInfos GetOrderInfos() const;
};

