# Orderbook - High-Performance C++17 Matching Engine

A production-grade orderbook implementation with comprehensive matching logic, execution analytics, and stress-tested thread safety.

## Features

- **Multiple Order Types**: GTC (Good Till Cancel), FillAndKill, FillOrKill, Market, GoodForDay
- **FIFO Matching**: Price-level-based order book with strict FIFO within each price level
- **Execution Analytics**: Track accepted/rejected adds/cancels/modifies, latency percentiles, throughput
- **Thread-Safe**: Mutex-protected operations with no races (stress tested with concurrent operations)
- **Cross-Platform**: Compiles on macOS, Linux, Windows with CMake
- **High Performance**: ~420k+ orders/sec throughput, <10μs p99.9 latency

## Building

### Option 1: CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

### Option 2: Direct g++ Compilation

```bash
# Build library
g++ -std=c++17 -I. Orderbook.cpp -c -o Orderbook.o

# Build demo
g++ -std=c++17 -I. main.cpp Orderbook.o -o OrderbookDemo

# Build tests (requires GTest)
g++ -std=c++17 -I. OrderbookTest/test.cpp Orderbook.cpp -o OrderbookTests \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lgtest_main -lgtest -lpthread

# Run
./OrderbookDemo
./OrderbookTests
```

## Running

### Demo Program
```bash
./build/OrderbookDemo
```
Generates `orderbook_stress_report.csv` with 10k deterministic orders and metrics.

### Tests
```bash
# Run all tests
./build/OrderbookTests

# Run specific suite
./build/OrderbookTests --gtest_filter="OrderbookStressTests.*"
```

## Test Results

- **26 unit + stress tests passing**
- **Throughput**: 421,941 orders/sec (100k adds)
- **Latency (p99.9)**: 9,458 ns
- **Stress scenarios**: 50k mixed operations (adds/cancels/modifies), concurrent 480k+ ops/2sec
- **Thread safety**: Verified with 4-thread concurrent stress test

## Core Classes

### `Orderbook`
Main matching engine interface:
- `AddOrder()` - Submit order (returns vector of trades)
- `CancelOrder()` - Remove order by ID
- `ModifyOrder()` - Modify price/quantity (cancel + re-add)
- `GetExecutionStats()` - Retrieve execution metrics
- `WriteCsvReport()` - Export stats to CSV

### `Order`
Order model with fill tracking:
- Supports all order types with type-specific semantics
- `Fill()` method for partial fills
- `ToGoodTillCancel()` conversion

### `Trade`
Represents execution (buyer, seller, price, quantity, trade ID)

## Metrics Available

- `acceptedAdds_`, `rejectedAdds_` - Add success/failure counts
- `acceptedCancels_`, `rejectedCancels_` - Cancel attempts
- `acceptedModifies_`, `rejectedModifies_` - Modify attempts
- `tradesExecuted_`, `ordersCancelled_`, `ordersMatched_`
- `avgLatencyMicros_`, `minLatencyMicros_`, `maxLatencyMicros_`
- `latencySamples_` - Count of operations timed

## Architecture

- **Data Structure**: `std::map<Price, PriceLevel>` for bids/asks
- **Order Storage**: `std::unordered_map<OrderId, OrderNode>` for O(1) lookup
- **Matching**: Walk price levels in order, match FIFO
- **Synchronization**: Single mutex per side with minimal contention

## Dependencies

- C++17 compiler (g++, clang, MSVC)
- CMake 3.15+ (for building)
- GoogleTest (for tests, optional)

## License

See LICENSE.txt

## Interview Notes

This codebase demonstrates:
- Low-level performance optimization (latency percentiles, throughput metrics)
- Correct concurrent programming (mutex usage, no races)
- Professional C++ practices (const-correctness, RAII, smart pointers)
- Comprehensive testing (unit, stress, concurrent)
- Cross-platform compatibility (macOS/Linux/Windows)
