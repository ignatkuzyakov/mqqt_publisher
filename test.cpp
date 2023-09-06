#include <boost/beast.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <string>
#include <iostream>

const static std::string MAIN_API = "api.guildwars2.com";
const static std::string API_ARGUMENTS = "/v2/items";

using boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

class Client
{
public:
  
    static std::string getResponse(std::string ip)
    {
        // Create a context that uses the default paths for
        // finding CA certificates.
        ssl::context ctx(ssl::context::sslv23);
       

        // Open a socket and connect it to the remote host.
        boost::asio::io_context io_context;
        ssl_socket sock(io_context, ctx);
        tcp::resolver resolver(io_context);
        auto it = resolver.resolve({"api.data.gov.sg", "443"});
        boost::asio::connect(sock.lowest_layer(), it);
        

       

    
        sock.handshake(ssl_socket::client);

        std::string response;
        {
            boost::beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            boost::system::error_code ec;
            int test = http::read(sock, buffer, res, ec);

            response = boost::beast::buffers_to_string(res.body().data());
        }

        return response;
    }
};

int main()
{
    Client Cli;
    std::cout << Cli.getResponse("S");
}