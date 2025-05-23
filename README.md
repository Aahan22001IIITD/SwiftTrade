# Deribit Integration Application
# File Structure 
```
automated_dom.cpp -> basic version , automated code , for testing.

automated_odc.cpp -> optimised version , automated code ,for testing 

odc.cpp -> optimised documented code 

dom.cpp -> documented basic main

application_logs.txt -> basic logger for dom and its automated version 

Optimised application_logs.txt -> logger for both optimised versions 
 
LatencyLog.txt -> for latency logging for basic versions.

Optimised Latency Log.txt -> for latency logging for optimised versions.
```
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


# Benchmarking Testing Process

The following steps outline the benchmarking testing process to evaluate the latencies in the system. The files are:

```bash
    automated_odc.cpp , which is the optimized version
    automated_dom.cpp , which is the base version
```
## System Setup

1. **Open four terminals.**

2. **In the first terminal (Server Terminal):**
   - Compile the main file:
     ```bash
     g++ -std=c++17 file.cpp -o deribit_app -lcurl -lssl -lcrypto -lboost_system -lpthread
     ```

3. **The remaining three terminals will act as Client Terminals.**

**Running the Benchmark**

1. **Option 1: Place Orders Automatically**
   - Run option 1 to automatically place orders. 
   - Options 2 and 3 will be ignored during benchmarking.

2. **Option 4: Run the Benchmark**
   - Execute option 4 to initiate the benchmark test for latency measurements.

3. **Option 5: Record Latencies**
   - Execute option 5 to record the latency values in the respective log files for further processing.

4. **Option 6: Start Webserver**
   - Run option 6 to start the web server for client-server communication.

**Client Setup and Subscription**

1. **Open all three client terminals.**

2. **In each client terminal, connect to the server:**
   ```bash
   wscat -c ws://localhost:9002
   ```

3.  **Subscribe to market data streams:**
- Client 1
```
SUBSCRIBE:ETH-PERPETUAL
SUBSCRIBE:BTC-PERPETUAL
```
- Client 2
```
SUBSCRIBE:ETH-PERPETUAL
SUBSCRIBE:BTC-PERPETUAL
```
- Client 3
```
SUBSCRIBE:ETH-PERPETUAL
SUBSCRIBE:BTC-PERPETUAL
```
4. **Closing the Clients and Server**
- Close the client terminals one by one.
- Terminate the main server file's websocket connection.
- End the program by executing option 8.

5. **Final Latency Data**
- Latencies will be recorded in the respective log files.
- Process the recorded latencies for analysis.

# Bench Marking results
- The benchmarking results and statistics are stored in ```BenchMarking.pdf``` file.
- The Optimisation details are added in the file ```Bonus Domentation.cpp```.
