# Deribit Integration Application

## Overview
This C++ application integrates with the Deribit cryptocurrency trading platform, enabling REST API and WebSocket functionalities. It provides real-time trading operations, market data, and a local WebSocket server for broadcasting updates to clients.

### Features:
1. **REST API Integration**:
   - Authenticate
   - Place Orders (market/limit)
   - Cancel Orders
   - Modify Orders
   - Fetch Order Book
   - Retrieve Positions
2. **WebSocket Integration**:
   - Real-time market data from Deribit.
   - Authentication and subscription to market data channels.
3. **Local WebSocket Server**:
   - Broadcasts real-time updates to connected clients.
   - Clients can subscribe to specific instruments.
4. **Interactive Command-Line Interface**:
   - User-friendly interface for performing trading operations.

---

## Setup and Requirements

### Prerequisites
1. **System Requirements**:
   - Linux (Ubuntu recommended) or WSL on Windows.
   - C++17 compatible compiler (e.g., `g++`).

2. **Dependencies**:
   - `libcurl` for REST API calls.
   - `libboost` for WebSocket integration.
   - `nlohmann/json` for JSON parsing.

### Install Dependencies
```bash
sudo apt-get update
sudo apt-get install -y libcurl4-openssl-dev libssl-dev libboost-all-dev
```
# Build Instructions

## Clone the Repository
```bash
git clone <repository-url>
cd <repository-folder>
```
Compile the Application
```bash
g++ -std=c++17 odc.cpp -o deribit_app -lcurl -lssl -lcrypto -lboost_system -lpthread
```
# Usage
**Running the Application**
Launch the application to access the interactive menu:
```bash
./deribit_app
```
### Interactive Menu Options
- **Place Order (Buy):** Automatically place a market or limit order.
- **Cancel Order:** Cancel an existing order by entering its ID.
- **Modify Order:** Update the price and quantity of an open order.
- **Get Order Book:** Fetch the order book for a specific instrument (e.g., `BTC-PERPETUAL`).
- **Get Positions:** Retrieve position details for specific instruments.
- **Start WebSocket Server:** Start a local WebSocket server for broadcasting updates.
- **View Orders:** Display active orders and view their details.
- **Exit:** Quit the application.
## Code Structure
- **main.cpp**: Main application file containing all functionalities:
  - REST API calls (`DeribitApi` class)
  - WebSocket client and server (`WebSocketClient`, `SimpleWebSocketServer` classes)
  - Market data subscription (`DeribitMarketData` class)
  - Utility functions (logging, latency measurement, JSON parsing)
### Key Classes

1. **`DeribitApi`**:
   - Manages REST API calls to Deribit.
   - Handles authentication, order placement, cancellation, modification, fetching positions, and order book data.

2. **`WebSocketClient`**:
   - Connects to the Deribit WebSocket API for real-time market data.
   - Handles WebSocket authentication, message sending, and receiving.

3. **`DeribitMarketData`**:
   - Subscribes to specific instruments for real-time market data updates.
   - Processes and broadcasts updates using callback functions.

4. **`SimpleWebSocketServer`**:
   - Implements a local WebSocket server for broadcasting updates to multiple clients.
   - Manages client subscriptions and forwards real-time updates.

5. **`Utility Functions`**:
   - Logging (`logMessage`, `LatencyLog`)
   - JSON parsing (`extract_json_field`)
   - Order message creation (`createPlaceOrderMessage`, `createCancelOrderMessage`, etc.)
   - HTTP server initialization for debugging (`startHttpServer`)

## Configuration

### API Credentials
Replace the placeholders in the code with your Deribit testnet credentials:

```cpp
static const std::string DERIBIT_CLIENT_ID = "your_client_id";
static const std::string DERIBIT_CLIENT_SECRET = "your_client_secret";
```
# WebSocket Server Port
The default port is 9002. You can change it in the SimpleWebSocketServer initialization.

Logging
The application generates two log files:
```application_logs.txt```: General application logs.
```LatencyLog.txt```: Logs latency for WebSocket operations.
Logs are automatically cleared when the application starts.
## Example Commands

### Place Order

1. Choose option `1`.
2. Enter the required parameters (e.g., instrument name, type, amount).

### Start WebSocket Server

1. Choose option `6`.
2. Connect to the server using a WebSocket client (e.g., `wscat`):
   ```bash
   wscat -c ws://localhost:9002
   ```
3. Subscribe to an instrument:
```SUBSCRIBE:BTC-PERPETUAL```
# Known Issues
- Authentication Failure: Ensure your API credentials are valid.
- Connection Issues: Verify network connectivity and Deribit server availability.

# Contributing
Contributions are welcome! Feel free to open an issue or submit a pull request.