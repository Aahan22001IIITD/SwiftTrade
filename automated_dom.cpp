
/******************************************************************************
 * A Complete C++ Code for Deribit (Test) integration
 *
 * Features:
 *   1. REST calls with libcurl:
 *      - Authenticate
 *      - Place Order
 *      - Cancel Order
 *      - Modify Order
 *      - Get Order Book
 *      - Get Positions
 *   2. Real-time market data using Deribit's WebSocket (Boost.Beast)
 *   3. A simple WebSocket server to broadcast updates to connected clients
 *
 *
 *
 *
 *****************************************************************************/
#include <deque>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <sstream>
#include <memory>
#include <vector>
#include <cstdlib>
using namespace std;
/*********************************
 * 1) INCLUDE CURL FOR REST
 *********************************/
#include <curl/curl.h>

/*********************************
 * 2) INCLUDE BOOST FOR WEBSOCKET
 *********************************/
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
using json = nlohmann::json;
namespace Beast = boost::beast;
namespace Http = Beast::http;
namespace WebSocket = Beast::websocket;
namespace Net = boost::asio;
namespace BoostSSL = boost::asio::ssl;

using tcp = boost::asio::ip::tcp;
mutex dataMutex;
string latestOrderBookJson;
/*********************************
 * Global Constants / Config
 *********************************/
static const string DERIBIT_CLIENT_ID = "VlT9vgkU";
static const string DERIBIT_CLIENT_SECRET = "8Wj5UWJ2LinPYgV8KckQuQxJYfozaZDMyqaOX96nsHI";

// Deribit Testnet
static const string DERIBIT_REST_HOST = "https://test.deribit.com";
static const string DERIBIT_WS_HOST = "test.deribit.com";
static const string DERIBIT_WS_PORT = "443";

/*********************************
 * Utility for JSON (simple)
 *********************************/

#include <sstream>
#include <iomanip>
#include <algorithm>
// Global atomic flags for controlling execution flow and shutdown
atomic<bool> shuttingDown{false};
atomic<bool> running{false};
void clearLogFile()
{
    // First log file
    {
        string filename = "application_logs.txt";
        ofstream file(filename, ios::trunc);
        if (file.is_open())
        {
            cout << "Log file 1 cleared successfully!" << endl;
            file.close();
        }
        else
        {
            cerr << "Failed to open the file: " << filename << endl;
        }
    }

    // Second log file
    {
        string filename = "LatencyLog.txt";
        ofstream file(filename, ios::trunc);
        if (file.is_open())
        {
            cout << "Log file 2 cleared successfully!" << endl;
            file.close();
        }
        else
        {
            cerr << "Failed to open the file: " << filename << endl;
        }
    }
}

void logMessage(const string &message)
{
    static ofstream logFile("application_logs.txt", ios::app);
    if (logFile.is_open())
    {
        logFile << message << endl;
    }
}
void LatencyLog(const string &message)
{
    static ofstream logFile("LatencyLog.txt", ios::app);
    if (logFile.is_open())
    {
        logFile << message << endl;
    }
}
string formatHtml(const string &jsonData)
{
    ostringstream html;
    html << "<html><head><title>Order Book</title></head><body>"
         << "<h1>Order Book Updates</h1>"
         << "<pre style='background:#f4f4f4;padding:15px;border:1px solid #ccc;'>"
         << jsonData << "</pre>"
         << "</body></html>";
    return html.str();
}
void startHttpServer(unsigned short port)
{
    try
    {
        Net::io_context ioc;
        tcp::acceptor acceptor{ioc, tcp::endpoint(tcp::v4(), port)};

        cout << "[HTTP Server] Listening on port " << port << "\n";

        while (true)
        {
            tcp::socket socket{ioc};
            acceptor.accept(socket);

            Beast::flat_buffer buffer;
            auto const response = [&]()
            {
                lock_guard<mutex> lock(dataMutex);
                return latestOrderBookJson.empty() ? formatHtml("No data available yet.") : formatHtml(latestOrderBookJson);
            }();
            auto const body = response;
            auto const size = body.size();
            Http::response<Http::string_body> res{Http::status::ok, 11};
            res.set(Http::field::server, "Beast/Boost");
            res.set(Http::field::content_type, "text/html");
            res.content_length(size);
            res.body() = body;
            Beast::http::write(socket, res);
        }
    }
    catch (const exception &e)
    {
        cerr << "[HTTP Server] Exception: " << e.what() << "\n";
    }
}

static string extract_json_field(const string &json, const string &field)
{
    string needle = "\"" + field + "\":";
    auto pos = json.find(needle);
    if (pos == string::npos)
    {
        return "";
    }
    pos += needle.size();
    while (pos < json.size() && json[pos] == ' ')
    {
        pos++;
    }
    string result;
    if (pos < json.size() && json[pos] == '\"')
    {
        pos++;
        while (pos < json.size() && json[pos] != '\"')
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                result.push_back(json[pos + 1]);
                pos += 2;
            }
            else
            {
                result.push_back(json[pos]);
                pos++;
            }
        }
    }
    else
    {
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ')
        {
            result.push_back(json[pos]);
            pos++;
        }
    }
    return result;
}

/*********************************
 * Helper for libcurl
 *********************************/
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

/*********************************
 * Deribit REST API Class
 *********************************/
class DeribitApi
{
public:
    DeribitApi(const string &client_id, const string &client_secret)
        : clientId_(client_id), clientSecret_(client_secret)
    {
        curl_global_init(CURL_GLOBAL_ALL);
        // attempting to authenticate right away
        authenticate();
    }

    ~DeribitApi()
    {
        curl_global_cleanup();
    }

    /**
     * This will re-authenticate with Deribit using client credentials
     */
    bool authenticate()
    {
        string url = DERIBIT_REST_HOST + "/api/v2/public/auth";

        // Construct JSON request
        json requestBody = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "public/auth"},
            {"params", {{"grant_type", "client_credentials"}, {"client_id", clientId_}, {"client_secret", clientSecret_}}}};
        string postData = requestBody.dump();
        string response;
        CURL *curl = curl_easy_init();
        if (!curl)
            return false;
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK)
        {
            cerr << "[Auth] Authentication request failed: "
                 << curl_easy_strerror(res) << endl;
            return false;
        }

        // prse the JSON response
        try
        {
            auto jsonResp = json::parse(response);
            if (jsonResp.contains("error"))
            {
                cerr << "[Auth] Error in response: "
                     << jsonResp["error"]["message"] << endl;
                return false;
            }

            auto result = jsonResp["result"];
            accessToken_ = result["access_token"].get<string>();
            refreshToken_ = result["refresh_token"].get<string>();
            tokenExpiresIn_ = result["expires_in"].get<int>();
            tokenIssueTime_ = chrono::system_clock::now();
            cout << "[AUTH]:Successfulluy Autheticated with the server! ";
            // cout << "[Auth] Successfully authenticated. Token: "
            //      << accessToken_ << endl;
            return true;
        }
        catch (const exception &e)
        {
            cerr << "[Auth] Exception while parsing response: "
                 << e.what() << endl;
            return false;
        }
    }
    // for demonstration  storing the token
    bool refreshToken()
    {
        // Correct URL for token refresh
        string url = DERIBIT_REST_HOST + "/api/v2/public/auth";
        string post = "grant_type=refresh_token&refresh_token=" + refreshToken_;
        string response;
        CURL *curl = curl_easy_init();
        if (!curl)
            return false;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK)
        {
            cerr << "[Auth] Refresh token request failed: " << curl_easy_strerror(res) << endl;
            return false;
        }

        // debuggingh the raw response to help identify the error
        cout << "[AUTH] We got a new refresh token !" << endl;
        cout << "[Auth] Refresh response: " << response << endl;
        // Parsing the response for the new access token and refresh token
        string resultObj = extract_json_field(response, "result");
        if (resultObj.empty())
        {
            cerr << "[RAuth] Could not find 'result' in response:\n"
                 << response << endl;
            return false;
        }
        accessToken_ = extract_json_field(resultObj, "access_token");
        refreshToken_ = extract_json_field(resultObj, "refresh_token");
        tokenExpiresIn_ = stoi(extract_json_field(resultObj, "expires_in"));
        tokenIssueTime_ = chrono::system_clock::now();
        if (accessToken_.empty())
        {
            cerr << "[Auth] Failed to parse access_token from result:\n"
                 << response << endl;
            return false;
        }
        // cout<
        cout << "[Auth] Successfully refreshed token. New token: " << accessToken_ << endl;
        return true;
    }

    string getAccessToken() const
    {
        return accessToken_;
    }

    /**
     * Place an order
     *   side: "buy" or "sell"
     *   instrument_name: e.g. "BTC-PERPETUAL"
     *   type: "limit" or "market"
     *   amount: quantity
     *   price: only if type="limit"
     */
    /**
     * Place an order on Deribit exchange.
     *
     * This function places either a 'buy' or 'sell' order depending on the 'side' parameter.
     * It supports both 'limit' and 'market' order types.
     *
     * @param side - "buy" or "sell", determines whether the order is a buy or sell.
     * @param instrument_name - The name of the instrument (e.g., "BTC-USD").
     * @param type - The type of order ("limit" or "market").
     * @param amount - The number of contracts to buy or sell.
     * @param price - (optional) The price for a limit order. Default is 0.0 (for market orders).
     * @return string - The response from the API after placing the order.
     */
    string placeOrder(const string &side,
                      const string &instrument_name,
                      const string &type,
                      double amount,
                      double price = 0.0)
    {
        // Ensure the token is valid before placing an order.
        if (!ensureTokenIsValid())
        {
            cerr << "[Error] Failed to authenticate. Ensure valid credentials.\n";
            return "";
        }

        // Determine the endpoint based on the 'side' parameter.
        string endpoint = (side == "buy") ? "/api/v2/private/buy" : "/api/v2/private/sell";

        // Construct the base URL for the order request.
        string url = DERIBIT_REST_HOST + endpoint +
                     "?instrument_name=" + instrument_name +
                     "&amount=" + to_string(amount);

        // Add additional parameters depending on the order type (limit or market).
        if (type == "limit")
        {
            url += "&type=limit&price=" + to_string(price);
        }
        else
        {
            url += "&type=market";
        }

        // Append the access token for authentication.
        url += "&access_token=" + accessToken_;

        logMessage("[placeOrder]: " + url); // Log the URL for debugging.

        // Execute the POST request to place the order and return the response.
        return executePostRequest(url);
    }

    /**
     * Cancel an existing order using its order_id.
     *
     * @param order_id - The unique ID of the order to cancel.
     * @return string - The response from the API after attempting to cancel the order.
     */
    string cancelOrder(const string &order_id)
    {
        // Ensure the token is valid before cancelling an order.
        if (!ensureTokenIsValid())
        {
            cerr << "[Error] Failed to authenticate. Ensure valid credentials.\n";
            return "";
        }

        // Construct the URL for the cancel order API request.
        string url = DERIBIT_REST_HOST + "/api/v2/private/cancel?order_id=" + order_id;

        string response;
        CURL *curl = curl_easy_init(); // Initialize the CURL session.
        if (!curl)
            return "";

        // Set up headers for authentication.
        struct curl_slist *headers = NULL;
        string auth_header = "Authorization: Bearer " + accessToken_;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set up other CURL options.
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // Perform the HTTP request to cancel the order.
        CURLcode res = curl_easy_perform(curl);

        // Clean up the CURL session.
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        // Check if the request was successful.
        if (res != CURLE_OK)
        {
            cerr << "cancelOrder failed: " << curl_easy_strerror(res) << endl;
            return "";
        }

        // Return the response from the server.
        cout << "[cancelOrder] response: " << response << endl;
        return response;
    }

    /**
     * Modify an existing order's price or amount.
     *
     * @param order_id - The unique ID of the order to modify.
     * @param new_price - The new price for the order.
     * @param new_amount - The new amount (quantity) for the order.
     * @return string - The response from the API after modifying the order.
     */
    string modifyOrder(const string &order_id,
                       double new_price,
                       double new_amount)
    {
        // Ensure the token is valid before modifying the order.
        if (!ensureTokenIsValid())
        {
            cerr << "[Error] Failed to authenticate. Ensure valid credentials.\n";
            return "";
        }

        // Construct the URL for modifying the order.
        string url = DERIBIT_REST_HOST + "/api/v2/private/edit?order_id=" + order_id +
                     "&amount=" + to_string(new_amount) +
                     "&price=" + to_string(new_price);

        // Execute the POST request to modify the order.
        return executePostRequest(url);
    }

    /**
     * Get the order book for a specific instrument.
     *
     * @param instrument_name - The name of the instrument (e.g., "BTC-USD").
     * @return string - The response from the API containing the order book.
     */
    string getOrderBook(const string &instrument_name)
    {
        // Ensure the token is valid before fetching the order book.
        if (!ensureTokenIsValid())
            return "";

        // Construct the URL to fetch the order book for the instrument.
        string url = DERIBIT_REST_HOST + "/api/v2/public/get_order_book?instrument_name=" + instrument_name;

        string response;
        CURL *curl = curl_easy_init(); // Initialize the CURL session.
        if (!curl)
            return "";

        // Set up CURL options.
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // Perform the HTTP request to get the order book.
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        // Check if the request was successful.
        if (res != CURLE_OK)
        {
            cerr << "getOrderBook failed: " << curl_easy_strerror(res) << endl;
            return "";
        }

        // Return the order book response.
        cout << "[getOrderBook] response: " << response << endl;
        return response;
    }

    /**
     * Get the user's positions on the Deribit exchange.
     *
     * This function retrieves all positions by default or a specific instrument if provided.
     *
     * @param instrument_name - (optional) The instrument name to filter positions.
     * @return string - The response from the API containing the positions.
     */
    string getPositions(const string &instrument_name = "")
    {
        // Ensure the token is valid before fetching positions.
        if (!ensureTokenIsValid())
            return "";

        // Construct the URL for fetching positions. If an instrument name is provided, include it.
        string url = DERIBIT_REST_HOST + "/api/v2/private/get_positions?";
        if (!instrument_name.empty())
        {
            url += "instrument_name=" + instrument_name;
        }
        else
        {
            // If no instrument name is provided, default to fetching BTC positions.
            url += "currency=BTC";
        }

        string response;
        CURL *curl = curl_easy_init(); // Initialize the CURL session.
        if (!curl)
            return "";

        // Set up headers for authentication.
        struct curl_slist *headers = NULL;
        string auth_header = "Authorization: Bearer " + accessToken_;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set up other CURL options.
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // Perform the HTTP request to get the positions.
        CURLcode res = curl_easy_perform(curl);

        // Clean up the CURL session.
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        // Check if the request was successful.
        if (res != CURLE_OK)
        {
            cerr << "getPositions failed: " << curl_easy_strerror(res) << endl;
            return "";
        }

        // Return the response containing the user's positions.
        cout << "[getPositions] response: " << response << endl;
        return response;
    }

private:
    string executePostRequest(const string &url)
    {
        string response;
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            cerr << "[Error] CURL initialization failed.\n";
            return "";
        }

        struct curl_slist *headers = nullptr;
        string auth_header = "Authorization: Bearer " + accessToken_;
        headers = curl_slist_append(headers, auth_header.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            cerr << "[Error] CURL request failed: " << curl_easy_strerror(res) << "\n";
            return "";
        }

        cout << "[Debug] Response: " << response << "\n";
        return response;
    }

    bool ensureTokenIsValid()
    {
        auto now = chrono::system_clock::now();
        int elapsedSeconds = chrono::duration_cast<chrono::seconds>(now - tokenIssueTime_).count();
        if (elapsedSeconds >= tokenExpiresIn_ - 60)
        { // Refresh token 60 seconds before expiry
            cout << "[Token] Token expired or nearing expiry. Refreshing..." << endl;
            return refreshToken();
        }
        return true;
    }
    string clientId_;
    string clientSecret_;
    string accessToken_;
    string refreshToken_;
    int tokenExpiresIn_;
    chrono::system_clock::time_point tokenIssueTime_;
};

/*********************************
 * Deribit WebSocket Subscriber
 *   - Connects to Deribit for real-time data
 *   - Subscribes to "book.INSTRUMENT_NAME.raw"
 *   - On updates, calls a callback
 *********************************/

class WebSocketClient
{
public:
    WebSocketClient(const string &uri, const string &client_id, const string &client_secret)
        : uri_(uri), clientId_(client_id), clientSecret_(client_secret), ioc_(), sslCtx_(BoostSSL::context::tlsv12_client), ws_(ioc_, sslCtx_)
    {
        sslCtx_.set_verify_mode(BoostSSL::verify_none);
    }

    void connect()
    {
        // cout << "Sdfdsf" << endl;
        auto startTime = chrono::high_resolution_clock::now(); // Start time
        try
        {
            tcp::resolver resolver(ioc_);
            auto const results = resolver.resolve(uri_, "443");
            auto ep = Net::connect(ws_.next_layer().next_layer(), results);

            ws_.next_layer().handshake(BoostSSL::stream_base::client);
            ws_.handshake(uri_, "/ws/api/v2");
            cout << "[WebSocket] \033[32mConnected to server.\033[0m\n";
            logMessage("[WebSocket] Connected to server.");

            authenticate();
        }
        catch (const exception &e)
        {
            cerr << "[WebSocket] \033[31mConnection failed: \033[0m" << e.what() << endl;
            logMessage("[WebSocket] Connection failed: " + string(e.what()));
        }
        auto endTime = chrono::high_resolution_clock::now(); // End time
        chrono::duration<double, milli> latency = endTime - startTime;
        LatencyLog("[Latency] WebSocket connection establishment latency: " + to_string(latency.count()) + " ms");
    }

    void send(const string &message)
    {
        auto startTime = chrono::high_resolution_clock::now();
        try
        {
            ws_.write(Net::buffer(message));
            logMessage("[WebSocket] Message sent: " + message);
        }
        catch (const exception &e)
        {
            cerr << "[WebSocket] \033[31mSend failed: \033[0m" << e.what() << endl;
            logMessage("[WebSocket] Send failed: " + string(e.what()));
        }
        auto endTime = chrono::high_resolution_clock::now();
        chrono::duration<double, milli> latency = endTime - startTime;
        LatencyLog("[Latency] WebSocket message send latency: " + to_string(latency.count()) + " ms");
    }

    string receive()
    {
        auto startTime = chrono::high_resolution_clock::now();
        try
        {
            Beast::flat_buffer buffer;
            ws_.read(buffer);
            string response = Beast::buffers_to_string(buffer.data());
            logMessage("[WebSocket] Received: " + response);
            cout << "[WebSocket] \033[34mReceived a response.\033[0m Check logs for details.\n";

            auto endTime = chrono::high_resolution_clock::now();
            chrono::duration<double, milli> latency = endTime - startTime;
            LatencyLog("[Latency] WebSocket message receive latency: " + to_string(latency.count()) + " ms");

            return response;
        }
        catch (const exception &e)
        {
            cerr << "[WebSocket] \033[31mReceive failed: \033[0m" << e.what() << endl;
            logMessage("[WebSocket] Receive failed: " + string(e.what()));
            return "";
        }
    }

    void close()
    {
        auto startTime = chrono::high_resolution_clock::now();
        try
        {
            ws_.close(WebSocket::close_code::normal);
            logMessage("[WebSocket] Connection closed.");
            cout << "[WebSocket] \033[32mConnection closed successfully.\033[0m\n";
        }
        catch (const exception &e)
        {
            cerr << "[WebSocket] \033[31mClose failed: \033[0m" << e.what() << endl;
            logMessage("[WebSocket] Close failed: " + string(e.what()));
        }
        auto endTime = chrono::high_resolution_clock::now();
        chrono::duration<double, milli> latency = endTime - startTime;
        LatencyLog("[Latency] WebSocket connection close latency: " + to_string(latency.count()) + " ms");
    }

private:
    void authenticate()
    {
        auto startTime = chrono::high_resolution_clock::now();
        json authMessage = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "public/auth"},
            {"params", {{"grant_type", "client_credentials"}, {"client_id", clientId_}, {"client_secret", clientSecret_}}}};

        cout << "[WebSocket] \033[33mSending authentication message.\033[0m\n";
        send(authMessage.dump());
        string response = receive();

        try
        {
            auto jsonResponse = json::parse(response);
            if (jsonResponse.contains("error"))
            {
                throw runtime_error("Authentication failed: " + jsonResponse["error"]["message"].get<string>());
            }

            accessToken_ = jsonResponse["result"]["access_token"].get<string>();
            cout << "[WebSocket] \033[32mAuthentication successful. Access token received.\033[0m\n";
            logMessage("[WebSocket] Authentication successful. Access token received.");
        }
        catch (const exception &e)
        {
            cerr << "[WebSocket] \033[31mAuthentication failed: \033[0m" << e.what() << endl;
            logMessage("[WebSocket] Authentication failed: " + string(e.what()));
            throw;
        }
        auto endTime = chrono::high_resolution_clock::now();
        chrono::duration<double, milli> latency = endTime - startTime;
        LatencyLog("[Latency] WebSocket authentication latency: " + to_string(latency.count()) + " ms");
    }

    string uri_;
    string clientId_;
    string clientSecret_;
    string accessToken_;

    Net::io_context ioc_;
    BoostSSL::context sslCtx_;
    WebSocket::stream<Beast::ssl_stream<tcp::socket>> ws_;
};

class DeribitMarketData
{
public:
    using OrderBookCallback = function<void(const string &)>;

    DeribitMarketData(const string &host,
                      const string &port,
                      const string &instrument,
                      const string &accessToken,
                      OrderBookCallback cb)
        : host_(host), port_(port), instrument_(instrument), accessToken_(accessToken), callback_(cb)
    {
        keepRunning_ = true;
        logMessage("[Info] DeribitMarketData instance created for instrument: " + instrument);
    }

    ~DeribitMarketData()
    {
        stop();
        logMessage("[Info] DeribitMarketData instance destroyed for instrument: " + instrument_);
    }

    void start()
    {
        logMessage("[Info] Starting data stream for instrument: " + instrument_);
        wsThread_ = thread([this]()
                           { this->run(); });
    }

    void stop()
    {
        keepRunning_ = false;
        if (wsThread_.joinable())
        {
            wsThread_.join();
        }
        logMessage("[Info] Data stream stopped for instrument: " + instrument_);
    }

private:
void run()
{
    try
    {
        auto loopStart = chrono::high_resolution_clock::now();

        Net::io_context ioc;
        BoostSSL::context ctx(BoostSSL::context::tlsv12_client);
        ctx.set_verify_mode(BoostSSL::verify_none);

        tcp::resolver resolver(ioc);
        Beast::ssl_stream<Beast::tcp_stream> stream(ioc, ctx);
        auto const results = resolver.resolve(host_, port_);
        Beast::get_lowest_layer(stream).connect(results);

        stream.handshake(BoostSSL::stream_base::client);
        logMessage("[Info] SSL handshake completed for instrument: " + instrument_);

        WebSocket::stream<Beast::ssl_stream<Beast::tcp_stream>> ws(move(stream));
        ws.handshake(host_, "/ws/api/v2");
        ws.binary(false); // Use text frames for efficiency
        logMessage("[Info] WebSocket handshake completed for instrument: " + instrument_);

        // Authenticate WebSocket connection
        string authMsg = R"({"jsonrpc":"2.0","id":1,"method":"public/auth","params":{"grant_type":"client_credentials","client_id":")" +
                         DERIBIT_CLIENT_ID + R"(","client_secret":")" + DERIBIT_CLIENT_SECRET + R"("}})";
        ws.write(Net::buffer(authMsg));
        logMessage("[Info] Authentication message sent for instrument: " + instrument_);

        Beast::flat_buffer authBuffer;
        ws.read(authBuffer);
        logMessage("[Info] Authentication response received for instrument: " + instrument_);

        // Subscribe to order book channel
        string subscribeMsg = R"({"jsonrpc":"2.0","id":42,"method":"public/subscribe","params":{"channels":["book.)" + instrument_ + R"(.raw"]}})";
        ws.write(Net::buffer(subscribeMsg));
        logMessage("[Info] Subscription message sent for instrument: " + instrument_);

        Beast::flat_buffer buffer;
        while (keepRunning_)
        {
            auto readStart = chrono::high_resolution_clock::now();

            Beast::error_code ec;
            ws.read(buffer, ec);

            if (ec)
            {
                if (ec == Beast::websocket::error::closed)
                {
                    logMessage("[Warning] WebSocket connection closed for instrument: " + instrument_);
                }
                else
                {
                    logMessage("[Error] WebSocket read error: " + ec.message());
                }
                break;
            }

            string msg = Beast::buffers_to_string(buffer.data());
            buffer.consume(buffer.size());

            auto readEnd = chrono::high_resolution_clock::now();
            chrono::duration<double, milli> readLatency = readEnd - readStart;
            LatencyLog("WebSocket read latency for instrument " + instrument_ + ": " + to_string(readLatency.count()) + " ms");

            if (callback_)
            {
                callback_(msg);
            }
        }

        Beast::error_code closeEc;
        ws.close(WebSocket::close_code::normal, closeEc);
        if (closeEc)
        {
            logMessage("[Error] WebSocket close error: " + closeEc.message());
        }

        auto loopEnd = chrono::high_resolution_clock::now();
        chrono::duration<double, milli> loopLatency = loopEnd - loopStart;
        LatencyLog("Total loop latency for instrument " + instrument_ + ": " + to_string(loopLatency.count()) + " ms");
    }
    catch (const exception &e)
    {
        logMessage("[Error] Exception in WebSocket thread for instrument " + instrument_ + ": " + e.what());
    }
}

    string host_;
    string port_;
    string instrument_;
    string accessToken_;
    OrderBookCallback callback_;
    thread wsThread_;
    atomic<bool> keepRunning_;
};

/*********************************
 * Local WebSocket server
 *   - Accepts client connections
 *   - Allows them to subscribe to multiple symbols
 *   - Streams updates we get from Deribit to each subscribed client
 *********************************/
string createGetInstrumentMessage(const string &instrument_name)
{
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "public/get_instrument"},
        {"params", {{"instrument_name", instrument_name}}}};
    return msg.dump();
}
class SimpleWebSocketServer
{
public:
    class Session : public enable_shared_from_this<Session>
    {
    public:
        Session(tcp::socket socket, SimpleWebSocketServer &server)
            : ws_(move(socket)), server_(server), writing_(false) {}

        void start()
        {
            auto self = shared_from_this();
            ws_.async_accept([this, self](boost::system::error_code ec)
                             {
            if (!ec) {
                 cout << "[Local WS] Client connected.\n";
                doRead();
            } else {
                 cerr << "[Local WS] Client connection failed: " << ec.message() << "\n";
            } });
        }

        void send(const string &message)
        {
            auto self = shared_from_this();
            Net::post(ws_.get_executor(), [this, self, message]()
                      {
            messageQueue_.push_back(message);
            if (!writing_) {
                doWrite();
            } });
        }

        void close()
        {
            ws_.async_close(boost::beast::websocket::close_code::normal, [](boost::system::error_code) {});
        }

        void addSubscription(const string &instrument)
        {
            subscriptions_.insert(instrument);
        }

        void removeSubscription(const string &instrument)
        {
            subscriptions_.erase(instrument);
        }

        const set<string> &getSubscriptions() const
        {
            return subscriptions_;
        }

    private:
        void doRead()
        {
            auto self = shared_from_this();
            ws_.async_read(buffer_, [this, self](boost::system::error_code ec, size_t bytes_transferred)
                           {
    if (!ec) {
        std::string message(Beast::buffers_to_string(buffer_.data()));
        buffer_.consume(bytes_transferred);
        logMessage("Messge Recived by the client "+message+"\n");
        if (message.rfind("SUBSCRIBE:", 0) == 0) {
            std::string instrument = message.substr(10);
            if (server_.isValidInstrument(instrument, DERIBIT_WS_HOST , DERIBIT_WS_PORT)) {
                server_.subscribeToInstrument(instrument, shared_from_this());
                addSubscription(instrument);
                send("Subscribed to " + instrument);
            } else {
                send("Invalid instrument: " + instrument);
            }
        } else {
            send("Unknown command.");
        }

        doRead();
    } else {
        std::cout << "[Local WS] Client disconnected.\n";
        server_.unsubscribeClient(shared_from_this());
    } });
        }
        void doWrite()
        {
            if (messageQueue_.empty())
            {
                writing_ = false;
                return;
            }

            writing_ = true;
            auto self = shared_from_this();
            ws_.async_write(Net::buffer(messageQueue_.front()), [this, self](boost::system::error_code ec, size_t)
                            {
            if (!ec) {
                messageQueue_.pop_front();
                doWrite();
            } else {
                 cerr << "[Local WS] Failed to send message: " << ec.message() << "\n";
                cout << "UNSUBSCRIBING ALL THE SYMBOLS"<<endl;
                writing_ = false;
            } });
        }

        WebSocket::stream<tcp::socket> ws_;
        Beast::flat_buffer buffer_;
        SimpleWebSocketServer &server_;
        bool writing_;
        deque<string> messageQueue_;
        set<string> subscriptions_; // Set of subscribed instruments for this client
    };

    SimpleWebSocketServer(unsigned short port)
        : ioc_(), acceptor_(ioc_, tcp::endpoint(tcp::v4(), port)) {}
    bool isValidInstrument(const string &instrument_name, const string &host, const string &port)
    {
        try
        {
            // Create an I/O context
            Net::io_context ioc;

            // Create an SSL context
            boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_client);
            ctx.set_verify_mode(boost::asio::ssl::verify_none);

            // Resolve the server host and port
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(host, port);

            // Set up a WebSocket stream with SSL
            WebSocket::stream<Beast::ssl_stream<tcp::socket>> ws(ioc, ctx);

            // Establish a connection
            boost::asio::connect(ws.next_layer().next_layer(), results);

            // Perform SSL handshake
            ws.next_layer().handshake(boost::asio::ssl::stream_base::client);

            // Perform WebSocket handshake
            ws.handshake(host, "/ws/api/v2");

            // Create the message for instrument validation
            json msg = {
                {"jsonrpc", "2.0"},
                {"id", 2},
                {"method", "public/get_instrument"},
                {"params", {{"instrument_name", instrument_name}}}};

            // Send the message
            ws.write(Net::buffer(msg.dump()));

            // Read the response
            Beast::flat_buffer buffer;
            ws.read(buffer);

            // Parse the JSON response
            string response = Beast::buffers_to_string(buffer.data());
            auto jsonResponse = nlohmann::json::parse(response);

            // Check for errors in the response
            if (jsonResponse.contains("error"))
            {
                cerr << "[Error] Instrument validation failed: " << jsonResponse["error"]["message"] << "\n";
                return false; // Instrument is invalid
            }

            // If no error, the instrument is valid
            return true;
        }
        catch (const exception &e)
        {
            cerr << "[Error] Validation failed: " << e.what() << "\n";
            return false;
        }
    }
    // Starts the server and begins accepting connections
    void start()
    {
        serverThread_ = thread([this]()
                               { run(); });
        cleanupThread_ = thread([this]()
                                {
        while (running_)
        {
            this_thread::sleep_for(chrono::seconds(10));
            cleanupSubscriptions();  // Clean up expired subscriptions periodically
        } });
        logMessage("Server started on port " + to_string(acceptor_.local_endpoint().port()));
        cout << "[Info] WebSocket server started. Listening on port "
             << acceptor_.local_endpoint().port() << endl;
    }

    // Stops the server
    void stop()
    {
        running_ = false;
        ioc_.stop(); // Stop the I/O context
        if (serverThread_.joinable())
        {
            serverThread_.join(); // Join the server thread
        }
        if (cleanupThread_.joinable())
        {
            cleanupThread_.join(); // Join the cleanup thread
        }
        logMessage("Server stopped.");
        cout << "[Info] WebSocket server stopped successfully." << endl;
    }

    // Returns a list of instruments that have at least one subscriber
    vector<string> getSubscribedInstruments()
    {
        lock_guard<mutex> lock(mutex_);
        vector<string> instruments;
        for (const auto &[instrument, sessions] : subscriptions_)
        {
            // Only include instruments that have at least one subscriber
            if (!sessions.empty())
            {
                instruments.push_back(instrument);
            }
        }
        return instruments;
    }

    bool isInstrumentSubscribed(const string &instrument)
    {
        lock_guard<mutex> lock(mutex_);
        auto it = subscriptions_.find(instrument);
        if (it == subscriptions_.end())
        {
            return false;
        }
        // Check if there is at least one non-expired session
        for (const auto &weakSession : it->second)
        {
            if (!weakSession.expired())
            {
                return true;
            }
        }
        return false;
    }

    void unsubscribeClient(shared_ptr<Session> session)
    {
        lock_guard<mutex> lock(mutex_);
        if (clientSubscriptions_.count(session) > 0)
        {
            for (const auto &instrument : clientSubscriptions_[session])
            {
                auto &subs = subscriptions_[instrument];
                subs.erase(remove_if(subs.begin(), subs.end(),
                                     [&session](const weak_ptr<Session> &weakSession)
                                     {
                                         return weakSession.lock() == session;
                                     }),
                           subs.end());
                // Remove key if no more subscribers
                if (subs.empty())
                {
                    subscriptions_.erase(instrument);
                }
            }
            clientSubscriptions_.erase(session);
            cout << "[Local WS] Unsubscribed all instruments for client.\n";
        }
    }

    void subscribeToInstrument(const string &instrument, shared_ptr<Session> session)
    {
        lock_guard<mutex> lock(mutex_);
        subscriptions_[instrument].emplace_back(session);
        clientSubscriptions_[session].insert(instrument);
        logMessage("Subscribed to instrument: " + instrument);
        cout << "[Info] Client subscribed to instrument: " << instrument << endl;
    }
    // sending data of a symbol to client whenever there is a update
    void onOrderBookUpdate(const string &instrument, const string &data)
    {
        auto start = chrono::high_resolution_clock::now();
        lock_guard<mutex> lock(mutex_);

        lastData_[instrument] = data;

        auto &subs = subscriptions_[instrument];
        for (auto it = subs.begin(); it != subs.end();)
        {
            auto session = it->lock();
            if (session)
            {
                try
                {
                    for (const auto &subscribedInstrument : session->getSubscriptions())
                    {
                        auto found = lastData_.find(subscribedInstrument);
                        if (found != lastData_.end())
                        {
                            session->send(found->second);
                        }
                    }
                    ++it;
                }
                catch (const exception &e)
                {
                    logMessage("Failed to send data to client: " + string(e.what()));
                    cerr << "[Error] Could not send data to client for instrument "
                         << instrument << ": " << e.what() << endl;
                    it = subs.erase(it);
                }
            }
            else
            {
                it = subs.erase(it);
            }
        }
        auto end = chrono::high_resolution_clock::now();
        chrono::duration<double, milli> latency = end - start;
        LatencyLog("Market data update latency: " + to_string(latency.count()) + " ms");
        cout << "[Latency] Market data update latency for " << instrument
             << ": " << latency.count() << " ms" << endl;
    }

    void cleanupSubscriptions()
    {
        lock_guard<mutex> lock(mutex_);
        for (auto &[instrument, sessions] : subscriptions_)
        {
            for (auto it = sessions.begin(); it != sessions.end();)
            {
                if (it->expired())
                {
                    it = sessions.erase(it); // Remove stale subscriptions
                }
                else
                {
                    ++it;
                }
            }
        }
    }
    void cleanupResources()
    {
        lock_guard<mutex> lock(mutex_);

        // Unsubscribe all clients and clear subscriptions
        for (auto &[instrument, sessions] : subscriptions_)
        {
            for (auto &weakSession : sessions)
            {
                if (auto session = weakSession.lock())
                {
                    session->close(); // Close the session
                }
            }
            sessions.clear();
        }
        subscriptions_.clear();
        clientSubscriptions_.clear();

        cout << "[Local WS] All sessions and subscriptions have been cleaned up.\n";
    }

private:
    void run()
    {
        doAccept();
        ioc_.run();
    }

    void doAccept()
    {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket)
                               {
                if (!ec) {
                    make_shared<Session>( move(socket), *this)->start();
                }
                doAccept(); });
    }
    thread cleanupThread_; // New thread for cleanup logic
    map<shared_ptr<Session>, set<string>> clientSubscriptions_;
    atomic<bool> running_{true};
    Net::io_context ioc_;
    map<string, string> lastData_;
    tcp::acceptor acceptor_;
    thread serverThread_;
    mutex mutex_;
    map<string, vector<weak_ptr<Session>>> subscriptions_;
};

// Global context for cleanup (using pointers to allow cleanup)
static SimpleWebSocketServer *globalServer = nullptr;
static map<string, unique_ptr<DeribitMarketData>> *globalActiveStreams = nullptr;

/**
 * Create a message to place a buy order on the exchange.
 *
 * This function generates a JSON-RPC formatted message to place a buy order with basic details like instrument,
 * amount, order type, and a label for tracking purposes.
 *
 * @param instrument - The name of the instrument to buy (e.g., "BTC-USD").
 * @param amount - The amount of the instrument to buy.
 * @param type - The type of the order, such as "limit" or "market".
 * @param label - A label for the order, used for tracking.
 * @param contracts - The number of contracts to buy.
 *
 * @return string - A JSON string representing the buy order message.
 */
string createPlaceOrderMessage(const string &instrument, double amount, const string &type, const string &label, double contracts)
{
    // Construct parameters for the JSON message
    json params = {
        {"instrument_name", instrument},
        {"contracts", contracts},
        {"type", type},
        {"label", label}};

    // Create the full JSON-RPC message
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", 5275},              // Unique ID for the request
        {"method", "private/buy"}, // API method for placing a buy order
        {"params", params}};

    // Convert the message to a JSON string and return it
    return msg.dump();
}

/**
 * Create a message to place a limit buy order on the exchange.
 *
 * This function generates a JSON-RPC formatted message for placing a limit buy order with specific price and amount.
 *
 * @param instrument - The name of the instrument to buy (e.g., "BTC-USD").
 * @param amount - The amount of the instrument to buy.
 * @param type - The type of the order ("limit" is expected).
 * @param price - The price at which to buy the instrument.
 * @param label - A label for the order, used for tracking.
 *
 * @return string - A JSON string representing the limit buy order message.
 */
string limcreatePlaceOrderMessage(const string &instrument, double amount, const string &type, const double price, const string &label)
{
    // Construct parameters for the limit order
    json params = {
        {"instrument_name", instrument},
        {"type", type},
        {"amount", amount},
        {"price", price}};

    // Create the full JSON-RPC message for the limit order
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", 100},               // Unique ID for the request
        {"method", "private/buy"}, // API method for placing a buy order
        {"params", params}};

    // Convert the message to a JSON string and return it
    return msg.dump();
}

/**
 * Create a message to cancel an existing order.
 *
 * This function generates a JSON-RPC formatted message to cancel an order using its unique order ID.
 *
 * @param order_id - The unique ID of the order to cancel.
 *
 * @return string - A JSON string representing the cancel order message.
 */
string createCancelOrderMessage(const string &order_id)
{
    // Construct the cancel order message with the order ID
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", 5276},                 // Unique ID for the request
        {"method", "private/cancel"}, // API method for cancelling an order
        {"params", {{"order_id", order_id}}}};

    // Convert the message to a JSON string and return it
    return msg.dump();
}

/**
 * Create a message to modify an existing order.
 *
 * This function generates a JSON-RPC formatted message to edit an existing order, changing its price and/or amount.
 *
 * @param order_id - The unique ID of the order to modify.
 * @param new_price - The new price to set for the order.
 * @param new_amount - The new amount to set for the order.
 *
 * @return string - A JSON string representing the modify order message.
 */
string createModifyOrderMessage(const string &order_id, double new_price, double new_amount)
{
    // Construct the modify order message with the new price and amount
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", 5277},               // Unique ID for the request
        {"method", "private/edit"}, // API method for editing an order
        {"params", {{"order_id", order_id}, {"amount", new_amount}, {"price", new_price}}}};

    // Convert the message to a JSON string and return it
    return msg.dump();
}

/**
 * Fetch the contract size for a specific instrument from the exchange.
 *
 * This function sends a message to the WebSocket client to request the contract size for a specific instrument.
 *
 * @param client - The WebSocket client used to communicate with the exchange.
 * @param instrumentName - The name of the instrument (e.g., "BTC-USD").
 *
 * @return optional<int> - The contract size, or nullopt if it couldn't be fetched.
 */
optional<int> fetchContractSize(WebSocketClient &client, const string &instrumentName)
{
    // Send a message to get the instrument details
    string getInstrumentMessage = createGetInstrumentMessage(instrumentName);
    cout << "[Info] Sending Get Instrument Message: " << getInstrumentMessage << endl;
    client.send(getInstrumentMessage);

    // Receive the response
    string response = client.receive();

    try
    {
        // Parse the response and check for contract size
        auto jsonResponse = json::parse(response);
        if (jsonResponse.contains("result") && jsonResponse["result"].contains("contract_size"))
        {
            return jsonResponse["result"]["contract_size"].get<int>();
        }
    }
    catch (const exception &e)
    {
        cerr << "[Error] Failed to parse Get Instrument response: " << e.what() << endl;
    }

    // Return nullopt if contract size isn't found
    return nullopt;
}

/**
 * Create a message to get the state of an order.
 *
 * This function generates a JSON-RPC formatted message to query the current state of an order using its order ID.
 *
 * @param order_id - The unique ID of the order to query.
 *
 * @return string - A JSON string representing the order state message.
 */
string createOrderStateMessage(const string &order_id)
{
    // Construct the message to get the order state using the order ID
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", 5280},                          // Unique ID for the request
        {"method", "private/get_order_state"}, // API method for fetching order state
        {"params", {{"order_id", order_id}}}};

    // Convert the message to a JSON string and return it
    return msg.dump();
}

/**
 * Check if the response from the API contains an error.
 *
 * This function checks whether the given response contains an error field, indicating a failure.
 *
 * @param response - The response string from the API.
 *
 * @return bool - True if the response contains an error, false otherwise.
 */
bool responseContainsError(const string &response)
{
    // Parse the response and check for an error field
    json jsonResponse = json::parse(response);
    return jsonResponse.contains("error");
}

/**
 * Create a message to get detailed information about a specific order.
 *
 * This function generates a JSON-RPC formatted message to query detailed information about an order using its order ID.
 *
 * @param orderId - The unique ID of the order to query.
 *
 * @return string - A JSON string representing the order details message.
 */
string createOrderDetailsMessage(const string &orderId)
{
    return "{\"jsonrpc\":\"2.0\",\"method\":\"private/get_order_state\",\"params\":{\"order_id\":\"" + orderId + "\"},\"id\":1}";
}

/**
 * Signal handler for cleanup during shutdown (e.g., SIGINT).
 *
 * This function handles the SIGINT signal (Ctrl+C) to gracefully stop the server, clean up resources,
 * and stop any active streams.
 *
 * @param signal - The signal that triggered the handler (e.g., SIGINT).
 */
void signalHandler(int signal)
{
    if (signal == SIGINT)
    {
        cout << "\n[Info] SIGINT received. Initiating cleanup...\n";

        // Stop the running flag to terminate the execution
        running = false;

        if (globalActiveStreams)
        {
            // Stop all active market data streams
            for (auto &[instrument, stream] : *globalActiveStreams)
            {
                stream->stop();
            }
            globalActiveStreams->clear(); // Clear the active streams map
        }

        if (globalServer)
        {
            // Clean up the WebSocket server resources
            globalServer->cleanupResources();
            globalServer->stop();
        }

        cout << "[Info] Cleanup completed. Exiting gracefully.\n";
    }
}
/*********************************
 * MAIN
 *********************************/
int main()
{
    clearLogFile();
    // Instantiate API and required data structures
    DeribitApi api(DERIBIT_CLIENT_ID, DERIBIT_CLIENT_SECRET);
    WebSocketClient client("test.deribit.com", DERIBIT_CLIENT_ID, DERIBIT_CLIENT_SECRET);
    client.connect();
    map<string, string> orderMap; // Tracks bought instruments with order IDs
    if (!api.authenticate())
    {

        logMessage("[Error] Authentication failed. Exiting...\n");
        return 1;
    }

    while (true)
    {
        // Display menu options
        cout << "\n--- Interactive Trading Menu ---\n";
        cout << "1. Place Order (Buy)\n";
        cout << "2. Cancel Order\n";
        cout << "3. Modify Order\n";
        cout << "4. Get Order Book\n";
        cout << "5. Get Positions\n";
        cout << "6. Start WebSocket Server\n";
        std ::cout << "7. View your orders \n";
        cout << "8. Exit\n";
        cout << "Enter your choice: ";

        int choice;
        cin >> choice;
        cin.ignore(); // Clear input buffer
        if (cin.fail()) {
            cin.clear(); // Clear the error flag
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Discard invalid input
            cout << "Invalid input. Please enter a valid integer.\n";
            continue; // Prompt user again
        }
        if (choice == 8)
        {
            cout << "Exiting application. Goodbye!\n";
            break;
        }

        switch (choice)
        {
        case 1:
        {
            auto loopStart = chrono::high_resolution_clock::now(); // Start time
                                                                   // Define automatic order parameters
            vector<tuple<string, string, double, double>> orders = {
                {"BTC-PERPETUAL", "market", 1.0, 0.0}, // Market order example
                {"BTC-PERPETUAL", "limit", 10, 10}     // Limit order example
            };

            for (const auto &[instrumentName, type, amount, price] : orders)
            {
                auto start = chrono::high_resolution_clock::now(); // Start timing

                string placeOrderMessage;
                if (type == "market")
                {
                    placeOrderMessage = createPlaceOrderMessage(instrumentName, amount, type, "auto_market_order", amount);
                }
                else if (type == "limit")
                {
                    placeOrderMessage = limcreatePlaceOrderMessage(instrumentName, amount, type, price, "auto_limit_order");
                }
                //  cout << "[Info] Sending Place Order Message: " << placeOrderMessage <<  endl;
                cout << "Sending Message to place an order !" << endl;
                logMessage("[Info] Sending Place Order Message: " + placeOrderMessage + "\n");
                client.send(placeOrderMessage);
                string response = client.receive();
                auto end = chrono::high_resolution_clock::now(); // End timing
                                                                 // Check if the response contains an error message
                string errorMsg = extract_json_field(response, "message");
                if (!errorMsg.empty())
                {
                    cout << "[Error] Failed to Place an Order , check log for more information!" << endl;
                    logMessage("[Error] Failed to place order. Reason: " + errorMsg + "\n");
                    continue;
                }
                string orderId = extract_json_field(response, "order_id");
                int i = 1;
                if (!orderId.empty())
                {
                    orderMap[orderId] = instrumentName;
                    cout << "Order placed successfully. Order ID: " << orderId << "\n";
                    logMessage("Order placed successfully. Order ID: " + orderId + "\n");
                    chrono::duration<double, milli> latency = end - start;
                    LatencyLog("Order Placement " + to_string(i) + " Latency: " + to_string(latency.count()) + " ms\n");
                    i++;
                }
                else
                {
                    cout << "[Error] Failed to extract Order ID from response.\n";
                    logMessage("[Error] Failed to extract Order ID from response while placing Order\n");
                }
            }
            auto loopEnd = chrono::high_resolution_clock::now(); // End time
            chrono::duration<double, milli> endToEndLatency = loopEnd - loopStart;
            cout << "[Latency] End-to-end trading loop latency: " << endToEndLatency.count() << " ms\n";
            LatencyLog("[Latency] End-to-end trading loop latency: " + to_string(endToEndLatency.count()) + " ms");
            break;
        }

        case 2: // Cancel Order
        {
            auto loopStart = chrono::high_resolution_clock::now(); // Start time
            if (orderMap.empty())
            {
                cout << "No active orders to cancel.\n";
                break;
            }

            cout << "Active Orders:\n";
            for (const auto &entry : orderMap)
            {
                cout << "Order ID: " << entry.first << " , Instrument: " << entry.second << "\n";
            }

            cout << "Enter Order ID to cancel: ";
            string orderId;
            cin >> orderId;
            logMessage("[Info] Received Order ID to cancel from user: " + orderId);

            if (orderMap.find(orderId) != orderMap.end())
            {
                string cancelOrderMessage = createCancelOrderMessage(orderId);
                cout << "[Info] Sending message to cancel the order!\n";
                logMessage("[Info] Sending Cancel Order Message: " + cancelOrderMessage);

                auto sentTime = chrono::high_resolution_clock::now(); // Time message sent
                client.send(cancelOrderMessage);

                // Measure time when the response is received
                auto responseStart = chrono::high_resolution_clock::now();
                client.receive();
                auto responseEnd = chrono::high_resolution_clock::now(); // Response received

                // Calculate propagation delay (approximation)
                chrono::duration<double, nano> propagationDelay = responseEnd - sentTime;
                LatencyLog("[Propagation Delay] Time for a message to propagate from server to client: " +
                           to_string(propagationDelay.count()) + " ns");

                orderMap.erase(orderId);
                cout << "Order cancelled successfully.\n";
                logMessage("[Info] Order cancelled successfully for Order ID: " + orderId);
            }
            else
            {
                cout << "Invalid Order ID.\n";
                logMessage("[Warning] Invalid Order ID entered by user: " + orderId);
            }

            auto loopEnd = chrono::high_resolution_clock::now(); // End time
            chrono::duration<double, milli> endToEndLatency = loopEnd - loopStart;
            cout << "[Latency] End-to-end trading loop latency: " << endToEndLatency.count() << " ms\n";
            LatencyLog("[Latency] End-to-end trading loop latency for canceling an order: " +
                       to_string(endToEndLatency.count()) + " ms");

            break;
        }

        case 3:
        {
            auto loopStart = chrono::high_resolution_clock::now(); // Start time
            if (orderMap.empty())
            {
                cout << "No active orders to modify.\n";
                break;
            }

            cout << "Active Orders:\n";
            // Display Order ID, Instrument, and Market Type
            for (const auto &entry : orderMap)
            {
                const string &orderId = entry.first;
                const string &instrumentName = entry.second;

                // Fetch additional details for the order (e.g., market type)
                string orderDetailsMessage = createOrderDetailsMessage(orderId);
                client.send(orderDetailsMessage);
                string orderDetailsResponse = client.receive();

                string marketType = extract_json_field(orderDetailsResponse, "order_type");
                if (marketType.empty())
                {
                    marketType = "unknown";
                }

                cout << "Order ID: " << orderId
                     << " , Instrument: " << instrumentName
                     << " , Market Type: " << marketType << "\n";
            }

            cout << "Enter Order ID to modify: ";
            string orderId;
            cin >> orderId;

            if (orderMap.find(orderId) != orderMap.end())
            {
                // Check the order state before proceeding
                string checkStateMessage = createOrderStateMessage(orderId);
                cout << "[Info] Sending Order State Message!" << endl;
                logMessage("[Info] Sending Order State Message: " + checkStateMessage + "\n");
                client.send(checkStateMessage);
                string stateResponse = client.receive();

                // Parse the response to check if the order is open
                string orderState = extract_json_field(stateResponse, "order_state");
                if (orderState != "open")
                {
                    cout << "[Error] Cannot modify order. Current state: " << orderState << "\n";
                    logMessage("[Error] Cannot modify order. Current state: " + orderState + "\n");
                    break;
                }

                double newPrice, newAmount;
                cout << "Enter new price: ";
                cin >> newPrice;
                cout << "Enter new amount: ";
                cin >> newAmount;

                string modifyOrderMessage = createModifyOrderMessage(orderId, newPrice, newAmount);
                cout << "[Info] Sending Modify Order Message!" << modifyOrderMessage << endl;
                logMessage("[Info] Sending Modify Order Message!" + modifyOrderMessage + "\n");
                client.send(modifyOrderMessage);
                string modifyResponse = client.receive();
                // Check response for success or errors
                if (responseContainsError(modifyResponse))
                {
                    string errorReason = extract_json_field(modifyResponse, "message");
                    cout << "[Error] Failed to modify order.Check Log for more information" << endl;
                    logMessage("[Error] Failed to modify order.Check Log for more information" + errorReason + "\n");
                }
                else
                {
                    cout << "Order modified successfully.\n";
                }
            }
            else
            {
                cout << "Invalid Order ID.\n";
            }
            auto loopEnd = chrono::high_resolution_clock::now(); // End time
            chrono::duration<double, milli> endToEndLatency = loopEnd - loopStart;
            cout << "[Latency] End-to-end trading loop latency: " << endToEndLatency.count() << " ms\n";
            LatencyLog("[Latency] End-to-end trading loop latency for modifying an order : " + to_string(endToEndLatency.count()) + " ms");
            break;
        }

        case 4: // Get Order Book
        {
            auto loopStart = chrono::high_resolution_clock::now(); // Start time

            // Automated for BTC-PERPETUAL
            string instrumentName = "BTC-PERPETUAL";
            cout << "\n========================\n"
                 << "   Fetch Order Book Automatically   \n"
                 << "========================\n";

            // Commented out manual input
            // cout << "Enter instrument name (e.g., BTC-PERPETUAL): ";
            // cin >> instrumentName;

            // Fetch order book
            string response = api.getOrderBook(instrumentName);
            if (!response.empty())
            {
                cout << "\n------------------------\n"
                     << "Order Book for: " << instrumentName << "\n"
                     << "------------------------\n"
                     << response << "\n";
            }
            else
            {
                logMessage("[Error] Failed to fetch or parse the JSON response for the order book in case 4.\n");
                cout << "\n[Error] Failed to fetch order book.\n";
            }

            // Calculate latency
            auto loopEnd = chrono::high_resolution_clock::now(); // End time
            chrono::duration<double, milli> endToEndLatency = loopEnd - loopStart;

            // Log latency with better formatting
            cout << "\n========================\n"
                 << "  Latency Report  \n"
                 << "========================\n"
                 << "End-to-End Trading Loop Latency: " << endToEndLatency.count() << " ms\n"
                 << "========================\n";

            LatencyLog("[Latency] End-to-end trading loop latency to fetch an order book: " + to_string(endToEndLatency.count()) + " ms\n");

            break;
        }
        case 5: // Get Positions
        {
            auto loopStart = chrono::high_resolution_clock::now(); // Start time

            // Automated for BTC-PERPETUAL
            string instrumentName = "BTC-PERPETUAL";
            cout << "\n========================\n"
                 << "   Fetch Positions Automatically   \n"
                 << "========================\n";

            // Commented out manual input
            // cout << "Enter instrument name (optional, press Enter to skip): ";
            // getline(cin, instrumentName);

            string response = api.getPositions(instrumentName);
            logMessage("[Info]: Get Positions Response for instrument name: " + instrumentName + " is " + response + "\n");
            if (!response.empty())
            {
                cout << "Positions:\n"
                     << response << "\n";
            }
            else
            {
                logMessage("[Error] There was an error in parsing the JSON file for Positions in case 5\n");
                cout << "Failed to fetch positions.\n";
            }

            auto loopEnd = chrono::high_resolution_clock::now(); // End time
            chrono::duration<double, milli> endToEndLatency = loopEnd - loopStart;
            cout << "[Latency] End-to-end trading loop latency: " << endToEndLatency.count() << " ms\n";
            LatencyLog("[Latency] End-to-end trading loop latency to get positions " + to_string(endToEndLatency.count()) + " ms");
            break;
        }
        case 6: // Start WebSocket Server
        {
            auto loopStart = chrono::high_resolution_clock::now(); // Start time
            logMessage("[Info] Starting WebSocket Server...");

            try
            {
                // Fetch the access token
                string accessToken = api.getAccessToken();
                if (accessToken.empty())
                {
                    throw runtime_error("Access token is empty. Ensure the API is properly authenticated.");
                }
                logMessage("[Auth] Successfully fetched access token.");

                // Initialize and start the WebSocket server
                SimpleWebSocketServer server(9002);
                server.start();
                logMessage("[Local WS] WebSocket server started on port 9002.");
                cout << "Clients can connect (e.g., 'wscat -c ws://localhost:9002')\n";
                cout << "Then send: SUBSCRIBE:<INSTRUMENT>\n";

                // Map of active Deribit market data streams for instruments
                map<string, unique_ptr<DeribitMarketData>> activeStreams;
                globalServer = &server;
                globalActiveStreams = &activeStreams;

                // Register signal handler
                signal(SIGINT, signalHandler);
                running = true;
                logMessage("[Info] Signal handler registered. Waiting for Ctrl+C to stop the server...");

                // Server main loop
                while (running)
                {
                    // Start new streams for subscribed instruments
                    for (const auto &instrument : server.getSubscribedInstruments())
                    {
                        if (activeStreams.find(instrument) == activeStreams.end())
                        {
                            logMessage("[Info] Starting data stream for instrument: " + instrument);
                            // Initialize the Deribit market data handler
                            activeStreams[instrument] = make_unique<DeribitMarketData>(
                                DERIBIT_WS_HOST,
                                DERIBIT_WS_PORT,
                                instrument,
                                accessToken,
                                [&server, instrument](const string &msg)
                                {
                                    try
                                    {
                                        server.onOrderBookUpdate(instrument, msg);
                                        logMessage("[Info] Order book update broadcasted for instrument: " + instrument);
                                    }
                                    catch (const exception &e)
                                    {
                                        cerr << "[Error] Failed to broadcast order book update for " << instrument << ": " << e.what() << "\n";
                                        logMessage("[Error] Exception during order book update: " + string(e.what()));
                                    }
                                });

                            // Start market data streaming
                            activeStreams[instrument]->start();
                            logMessage("[Info] Market data streaming started for instrument: " + instrument);
                        }
                    }

                    // Remove streams for unsubscribed instruments
                    for (auto it = activeStreams.begin(); it != activeStreams.end();)
                    {
                        if (!server.isInstrumentSubscribed(it->first))
                        {
                            logMessage("[Info] Stopping data stream for unsubscribed instrument: " + it->first);
                            it->second->stop();
                            it = activeStreams.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    this_thread::sleep_for(chrono::seconds(1));
                }

                // Clean up resources
                cout << "[Info] Stopping market data and WebSocket server...\n";
                logMessage("[Info] Stopping all market data streams...");
                for (auto &[instrument, stream] : activeStreams)
                {
                    stream->stop();
                    logMessage("[Info] Stopped data stream for instrument: " + instrument);
                }
                activeStreams.clear();

                // Perform cleanup for stale sessions
                server.cleanupResources(); // Explicit cleanup only at server shutdown
                server.stop();
                logMessage("[Info] WebSocket server stopped successfully.");
            }
            catch (const exception &e)
            {
                cerr << "[Error] Exception occurred: " << e.what() << "\n";
                logMessage("[Error] Exception occurred: " + string(e.what()));
            }
            catch (...)
            {
                cerr << "[Error] Unknown exception occurred in Case 6.\n";
                logMessage("[Error] Unknown exception occurred in Case 6.");
            }

            auto loopEnd = chrono::high_resolution_clock::now(); // End time
            chrono::duration<double, milli> endToEndLatency = loopEnd - loopStart;

            // Log latency
            cout << "\n========================\n";
            cout << "  WebSocket Server Info  \n";
            cout << "========================\n";
            cout << "[Latency] End-to-end trading loop latency: " << endToEndLatency.count() << " ms\n";
            logMessage("[Latency] End-to-end trading loop latency for WebSocket connection with client: " + to_string(endToEndLatency.count()) + " ms");
            LatencyLog("[Latency] End-to-end trading loop latency for WebSocket connection with client: " + to_string(endToEndLatency.count()) + " ms");
            cout << "========================\n";

            break;
        }

        case 7:
        {
            if (orderMap.empty())
            {
                cout << "No orders to view.\n";
                break;
            }

            cout << "Active Orders:\n";
            // Display Order ID and Instrument Name
            for (const auto &entry : orderMap)
            {
                cout << "Order ID: " << entry.first
                     << " , Instrument: " << entry.second << "\n";
            }

            cout << "Enter Order ID to view details: ";
            string orderId;
            cin >> orderId;

            if (orderMap.find(orderId) != orderMap.end())
            {
                // Fetch and display order details
                string orderDetailsMessage = createOrderDetailsMessage(orderId);
                cout << "[Info] Sending Order Details Message: " << orderDetailsMessage << endl;
                client.send(orderDetailsMessage);
                string orderDetailsResponse = client.receive();

                // Parse and display relevant fields from the response
                string instrumentName = extract_json_field(orderDetailsResponse, "instrument_name");
                string orderType = extract_json_field(orderDetailsResponse, "type");
                string orderState = extract_json_field(orderDetailsResponse, "order_state");
                double price = stod(extract_json_field(orderDetailsResponse, "price"));
                double amount = stod(extract_json_field(orderDetailsResponse, "amount"));
                double filledAmount = stod(extract_json_field(orderDetailsResponse, "filled_amount"));

                cout << "Order Details:\n";
                cout << "Instrument: " << instrumentName << "\n";
                cout << "Order Type: " << orderType << "\n";
                cout << "Order State: " << orderState << "\n";
                cout << "Price: " << price << "\n";
                cout << "Amount: " << amount << "\n";
                cout << "Filled Amount: " << filledAmount << "\n";
            }
            else
            {
                cout << "Invalid Order ID.\n";
            }

            break;
        }

        default:
            cout << "Invalid choice. Please try again.\n";
        }
    }

    return 0;
}