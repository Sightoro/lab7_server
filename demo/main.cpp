#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/config.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include <fstream>
#include <beast/version.hpp>
#include <boost/asio/io_service.hpp>

namespace beast = boost::beast;
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
nlohmann::json res_array;
//------------------------------------------------------------------------------

/* This function produces an HTTP response for the given
request. The type of the response object depends on the
contents of the request, so the interface requires the
caller to pass a generic lambda for receiving the response.*/

template<class Body, class Allocator, class Send>
void
handle_request(
    http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send)
{
  // Returns a not found response
  auto const not_found =
      [&req](beast::string_view target)
  {
    http::response<http::string_body> res{http::status::not_found, 11};
    res.set(http::field::server, BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.set(beast::http::field::body, "The resource '" + std::string(target) + "' was not found.");
    res.prepare_payload();
    return res;
  };

  // Returns a server error response
  auto const server_error =
      [&req](beast::string_view what)
  {
    http::response<http::string_body> res{http::status::internal_server_error, 11};
    res.set(http::field::server, BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.set(beast::http::field::body, "An error occurred: '" + std::string(what) + "'");
    res.prepare_payload();
    return res;
  };

  // Attempt to open the file
  beast::error_code ec;
  http::string_body::value_type body = res_array.dump(4);

  // Handle the case where the file doesn't exist
  if(ec == beast::errc::no_such_file_or_directory) {
    return send(not_found(req.target()));
  }

  // Handle an unknown error
  if(ec) {
    return send(server_error(ec.message()));
  }


  // Respond to GET request
  http::response<http::string_body> res{
      std::piecewise_construct,
      std::make_tuple(body),
      std::make_tuple(http::status::ok, 11)};
  res.set(http::field::server, BEAST_VERSION_STRING);
  res.set(http::field::content_type, "application/json");
  res.prepare_payload();
  res.keep_alive(req.keep_alive());
  res_array.clear();
  return send(std::move(res));
}

//------------------------------------------------------------------------------

// Report a failure
void
fail(beast::error_code ec, char const* what)
{
  std::cerr << what << ": " << ec.message() << "\n";
}

// This is the C++11 equivalent of a generic lambda.
// The function object is used to send an HTTP message.
template<class Stream>
struct send_lambda
{
  Stream& stream_;
  bool& close_;
  beast::error_code& ec_;

  explicit
      send_lambda(
          Stream& stream,
          bool& close,
          beast::error_code& ec)
      : stream_(stream)
        , close_(close)
        , ec_(ec)
  {
  }

  template<bool isRequest, class Body, class Fields>
  void
  operator()(http::message<isRequest, Body, Fields>&& msg) const
  {
    // We need the serializer here because the serializer requires
    // a non-const file_body, and the message oriented version of
    // http::write only works with const messages.
    http::serializer<isRequest, Body, Fields> sr{msg};
    http::write(stream_, sr, ec_);
  }
};


// Handles an HTTP server connection
void
do_session(tcp::socket& socket)
{
  bool close = false;
  beast::error_code ec;

  // This buffer is required to persist across reads
  beast::flat_buffer buffer;

  // This lambda is used to send messages
  send_lambda<tcp::socket> lambda{socket, close, ec};

  for(;;)
  {
    // Read a request
    http::request<http::string_body> req;
    http::read(socket, buffer,req, ec);
    if(req.body()[0]=='{'){
      nlohmann::json req_array = nlohmann::json::parse(req.body());

      std::ifstream file(R"(/Users/itsumaden/C++/PartyOne/lab7.1/lab-07-http-server/include/suggestions.json)");
      nlohmann::json json_file = nlohmann::json::parse(file);
      std::sort(json_file.begin(), json_file.end());
      int position_count = 0;
      res_array["suggestions"] = {};
      for(size_t i = 0; i < json_file.size(); i++){
        if(json_file[i].at("id") == req_array.at("input")){
          res_array["suggestions"].push_back(nlohmann::json::object({
              {"text", json_file[i].at("name")},
              {"position", position_count}
          }));
          position_count++;
        }
      }
    }
    if(ec == http::error::end_of_stream)
      break;
    if(ec){
      return fail(ec, "read");}

    // Send the response
    handle_request(std::move(req), lambda);
    if(ec)
      return fail(ec, "write");
    if(close)
    {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      break;
    }
  }

  // Send a TCP shutdown
  socket.shutdown(tcp::socket::shutdown_send, ec);

  // At this point the connection is closed gracefully
}


int main() {
  try
  {
    auto const address = net::ip::make_address("127.0.0.1");
    auto const port = 80;
    auto const doc_root = std::make_shared<std::string>("/v1/api/suggest");

    // The io_context is required for all I/O
    net::io_service ioc{1};

    // The acceptor receives incoming connections
    tcp::acceptor acceptor = {ioc, boost::asio::ip::tcp::endpoint{address, port}};
    for(;;)
    {
      // This will receive the new connection
      tcp::socket socket{ioc};

      // Block until we get a connection
      acceptor.accept(socket);

      // Launch the session, transferring ownership of the socket
      std::thread{std::bind(
                      &do_session,
                      std::move(socket))}.detach();
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl ;
    return EXIT_FAILURE;
  }
}