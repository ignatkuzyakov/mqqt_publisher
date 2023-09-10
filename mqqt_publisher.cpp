#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include <mosquitto.h>

#include <unordered_map>
#include <exception>
#include <iostream>
#include <iterator>
#include <vector>

using namespace boost::asio;
namespace http = boost::beast::http;

class Client
{
private:
    boost::system::error_code ec;

    io_service svc;

    ssl::context ctx;
    ssl::stream<boost::asio::ip::tcp::socket> socket;
    ip::tcp::resolver resolver;

    std::string host = "api.data.gov.sg";
    std::string page = "/v1/environment/air-temperature";
    std::string port = "443";


private:
    http::request<boost::beast::http::string_body> req;

public:
    Client() : ctx(ssl::context::method::sslv23_client), socket(svc, ctx), resolver(svc), req(http::verb::get, page, 11)
    {
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    }

    void connectToApi()
    {
        auto it = resolver.resolve({host, port});
        connect(socket.lowest_layer(), it);
        socket.handshake(ssl::stream_base::handshake_type::client);
    }

    std::string getResponse()
    {
        http::write(socket, req);

        boost::beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(socket, buffer, res);

        return buffers_to_string(res.body().data());
    }
    ~Client()
    {
        socket.shutdown(ec);
    }
};

class MQTTError : public std::exception
{
public:
    MQTTError(const std::string &message) : message(message) {}

    const char *what() const noexcept override { return message.c_str(); }

private:
    std::string message;
};

class MQTTWrapper
{
private:
    mosquitto *mos;

public:
    MQTTWrapper(const char *host)
    {
        mosquitto_lib_init();
        mos = mosquitto_new(host, true, NULL);
        
        if (!mos)
        {
            mosquitto_lib_cleanup();
            throw MQTTError("Exception: mosquitto_new");
        }
    }
    void tls_set(const char *cafile = NULL,
                 const char *capath = NULL, const char *certfile = NULL,
                 const char *keyfile = NULL, int (*pw_callback)(char *buf, int size, int rwflag, void *userdata) = NULL)
    {
        if (mosquitto_tls_set(mos, cafile, capath, certfile, keyfile, pw_callback) != MOSQ_ERR_SUCCESS)
        {
            mosquitto_destroy(mos);
            mosquitto_lib_cleanup();

            throw MQTTError("Exception: mosquitto_tls_set");
        }
    }

    void set_user(const char *login, const char *password)
    {
        if (mosquitto_username_pw_set(mos, login, password) != MOSQ_ERR_SUCCESS)
        {
            mosquitto_destroy(mos);
            mosquitto_lib_cleanup();

            throw MQTTError("Exception: mosquitto_username_pw_set");
        }
    }
    void connect(const char *host, int port, int keepalive = 60)
    {
        if (mosquitto_connect(mos, host, port, keepalive) != MOSQ_ERR_SUCCESS)
        {
            mosquitto_destroy(mos);
            mosquitto_lib_cleanup();

            throw MQTTError("Exception: mosquitto_connect");
        }
    }

    int loop(int timeout, int max_packets)
    {
        return (mosquitto_loop(mos, -1, 1) == MOSQ_ERR_SUCCESS);
    }

    void publish(int *mid, const char *topic, int payloadlen, const void *payload, int qos = 0, bool retain = false)
    {
        if (mosquitto_publish(mos, mid, topic, payloadlen, payload, qos, retain) != MOSQ_ERR_SUCCESS)
        {
            mosquitto_disconnect(mos);
            mosquitto_destroy(mos);
            mosquitto_lib_cleanup();

            throw MQTTError("Exception: mosquitto_publish");
        }
    }

    ~MQTTWrapper()
    {
        mosquitto_disconnect(mos);
        mosquitto_destroy(mos);
        mosquitto_lib_cleanup();
    }
};

void APIparse(std::string json, std::unordered_map<std::string, std::string> &data)
{
    std::stringstream jsonEncoded(json);
    boost::property_tree::ptree root;
    boost::property_tree::read_json(jsonEncoded, root);

    data["api_status"] = root.get<std::string>("api_info.status");

    BOOST_FOREACH (boost::property_tree::ptree::value_type &v, root.get_child("items..readings"))
    {
        if (v.second.get<std::string>("station_id") == "S50")
            data["S50"] = v.second.get<std::string>("value");

        else if (v.second.get<std::string>("station_id") == "S60")
            data["S60"] = v.second.get<std::string>("value");

        else if (v.second.get<std::string>("station_id") == "S107")
            data["S107"] = v.second.get<std::string>("value");
    }
}

int main()
{

    std::string topicIDs = "api/temperature/";
    std::string topicStatus = "api/status";

    const char *host = "test.mosquitto.org";
    const char *crt = "./certs/mosquitto.org.crt";

    const char *password = "writeonly";
    const char *login = "wo";

    int port = 8885;
    int keepalive = 60;

    std::unordered_map<std::string, std::string> data;

    Client client;
    MQTTWrapper mqtt(host);

    client.connectToApi();

    mqtt.tls_set(crt);
    mqtt.set_user(login, password);
    mqtt.connect(host, port);

    while (mqtt.loop(-1, 1))
    {
        APIparse(client.getResponse(), data);

        mqtt.publish(NULL, (topicIDs + "S50").c_str(), strlen(data["S50"].c_str()), data["S50"].c_str());
        mqtt.publish(NULL, (topicIDs + "S60").c_str(), strlen(data["S60"].c_str()), data["S60"].c_str());
        mqtt.publish(NULL, (topicIDs + "S107").c_str(), strlen(data["S107"].c_str()), data["S107"].c_str());
        mqtt.publish(NULL, topicStatus.c_str(), strlen(data["api_status"].c_str()), data["api_status"].c_str());
    }

    return 0;
}
