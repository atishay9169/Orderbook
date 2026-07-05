#include "Orderbook.h"

#include <iostream>
#include <memory>
#include <random>

int main()
{
    Orderbook orderbook;
    orderbook.ResetExecutionStats();
    orderbook.EnableTracing(false);

    constexpr std::size_t totalOrders = 10000;
    std::mt19937_64 rng{ 42 };
    std::uniform_int_distribution<int> priceDelta(0, 20);
    std::uniform_int_distribution<int> quantityDist(1, 10);
    std::uniform_int_distribution<int> orderTypeDist(0, 9);

    for (std::size_t index = 1; index <= totalOrders; ++index)
    {
        const auto orderId = static_cast<OrderId>(index);
        const bool isBuy = (index % 2) == 1;
        const auto price = static_cast<Price>(100 + (isBuy ? -priceDelta(rng) : priceDelta(rng)));
        const auto quantity = static_cast<Quantity>(quantityDist(rng));
        const auto side = isBuy ? Side::Buy : Side::Sell;
        const auto type = (orderTypeDist(rng) == 0) ? OrderType::Market : OrderType::GoodTillCancel;

        if (type == OrderType::Market)
        {
            orderbook.AddOrder(std::make_shared<Order>(orderId, side, quantity));
        }
        else
        {
            orderbook.AddOrder(std::make_shared<Order>(type, orderId, side, price, quantity));
        }
    }

    const auto stats = orderbook.GetExecutionStats();

    std::cout << "Orderbook stress test complete:\n";
    std::cout << "  total_orders=" << totalOrders << "\n";
    std::cout << "  accepted_adds=" << stats.acceptedAdds_ << "\n";
    std::cout << "  rejected_adds=" << stats.rejectedAdds_ << "\n";
    std::cout << "  trades_executed=" << stats.tradesExecuted_ << "\n";
    std::cout << "  latency_samples=" << stats.latencySamples_ << "\n";
    std::cout << "  avg_latency_micros="
              << (stats.latencySamples_ > 0 ? static_cast<double>(stats.totalLatencyMicros_) / stats.latencySamples_ : 0.0)
              << "\n";

    const auto reportPath = "orderbook_stress_report.csv";
    if (!orderbook.WriteCsvReport(reportPath))
    {
        std::cerr << "Failed to write CSV report to: " << reportPath << "\n";
        return 1;
    }

    std::cout << "CSV report written to: " << reportPath << "\n";
    return 0;
}
