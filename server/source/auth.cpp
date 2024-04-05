#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <pqxx/pqxx>
#include <sw/redis++/redis++.h>
// #include <cassandra.h>
// #include "logs.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
namespace redis = sw::redis;

std::string generate_token() {
  const int length = 32;
  const std::string characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<> distribution(0, characters.size() - 1);

  std::string token;
  for (size_t i = 0; i < length; i++) {
    token += characters[distribution(generator)];
  }

  return token;
}

bool login_user_via_token(redis::Redis& redis, const std::vector<std::string>& log_info) {
  return 0;
}

bool is_user_exists(pqxx::work& tx, const std::vector<std::string>& info) {
  return tx.query_value<int>(
    "SELECT COUNT(id) FROM users WHERE login = $1 AND password = $2",
    pqxx::params{info[0], info[1]}
  );
}

bool add_new_user(pqxx::work& tx, const std::vector<std::string>& reg_info) {
  if(!is_user_exists(tx, reg_info)) {
    tx.exec_params(
      "INSERT INTO users (login, password, username) VALUES ($1, $2, $3)",
      reg_info[0], reg_info[1], reg_info[2]
    );

    tx.commit();
    return 1;
  }
  return 0;
}

template <class Body, class Allocator>
http::message_generator handle_request(http::request<Body, 
  http::basic_fields<Allocator>>&& req) {
  auto const bad_request = [&req](beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();
    return res;
  };

  auto const not_found = [&req](beast::string_view target) {
    http::response<http::string_body> res{http::status::not_found, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.set("type", "bad");
    res.keep_alive(req.keep_alive());
    res.body() = "The resource '" + std::string(target) + "' was not found.";
    res.prepare_payload();
    return res;
  };

  auto const server_error = [&req](beast::string_view what) {
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.set("type", "bad");
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + std::string(what) + "'";
    res.prepare_payload();
    return res;
  };

  auto const ok = [&req]() {
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    return res;
  };

  if(req.method() != http::verb::get &&
    req.method() != http::verb::head) {
      return bad_request("Unknown HTTP-method");
  }

  //заменить айпи бд
  try {
    pqxx::connection c("dbname=chat user=admin123 password=password123 \
      hostaddr=127.0.0.1 port=5432");
    pqxx::work tx{c};

    auto redis = redis::Redis("tcp://127.0.0.1:6379");

    if(req.target() == "/register") {
      auto res = ok();

      const std::vector<std::string> reg_info = {
        std::string(req.find("login")->value()),
        std::string(req.find("password")->value()),
        std::string(req.find("username")->value())
      };

      if(add_new_user(tx, reg_info)) {
        auto token = generate_token();
        redis.set(reg_info[0], token);
        res.set("type", "ok");
        res.set("login", reg_info[0]);
        res.set("token", token);
      }
      else {
        res.set("type", "bad");
        res.set("error", "user_exists");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/login") {
      auto res = ok();

      if(std::string(req.find("log-type")->value()) == "token") {
        const std::vector<std::string> log_info = {
          std::string(req.find("token")->value())
        };
        if(login_user_via_token(redis, log_info)) {
        //добавить генерацию токена и отправлять токен
          // redis.set(log_info[0], token);
          // res.set("type", "ok");
          // res.set("login", log_info[0]);
          // res.set("token", token);
        }
        else {
          res.body() = "{\"err_login\": \"wrong token\"}";
        }
      }
      else {
        const std::vector<std::string> log_info = {
          std::string(req.find("login")->value()),
          std::string(req.find("password")->value())
        };

        if(is_user_exists(tx, log_info)) {
          auto token = generate_token();
          redis.set(log_info[0], token);
          res.set("type", "ok");
          res.set("login", log_info[0]);
          res.set("token", token);
        }
        else {
          res.set("type", "bad");
          res.set("error", "wrong_login_or_password");
        }
      }

      res.prepare_payload();
      return res;
    }




  }
  catch (std::exception const &e) {
    std::cout << e.what() << std::endl;
    return server_error("server_problem");
  }

  return not_found(req.target());
}

//------------------------------------------------------------------------------

void fail(beast::error_code ec, char const* what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

class session : public std::enable_shared_from_this<session> {
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;

  public:
    session(tcp::socket&& socket) : stream_(std::move(socket)) {
    }

    void run() {
      net::dispatch(stream_.get_executor(), beast::bind_front_handler(
        &session::do_read, shared_from_this()));
    }

    void do_read() {
      req_ = {};
      stream_.expires_after(std::chrono::seconds(30));

      http::async_read(stream_, buffer_, req_, beast::bind_front_handler(
        &session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
      boost::ignore_unused(bytes_transferred);

      if(ec == http::error::end_of_stream) {
        return do_close();
      }
      if(ec) {
        return fail(ec, "read");
      }

      send_response(handle_request(std::move(req_)));
    }

    void send_response(http::message_generator&& msg) {
      bool keep_alive = msg.keep_alive();

      beast::async_write(stream_, std::move(msg), beast::bind_front_handler(
        &session::on_write, shared_from_this(), keep_alive));
    }

    void on_write(bool keep_alive, beast::error_code ec, 
      std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

      if(ec) {
        return fail(ec, "write");
      }
      if(!keep_alive) {
        return do_close();
      }

      do_read();
    }

    void do_close() {
      beast::error_code ec;
      stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    }
};

//------------------------------------------------------------------------------

class listener : public std::enable_shared_from_this<listener> {
  net::io_context& ioc_;
  tcp::acceptor acceptor_;

  public:
    listener(net::io_context& ioc, tcp::endpoint endpoint)
      : ioc_(ioc), acceptor_(net::make_strand(ioc)) {
      beast::error_code ec;

      acceptor_.open(endpoint.protocol(), ec);
      if(ec) {
        fail(ec, "open");
        return;
      }

      acceptor_.set_option(net::socket_base::reuse_address(true), ec);
      if(ec) {
        fail(ec, "set_option");
        return;
      }

      acceptor_.bind(endpoint, ec);
      if(ec) {
        fail(ec, "bind");
        return;
      }

      acceptor_.listen(net::socket_base::max_listen_connections, ec);
      if(ec) {
        fail(ec, "listen");
        return;
      }
    }

    void run() {
      do_accept();
    }

  private:
    void do_accept() {
      acceptor_.async_accept(net::make_strand(ioc_), beast::bind_front_handler(
        &listener::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
      if(ec) {
        fail(ec, "accept");
        return;
      }
      else {
        std::make_shared<session>(std::move(socket))->run();
      }

      do_accept();
    }
};

int main(int argc, char* argv[]) {
  if(argc != 4) {
    std::cerr 
      << "Usage: auth <address> <port> <threads>\n"
      << "Example:\n"
      << "auth 0.0.0.0 8080 1\n";
    return EXIT_FAILURE;
  }

  auto const address = net::ip::make_address(argv[1]);
  auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
  auto const threads = std::max<int>(1, std::atoi(argv[3]));

  net::io_context ioc{threads};
  std::make_shared<listener>(ioc, tcp::endpoint{address, port})->run();

  std::vector<std::thread> v;
  v.reserve(threads - 1);
  for(auto i = threads - 1; i > 0; --i) {
    v.emplace_back(
      [&ioc] {
      ioc.run();
    });
  }

  ioc.run();

  return EXIT_SUCCESS;
}