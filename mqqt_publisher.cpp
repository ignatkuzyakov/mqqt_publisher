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

private:
    std::string host;
    std::string page;
    std::string port;

private:
    http::request<boost::beast::http::string_body> req;

public:
    Client(const std::string &host, const std::string &page, const std::string &port) : host(host), page(page), port(port),
                                                                                        ctx(ssl::context::method::sslv23_client), socket(svc, ctx), resolver(svc), req(http::verb::get, page, 11)
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
    MQTTWrapper(const std::string &host)
    {
        mosquitto_lib_init();
        mos = mosquitto_new(host.c_str(), true, NULL);

        if (!mos) throw MQTTError("Exception: mosquitto_new");
    }
    void tls_set(const std::string &cafile = "",
                 const std::string &capath = "", const std::string &certfile = "",
                 const std::string &keyfile = "", int (*pw_callback)(char *buf, int size, int rwflag, void *userdata) = NULL)
    {
        if (mosquitto_tls_set(mos,
                              (cafile.size() == 0) ? NULL : cafile.c_str(),
                              (capath.size() == 0) ? NULL : capath.c_str(),
                              (certfile.size() == 0) ? NULL : certfile.c_str(),
                              (keyfile.size() == 0) ? NULL : keyfile.c_str(), pw_callback) != MOSQ_ERR_SUCCESS)
            throw MQTTError("Exception: mosquitto_tls_set");
    }

    void set_user(const std::string &login, const std::string &password)
    {
        if (mosquitto_username_pw_set(mos, login.c_str(), password.c_str()) != MOSQ_ERR_SUCCESS)
            throw MQTTError("Exception: mosquitto_username_pw_set");
    }

    void connect(const std::string &host, int port, int keepalive = 60)
    {
        if (mosquitto_connect(mos, host.c_str(), port, keepalive) != MOSQ_ERR_SUCCESS)
            throw MQTTError("Exception: mosquitto_connect");
    }

    int loop(int timeout, int max_packets) { return (mosquitto_loop(mos, -1, 1) == MOSQ_ERR_SUCCESS); }

    void publish(const std::string &topic, const std::string &payload, int *mid = NULL, int qos = 0, bool retain = false)
    {
        if (mosquitto_publish(mos, mid, topic.c_str(), payload.size(), (const void *)payload.c_str(), qos, retain) != MOSQ_ERR_SUCCESS)
            throw MQTTError("Exception: mosquitto_publish");
    }

    ~MQTTWrapper()
    {
        mosquitto_disconnect(mos);
        mosquitto_destroy(mos);
        mosquitto_lib_cleanup();
    }
};

class Parser
{
private:
    boost::property_tree::ptree root;
    std::vector<std::string> Ids{"S50", "S60", "S107"};

public:
    void parse(std::string json, std::unordered_map<std::string, std::string> &data)
    {
        std::stringstream jsonEncoded(json);
        boost::property_tree::read_json(jsonEncoded, root);

        data["api/status"] = root.get<std::string>("api_info.status");

        BOOST_FOREACH (boost::property_tree::ptree::value_type &v, root.get_child("items..readings"))
        {
            for (const auto &id : Ids)
                if (v.second.get<std::string>("station_id") == id)
                    data["api/temperature/" + id] = v.second.get<std::string>("value");
        }

        root.clear();
    }
};

int main()
{
    std::string APIhost = "api.data.gov.sg";
    std::string APIpage = "/v1/environment/air-temperature";
    std::string APIport = "443";

    std::string host = "test.mosquitto.org";
    std::string crt = "./certs/mosquitto.org.crt";

    std::string password = "writeonly";
    std::string login = "wo";

    int port = 8885;
    int keepalive = 60;

    std::unordered_map<std::string, std::string> data;

    Client client(APIhost, APIpage, APIport);
    Parser parser;

    client.connectToApi();

    try
    {
        MQTTWrapper mqtt(host);
        mqtt.tls_set(crt);
        mqtt.set_user(login, password);
        mqtt.connect(host, port);

        while (mqtt.loop(-1, 1))
        {
            parser.parse(client.getResponse(), data);

            for (const auto &[topic, value] : data)
                mqtt.publish(topic, value);
        }
    }

    catch (MQTTError &err)
    {
        std::cout << err.what() << std::endl;
    }

    return 0;
}