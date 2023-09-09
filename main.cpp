#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

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

void findAPIValue(boost::property_tree::ptree const &node, std::string field, std::string &out)
{
    auto it = node.find(field);

    if (it == node.not_found())
        for (auto const &child : node)
            findAPIValue(child.second, field, out);

    else
        out = it->second.data();
}
void findIDsValues(boost::property_tree::ptree const &node, std::string field, std::string &out)
{
    auto it = node.find("station_id");

    if (it == node.not_found())
        for (auto const &child : node)
            findIDsValues(child.second, field, out);
    else
    {
        auto data = it->second.data();
        if (data == field)
        {
            it = node.find("value");
            out = it->second.data();
        }
    }
}

std::string api_status = "c";

void getFieldFromJson(std::string json, std::vector<std::pair<std::string, std::string>> &IDs)
{
    std::pair<boost::property_tree::ptree::const_assoc_iterator, boost::property_tree::ptree> tmp;
    std::stringstream jsonEncoded(json);
    boost::property_tree::ptree root;
    boost::property_tree::read_json(jsonEncoded, root);

    if (root.empty())
        return;

    findAPIValue(root, "status", api_status);

    for (size_t i = 0; i < IDs.size(); i++)
    {
        findIDsValues(root, IDs[i].first, IDs[i].second);
    }
}

int main()
{

    std::vector<std::pair<std::string, std::string>> IDs = {{"S50", "0"}, {"S107", "0"}, {"S60", "0"}};

    Client client;
    client.connectToApi();

    std::string PASSWORD = "writeonly";
    std::string LOGIN = "wo";
    std::string HOST = "test.mosquitto.org";
    std::string CRT = "./certs/mosquitto.org.crt";

    std::string topicIDs = "api/temperature/";
    std::string topicStatus = "api/status";

    int PORT = 8885;
    int KEEPALIVE = 60;

    mosquitto_lib_init();

    mosquitto *mos = mosquitto_new(HOST.c_str(), true, NULL);

    mosquitto_tls_set(mos, CRT.c_str(), NULL, NULL, NULL, NULL);

    mosquitto_username_pw_set(mos, LOGIN.c_str(), PASSWORD.c_str());
    mosquitto_connect(mos, HOST.c_str(), PORT, KEEPALIVE);

    while (MOSQ_ERR_SUCCESS == mosquitto_loop(mos, -1, 1))
    {

        getFieldFromJson(client.getResponse(), IDs);

        for (int i = 0; i < IDs.size(); ++i)
        {
            mosquitto_publish(mos, NULL,  (topicIDs + IDs[i].first).c_str(), strlen(IDs[i].second.c_str()), IDs[i].second.c_str(), 0, true);
        }

        mosquitto_publish(mos, NULL, topicStatus.c_str(), strlen(api_status.c_str()), api_status.c_str(), 0, true);
    }

    mosquitto_disconnect(mos);
    mosquitto_destroy(mos);

    mosquitto_lib_cleanup();

    return 0;
}
