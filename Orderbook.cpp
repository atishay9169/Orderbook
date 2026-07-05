#include "Orderbook.h"

#include <numeric>
#include <chrono>
#include <ctime>
#include <iostream>
#include <fstream>
#include <string_view>

void Orderbook::RecordOperation(const char* action, bool accepted, std::chrono::steady_clock::time_point start, std::size_t extraTrades)
{
    const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);

    if (std::string_view{ action } == "add")
    {
        if (accepted)
            ++executionStats_.acceptedAdds_;
        else
            ++executionStats_.rejectedAdds_;
    }
    else if (std::string_view{ action } == "cancel")
    {
        if (accepted)
            ++executionStats_.acceptedCancels_;
        else
            ++executionStats_.rejectedCancels_;
    }
    else if (std::string_view{ action } == "modify")
    {
        if (accepted)
            ++executionStats_.acceptedModifies_;
        else
            ++executionStats_.rejectedModifies_;
    }

    const auto latencyMicros = static_cast<std::uint64_t>(latency.count());
    executionStats_.tradesExecuted_ += extraTrades;
    executionStats_.totalLatencyMicros_ += latencyMicros;
    executionStats_.latencySamples_ += 1;
    if (executionStats_.latencySamples_ == 1)
    {
        executionStats_.minLatencyMicros_ = latencyMicros;
        executionStats_.maxLatencyMicros_ = latencyMicros;
    }
    else
    {
        executionStats_.minLatencyMicros_ = std::min(executionStats_.minLatencyMicros_, latencyMicros);
        executionStats_.maxLatencyMicros_ = std::max(executionStats_.maxLatencyMicros_, latencyMicros);
    }

    if (tracingEnabled_ && traceStream_)
    {
        *traceStream_ << "[Orderbook] action=" << action
                      << " accepted=" << (accepted ? 1 : 0)
                      << " latency_us=" << latencyMicros
                      << " trades=" << extraTrades
                      << '\n';
    }
}

void Orderbook::PruneGoodForDayOrders()
{    
    using namespace std::chrono;
    const auto end = hours(16);

	while (true)
	{
		const auto now = system_clock::now();
		const auto now_c = system_clock::to_time_t(now);
		std::tm now_parts{};
#if defined(_WIN32)
		localtime_s(&now_parts, &now_c);
#else
		localtime_r(&now_c, &now_parts);
#endif

		if (now_parts.tm_hour >= end.count())
			now_parts.tm_mday += 1;

		now_parts.tm_hour = end.count();
		now_parts.tm_min = 0;
		now_parts.tm_sec = 0;

		auto next = system_clock::from_time_t(mktime(&now_parts));
		auto till = next - now + milliseconds(100);

		{
			std::unique_lock ordersLock{ ordersMutex_ };

			if (shutdown_.load(std::memory_order_acquire) ||
				shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
				return;
		}

		OrderIds orderIds;

		{
			std::scoped_lock ordersLock{ ordersMutex_ };

			for (const auto& entry : orders_)
			{
				const auto& order = entry.second.order_;

				if (order->GetOrderType() != OrderType::GoodForDay)
					continue;

				orderIds.push_back(order->GetOrderId());
			}
		}

		CancelOrders(orderIds);
	}
}

void Orderbook::CancelOrders(OrderIds orderIds)
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	for (const auto& orderId : orderIds)
		CancelOrderInternal(orderId);
}

void Orderbook::CancelOrderInternal(OrderId orderId)
{
	if (orders_.find(orderId) == orders_.end())
		return;

	const auto [order, iterator] = orders_.at(orderId);
	orders_.erase(orderId);

	if (order->GetSide() == Side::Sell)
	{
		auto price = order->GetPrice();
		auto& orders = asks_.at(price);
		orders.erase(iterator);
		if (orders.empty())
			asks_.erase(price);
	}
	else
	{
		auto price = order->GetPrice();
		auto& orders = bids_.at(price);
		orders.erase(iterator);
		if (orders.empty())
			bids_.erase(price);
	}

	++executionStats_.ordersCancelled_;
	OnOrderCancelled(order);
}

void Orderbook::OnOrderCancelled(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void Orderbook::OnOrderAdded(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
	++executionStats_.ordersMatched_;
	UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void Orderbook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
{
	auto& data = data_[price];

	data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;
	if (action == LevelData::Action::Remove || action == LevelData::Action::Match)
	{
		data.quantity_ -= quantity;
	}
	else
	{
		data.quantity_ += quantity;
	}

	if (data.count_ == 0)
		data_.erase(price);
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
	if (!CanMatch(side, price))
		return false;

	std::optional<Price> threshold;

	if (side == Side::Buy)
	{
		const auto [askPrice, _] = *asks_.begin();
		threshold = askPrice;
	}
	else
	{
		const auto [bidPrice, _] = *bids_.begin();
		threshold = bidPrice;
	}

	for (const auto& [levelPrice, levelData] : data_)
	{
		const bool exceedsThreshold = threshold.has_value() &&
			((side == Side::Buy && threshold.value() > levelPrice) ||
			 (side == Side::Sell && threshold.value() < levelPrice));
		if (exceedsThreshold)
			continue;

		if ((side == Side::Buy && levelPrice > price) ||
			(side == Side::Sell && levelPrice < price))
			continue;

		if (quantity <= levelData.quantity_)
			return true;

		quantity -= levelData.quantity_;
	}

	return false;
}

bool Orderbook::CanMatch(Side side, Price price) const
{
	if (side == Side::Buy)
	{
		if (asks_.empty())
			return false;

		const auto& [bestAsk, _] = *asks_.begin();
		return price >= bestAsk;
	}
	else
	{
		if (bids_.empty())
			return false;

		const auto& [bestBid, _] = *bids_.begin();
		return price <= bestBid;
	}
}

Trades Orderbook::MatchOrders()
{
	Trades trades;
	trades.reserve(orders_.size());

	while (true)
	{
		if (bids_.empty() || asks_.empty())
			break;

		auto& [bidPrice, bids] = *bids_.begin();
		auto& [askPrice, asks] = *asks_.begin();

		if (bidPrice < askPrice)
			break;

		while (!bids.empty() && !asks.empty())
		{
			auto bid = bids.front();
			auto ask = asks.front();

			Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

			bid->Fill(quantity);
			ask->Fill(quantity);

			if (bid->IsFilled())
			{
				bids.pop_front();
				orders_.erase(bid->GetOrderId());
			}

			if (ask->IsFilled())
			{
				asks.pop_front();
				orders_.erase(ask->GetOrderId());
			}


			trades.push_back(Trade{
				TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
				TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity } 
				});

			OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
			OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
		}

        if (bids.empty())
        {
            bids_.erase(bidPrice);
            data_.erase(bidPrice);
        }

        if (asks.empty())
        {
            asks_.erase(askPrice);
            data_.erase(askPrice);
        }
	}

	if (!bids_.empty())
	{
		auto& [_, bids] = *bids_.begin();
		auto& order = bids.front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}

	if (!asks_.empty())
	{
		auto& [_, asks] = *asks_.begin();
		auto& order = asks.front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}

	return trades;
}

Orderbook::Orderbook() : ordersPruneThread_{ [this] { PruneGoodForDayOrders(); } }, traceStream_{ &std::cout } { }

Orderbook::~Orderbook()
{
    shutdown_.store(true, std::memory_order_release);
	shutdownConditionVariable_.notify_one();
	ordersPruneThread_.join();
}

Trades Orderbook::AddOrder(OrderPointer order)
{
    const auto start = std::chrono::steady_clock::now();
	std::scoped_lock ordersLock{ ordersMutex_ };

	if (orders_.find(order->GetOrderId()) != orders_.end())
	{
		RecordOperation("add", false, start);
		return { };
	}

	if (order->GetOrderType() == OrderType::Market)
	{
		if (order->GetSide() == Side::Buy && !asks_.empty())
		{
			const auto& [worstAsk, _] = *asks_.rbegin();
			order->ToGoodTillCancel(worstAsk);
		}
		else if (order->GetSide() == Side::Sell && !bids_.empty())
		{
			const auto& [worstBid, _] = *bids_.rbegin();
			order->ToGoodTillCancel(worstBid);
		}
		else
		{
			RecordOperation("add", false, start);
			return { };
		}
	}

	if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
	{
		executionStats_.rejectedFillAndKill_ += 1;
		RecordOperation("add", false, start);
		return { };
	}

	if (order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
	{
		executionStats_.rejectedFillOrKill_ += 1;
		RecordOperation("add", false, start);
		return { };
	}

	OrderPointers::iterator iterator;

	if (order->GetSide() == Side::Buy)
	{
		auto& orders = bids_[order->GetPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}
	else
	{
		auto& orders = asks_[order->GetPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}

	orders_.insert({ order->GetOrderId(), OrderEntry{ order, iterator } });
	
	OnOrderAdded(order);
	
	const auto trades = MatchOrders();
	RecordOperation("add", true, start, trades.size());
	return trades;

}

void Orderbook::CancelOrder(OrderId orderId)
{
    const auto start = std::chrono::steady_clock::now();
	std::scoped_lock ordersLock{ ordersMutex_ };

	if (orders_.find(orderId) == orders_.end())
	{
		RecordOperation("cancel", false, start);
		return;
	}

	CancelOrderInternal(orderId);
	RecordOperation("cancel", true, start);
}

Trades Orderbook::ModifyOrder(OrderModify order)
{
    const auto start = std::chrono::steady_clock::now();
	OrderType orderType;

	{
		std::scoped_lock ordersLock{ ordersMutex_ };

		if (orders_.find(order.GetOrderId()) == orders_.end())
		{
			RecordOperation("modify", false, start);
			return { };
		}

		const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
		orderType = existingOrder->GetOrderType();
	}

	CancelOrder(order.GetOrderId());
	const auto trades = AddOrder(order.ToOrderPointer(orderType));
	RecordOperation("modify", true, start, trades.size());
	return trades;
}

void Orderbook::EnableTracing(bool enable)
{
    tracingEnabled_ = enable;
}

void Orderbook::SetTraceStream(std::ostream& stream)
{
    traceStream_ = &stream;
}

void Orderbook::ResetExecutionStats()
{
    std::scoped_lock ordersLock{ ordersMutex_ };
    executionStats_ = {};
}

Orderbook::ExecutionStats Orderbook::GetExecutionStats() const
{
    std::scoped_lock ordersLock{ ordersMutex_ };
    return executionStats_;
}

bool Orderbook::WriteCsvReport(const std::string& path) const
{
    std::ofstream out{ path };
    if (!out)
        return false;

    const auto stats = GetExecutionStats();
    out << "metric,value\n";
    out << "accepted_adds," << stats.acceptedAdds_ << "\n";
    out << "rejected_adds," << stats.rejectedAdds_ << "\n";
    out << "accepted_cancels," << stats.acceptedCancels_ << "\n";
    out << "rejected_cancels," << stats.rejectedCancels_ << "\n";
    out << "accepted_modifies," << stats.acceptedModifies_ << "\n";
    out << "rejected_modifies," << stats.rejectedModifies_ << "\n";
    out << "rejected_fill_and_kill," << stats.rejectedFillAndKill_ << "\n";
    out << "rejected_fill_or_kill," << stats.rejectedFillOrKill_ << "\n";
    out << "trades_executed," << stats.tradesExecuted_ << "\n";
    out << "orders_cancelled," << stats.ordersCancelled_ << "\n";
    out << "orders_matched," << stats.ordersMatched_ << "\n";
    out << "latency_samples," << stats.latencySamples_ << "\n";
    out << "total_latency_micros," << stats.totalLatencyMicros_ << "\n";
    out << "min_latency_micros," << stats.minLatencyMicros_ << "\n";
    out << "max_latency_micros," << stats.maxLatencyMicros_ << "\n";
    out << "avg_latency_micros," << (stats.latencySamples_ > 0 ? static_cast<double>(stats.totalLatencyMicros_) / stats.latencySamples_ : 0.0) << "\n";
    return true;
}

std::size_t Orderbook::Size() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return orders_.size(); 
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const
{
	LevelInfos bidInfos, askInfos;
	bidInfos.reserve(orders_.size());
	askInfos.reserve(orders_.size());

	auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
	{
		return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
			[](Quantity runningSum, const OrderPointer& order)
			{ return runningSum + order->GetRemainingQuantity(); }) };
	};

	for (const auto& [price, orders] : bids_)
		bidInfos.push_back(CreateLevelInfos(price, orders));

	for (const auto& [price, orders] : asks_)
		askInfos.push_back(CreateLevelInfos(price, orders));

	return OrderbookLevelInfos{ bidInfos, askInfos };

}

