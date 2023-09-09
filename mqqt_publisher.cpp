#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include <unordered_map>
#include <mosquitto.h>

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
    ssl::stream<ip::tcp::socket> ssock;
    ip::tcp::resolver resolver;

private:
    http::request<http::string_body> req;

public:
    Client() : ctx(ssl::context::method::sslv23_client), ssock(svc, ctx), resolver(svc), req(http::verb::get, "/v1/environment/air-temperature", 11)
    {
        req.set(http::field::host, "api.data.gov.sg");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    }

    void connectToApi()
    {
        auto it = resolver.resolve({"api.data.gov.sg", "443"});
        boost::asio::connect(ssock.lowest_layer(), it);
        ssock.handshake(ssl::stream_base::handshake_type::client);
    }

    std::string getResponse()
    {
        std::string response;

        http::write(ssock, req);

        boost::beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(ssock, buffer, res);
        response = boost::beast::buffers_to_string(res.body().data());

        return response;
    }
    ~Client()
    {
        ssock.shutdown(ec);
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

    std::unordered_map<std::string, std::string> data;

    Client client;
    client.connectToApi();

    std::string password = "writeonly";
    std::string login = "wo";
    std::string host = "test.mosquitto.org";
    std::string crt = "./certs/mosquitto.org.crt";

    std::string topicIDs = "api/temperature/";
    std::string topicStatus = "api/status";

    int port = 8885;
    int keepalive = 60;

    mosquitto_lib_init();

    mosquitto *mos = mosquitto_new(host.c_str(), true, NULL);

    mosquitto_tls_set(mos, crt.c_str(), NULL, NULL, NULL, NULL);

    mosquitto_username_pw_set(mos, login.c_str(), password.c_str());
    mosquitto_connect(mos, host.c_str(), port, keepalive);

    while (MOSQ_ERR_SUCCESS == mosquitto_loop(mos, -1, 1))
    {
        APIparse(client.getResponse(), data);

        mosquitto_publish(mos, NULL, (topicIDs + "S50").c_str(), strlen(data["S50"].c_str()), data["S50"].c_str(), 0, true);
        mosquitto_publish(mos, NULL, (topicIDs + "S60").c_str(), strlen(data["S60"].c_str()), data["S60"].c_str(), 0, true);
        mosquitto_publish(mos, NULL, (topicIDs + "S107").c_str(), strlen(data["S107"].c_str()), data["S107"].c_str(), 0, true);
        mosquitto_publish(mos, NULL, topicStatus.c_str(), strlen(data["api_status"].c_str()), data["api_status"].c_str(), 0, true);
    }
    

    mosquitto_disconnect(mos);
    mosquitto_destroy(mos);

    mosquitto_lib_cleanup();

    return 0;
}
