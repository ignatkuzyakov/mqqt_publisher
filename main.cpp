#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>

#include <iostream>
#include <vector>

using namespace boost::asio;
namespace http = boost::beast::http;

class Client
{

public:
    std::string getResp()
    {
        boost::system::error_code ec;

        io_service svc;
        ssl::context ctx(ssl::context::method::sslv23_client);
        ssl::stream<ip::tcp::socket> ssock(svc, ctx);

        ip::tcp::resolver resolver(svc);

        auto it = resolver.resolve({"api.data.gov.sg", "443"});
        boost::asio::connect(ssock.lowest_layer(), it);

        ssock.handshake(ssl::stream_base::handshake_type::client);

        http::request<http::string_body> req(http::verb::get, "/v1/environment/air-temperature", 11);

        req.set(http::field::host, "api.data.gov.sg");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        http::write(ssock, req);

        std::string response;
        {
            boost::beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(ssock, buffer, res);
            response = boost::beast::buffers_to_string(res.body().data());
        }

        ssock.shutdown(ec);
        return response;
    }
};

struct Searcher
{
    struct Path
    {
        std::string const &key;
        Path const *prev;
    };

    void operator()(boost::property_tree::ptree const &node, Path const *path = nullptr) const
    {
        auto it = node.find("status");

        if (it == node.not_found())
        {
            for (auto const &child : node)
            {
                Path next{child.first, path};
                (*this)(child.second, &next);
            }
        }
        else
        {
            std::string data = boost::lexical_cast<std::string>(it->second.data());
            std::cout << "data : " << data << std::endl;
        }
    }
    void operator()(boost::property_tree::ptree const &node, std::string field, Path const *path = nullptr) const
    {
        auto it = node.find("station_id");

        if (it == node.not_found())
        {
            for (auto const &child : node)
            {
                Path next{child.first, path};
                (*this)(child.second, field, &next);
            }
        }
        else
        {
            std::string data = boost::lexical_cast<std::string>(it->second.data());

            if (data == field)
            {
                auto it = node.find("value");
                double value = boost::lexical_cast<double>(it->second.data());
                std::cout << "data : " << data << "; value : " << value << std::endl;
            }
        }
    }
};

void getFieldFromJson(std::string json, std::vector<std::string> IDs);

int main()
{
    std::vector<std::string> IDs = {"S50", "S107", "S60"};

    Client cli;
    std::string res = cli.getResp();

    getFieldFromJson(res, IDs);
}

void getFieldFromJson(std::string json, std::vector<std::string> IDs)
{
    std::stringstream jsonEncoded(json);
    boost::property_tree::ptree root;
    boost::property_tree::read_json(jsonEncoded, root);

    if (root.empty())
        return;

    for (size_t i = 0; i < IDs.size(); i++)
    {
        Searcher{}(root, IDs[i]);
    }
    Searcher{}(root);

    return;
}