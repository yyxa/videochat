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
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <sw/redis++/redis++.h>
#include <cassandra.h>
#include "logs.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
namespace redis = sw::redis;
using json = nlohmann::json;

const char* hosts_ = "172.20.0.2"; //172.20.0.2

// insert into chats (chat_id, time_updated, edited, from_user_id, message)  
// values (fbe19e56-a3f7-46a1-b151-e06be6109503, toUnixTimestamp(now()), false, 1, 'adisabeba');

json http_connection::get_request_json_body() {
  const auto& body = request_.body();
  std::stringstream ss;

  for(const auto& buffer : body.data()) {
    ss << beast::make_printable(buffer);
  }

  return json::parse(ss);
}

json get_json_row(const CassResult* result, CassIterator* rows) {
  size_t column_count = cass_result_column_count(result);
  const CassRow* row = cass_iterator_get_row(rows);
  json json_row;

  for (size_t i = 0; i < column_count; ++i) {
    const char* column_name;
    size_t column_name_length;
    cass_result_column_name(result, i, &column_name, &column_name_length);
    std::string col_name(column_name, column_name_length);

    const CassValue* column = cass_row_get_column(row, i);

    CassValueType type = cass_value_type(column);

    switch (type) {
      case CASS_VALUE_TYPE_UUID: {
        CassUuid uuid;
        char uuid_str[CASS_UUID_STRING_LENGTH];
        cass_value_get_uuid(column, &uuid);
        cass_uuid_string(uuid, uuid_str);
        json_row[col_name] = uuid_str;
        break;
      }
      case CASS_VALUE_TYPE_BIGINT: {
        cass_int64_t column_value;
        cass_value_get_int64(column, &column_value);
        json_row[col_name] = column_value;
        break;
      }
      case CASS_VALUE_TYPE_BOOLEAN: {
        cass_bool_t column_value;
        cass_value_get_bool(column, &column_value);
        json_row[col_name] = (column_value == cass_true ? true : false);
        break;
      }
      default: {
        const char* column_value;
        size_t column_value_length;
        cass_value_get_string(column, &column_value, &column_value_length);
        std::string col_value(column_value, column_value_length);
        json_row[col_name] = col_value;
        break;
      }
    }
  }

  return json_row;
}

std::string make_chat(std::unique_ptr<CassSession, decltype(&cass_session_free)>& scylla_session, 
  const int& user_id) {
    std::string chat_id;
  const char* query =
    "INSERT INTO keyspace_chat.user_chats (user_id, chat_id, time_updated) "
    " VALUES (?, uuid(), toUnixTimestamp(now()))";

  auto statement = std::unique_ptr<CassStatement, 
    decltype(&cass_statement_free)>(cass_statement_new(query, 1), &cass_statement_free);

  cass_statement_bind_int64(statement.get(), 0, user_id);

  auto cluster = std::unique_ptr<CassCluster, 
    decltype(&cass_cluster_free)>(cass_cluster_new(), &cass_cluster_free);

  cass_cluster_set_contact_points(cluster.get(), hosts_);

  auto connect_future = std::unique_ptr<CassFuture, 
    decltype(&cass_future_free)>(cass_session_connect(
      scylla_session.get(), cluster.get()), &cass_future_free);

  if (cass_future_error_code(connect_future.get()) == CASS_OK) {
    CassFuture* result_future = cass_session_execute(scylla_session.get(), statement.get());
    
    if (cass_future_error_code(result_future) == CASS_OK) {
      const CassResult* result = cass_future_get_result(result_future);

      auto rows = std::unique_ptr<CassIterator, 
        decltype(&cass_iterator_free)>(cass_iterator_from_result(result), &cass_iterator_free);


      cass_result_free(result);
    } 
    else {
      const char* message;
      size_t message_length;
      cass_future_error_message(result_future, &message, &message_length);
      std::cout 
        << "Unable to run query: " << std::string(message, message_length) << std::endl;
    }

    cass_future_free(result_future);
  } 
  else {
    const char* message;
    size_t message_length;
    cass_future_error_message(connect_future.get(), &message, &message_length);
    std::cout 
      << "Unable to connect to db: " << std::string(message, message_length) << std::endl;
  }
  return chat_id;
}

bool is_chat_exists(std::unique_ptr<CassSession, decltype(&cass_session_free)>& scylla_session, 
  const int& user_id, const std::string& chat_id) {
  bool exists = false;
  const char* query =
    "SELECT * FROM keyspace_chat.user_chats WHERE user_id = ? AND chat_id = ?";

  auto statement = std::unique_ptr<CassStatement, 
    decltype(&cass_statement_free)>(cass_statement_new(query, 2), &cass_statement_free);
  
  CassUuid uuid_chat_id;
  cass_uuid_from_string(chat_id.c_str(), &uuid_chat_id);

  cass_statement_bind_int64(statement.get(), 0, user_id);
  cass_statement_bind_uuid(statement.get(), 1, uuid_chat_id);

  auto cluster = std::unique_ptr<CassCluster, 
    decltype(&cass_cluster_free)>(cass_cluster_new(), &cass_cluster_free);

  cass_cluster_set_contact_points(cluster.get(), hosts_);

  auto connect_future = std::unique_ptr<CassFuture, 
    decltype(&cass_future_free)>(cass_session_connect(
      scylla_session.get(), cluster.get()), &cass_future_free);

  if (cass_future_error_code(connect_future.get()) == CASS_OK) {
    CassFuture* result_future = cass_session_execute(scylla_session.get(), statement.get());
    
    if (cass_future_error_code(result_future) == CASS_OK) {
      const CassResult* result = cass_future_get_result(result_future);

      auto rows = std::unique_ptr<CassIterator, 
        decltype(&cass_iterator_free)>(cass_iterator_from_result(result), &cass_iterator_free);

      size_t total_rows = cass_result_row_count(result);

      if(total_rows == 1) {
        exists = true;
      }

      cass_result_free(result);
    } 
    else {
      const char* message;
      size_t message_length;
      cass_future_error_message(result_future, &message, &message_length);
      std::cout 
        << "Unable to run query: " << std::string(message, message_length) << std::endl;
    }

    cass_future_free(result_future);
  } 
  else {
    const char* message;
    size_t message_length;
    cass_future_error_message(connect_future.get(), &message, &message_length);
    std::cout 
      << "Unable to connect to db: " << std::string(message, message_length) << std::endl;
  }

  return exists;
}

json get_chat(std::unique_ptr<CassSession, decltype(&cass_session_free)>& scylla_session, 
  const std::string& chat_id) {
  json json_array = json::array();
  const char* query =
    "SELECT * FROM keyspace_chat.chats WHERE chat_id = ?";

  auto statement = std::unique_ptr<CassStatement, 
    decltype(&cass_statement_free)>(cass_statement_new(query, 1), &cass_statement_free);
  
  CassUuid uuid_chat_id;
  cass_uuid_from_string(chat_id.c_str(), &uuid_chat_id);

  cass_statement_bind_uuid(statement.get(), 0, uuid_chat_id);

  auto cluster = std::unique_ptr<CassCluster, 
    decltype(&cass_cluster_free)>(cass_cluster_new(), &cass_cluster_free);

  cass_cluster_set_contact_points(cluster.get(), hosts_);

  auto connect_future = std::unique_ptr<CassFuture, 
    decltype(&cass_future_free)>(cass_session_connect(
      scylla_session.get(), cluster.get()), &cass_future_free);

  if (cass_future_error_code(connect_future.get()) == CASS_OK) {
    CassFuture* result_future = cass_session_execute(scylla_session.get(), statement.get());
    
    if (cass_future_error_code(result_future) == CASS_OK) {
      const CassResult* result = cass_future_get_result(result_future);

      auto rows = std::unique_ptr<CassIterator, 
        decltype(&cass_iterator_free)>(cass_iterator_from_result(result), &cass_iterator_free);

      for (size_t i = 0; cass_iterator_next(rows.get()); ++i) {
        json_array.push_back(get_json_row(result, rows.get()));
      }

      cass_result_free(result);
    } 
    else {
      const char* message;
      size_t message_length;
      cass_future_error_message(result_future, &message, &message_length);
      std::cout 
        << "Unable to run query: " << std::string(message, message_length) << std::endl;
    }

    cass_future_free(result_future);
  } 
  else {
    const char* message;
    size_t message_length;
    cass_future_error_message(connect_future.get(), &message, &message_length);
    std::cout 
      << "Unable to connect to db: " << std::string(message, message_length) << std::endl;
  }

  return json_array;
}

json get_user_chats(std::unique_ptr<CassSession, decltype(&cass_session_free)>& scylla_session, 
  const int& id) {
  json json_array = json::array();
  const char* query =
    "SELECT * FROM keyspace_chat.user_chats WHERE user_id = ?";

  auto statement = std::unique_ptr<CassStatement, 
    decltype(&cass_statement_free)>(cass_statement_new(query, 1), &cass_statement_free);
  
  cass_statement_bind_int64(statement.get(), 0, id);

  auto cluster = std::unique_ptr<CassCluster, 
    decltype(&cass_cluster_free)>(cass_cluster_new(), &cass_cluster_free);

  cass_cluster_set_contact_points(cluster.get(), hosts_);

  auto connect_future = std::unique_ptr<CassFuture, 
    decltype(&cass_future_free)>(cass_session_connect(
      scylla_session.get(), cluster.get()), &cass_future_free);

  if (cass_future_error_code(connect_future.get()) == CASS_OK) {
    CassFuture* result_future = cass_session_execute(scylla_session.get(), statement.get());
    
    if (cass_future_error_code(result_future) == CASS_OK) {
      const CassResult* result = cass_future_get_result(result_future);

      auto rows = std::unique_ptr<CassIterator, 
        decltype(&cass_iterator_free)>(cass_iterator_from_result(result), &cass_iterator_free);

      for (size_t i = 0; cass_iterator_next(rows.get()); ++i) {
        json_array.push_back(get_json_row(result, rows.get()));
      }

      cass_result_free(result);
    } 
    else {
      const char* message;
      size_t message_length;
      cass_future_error_message(result_future, &message, &message_length);
      std::cout 
        << "Unable to run query: " << std::string(message, message_length) << std::endl;
    }

    cass_future_free(result_future);
  } 
  else {
    const char* message;
    size_t message_length;
    cass_future_error_message(connect_future.get(), &message, &message_length);
    std::cout 
      << "Unable to connect to db: " << std::string(message, message_length) << std::endl;
  }

  return json_array;
}

int get_user_id(pqxx::work& tx, const std::string& login) {
  return tx.query_value<int>(
    "SELECT id FROM users WHERE login = $1",
    pqxx::params{login}
  );
}

void change_userInfo(pqxx::work& tx, const std::vector<std::string>& info) {
    tx.exec0(
    "UPDATE users "
    "SET bio = '" + info[2] + "', "
    "username = '" + info[3] + "' "
    "WHERE login = '" + info[0] + "'"
  );
  tx.commit();
}

void delete_user(pqxx::work& tx, const std::string& login) {
  tx.exec0(
    "DELETE FROM users WHERE login = '" + login + "'"
  );
  tx.commit();
}

json get_users(pqxx::work& tx, const std::string& username) {
  json result = json::array();;
  std::string param = username + "%";
  for (auto [login, bio, username] : tx.query<std::string, std::string, std::string>(
    "SELECT login, bio, username FROM users WHERE username LIKE $1",
    pqxx::params{param}
  )) {
    result.push_back(
      {
        {"login", login},
        {"bio", bio},
        {"username", username}
      }
    );
  }
  return result;
}

json get_user_info(pqxx::work& tx, const std::vector<std::string>& info) {
  auto [username, bio] = tx.query1<std::string, std::string>(
    "SELECT username, bio FROM users WHERE login = $1",
    pqxx::params{info[2]}
  );
  return {{"username", username}, {"bio", bio}};
}

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

bool login_user_via_token(redis::Redis& redis, const std::string& login, const std::string& token) {
  auto val = redis.get(login);;
  if(val) {
    return *val == token ? 1 : 0;
  }
  return 0;
}

bool is_user_exists(pqxx::work& tx, const std::string& login, const std::string& password) {
  return tx.query_value<int>(
    "SELECT COUNT(id) FROM users WHERE login = $1 AND password = $2",
    pqxx::params{login, password}
  );
}

bool is_login_exists(pqxx::work& tx, const std::string& login) {
  return tx.query_value<int>(
    "SELECT COUNT(id) FROM users WHERE login = $1",
    pqxx::params{login}
  );
}

bool add_new_user(pqxx::work& tx, const std::vector<std::string>& reg_info) {
  if(!is_user_exists(tx, reg_info[0], reg_info[1])) {
    tx.exec_params(
      "INSERT INTO users (login, password, username, bio) VALUES ($1, $2, $3, ' ')",
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
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();
    return res;
  };

  auto const not_found = [&req](beast::string_view target) {
    http::response<http::string_body> res{http::status::not_found, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.set("type", "bad");
    res.keep_alive(req.keep_alive());
    res.body() = "The resource '" + std::string(target) + "' was not found.";
    res.prepare_payload();
    return res;
  };

  auto const server_error = [&req](beast::string_view what) {
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.set("type", "bad");
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + std::string(what) + "'";
    res.prepare_payload();
    return res;
  };

  auto const ok = [&req]() { // поменять статус
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());
    return res;
  };

  //заменить айпи бд
  try {
    pqxx::connection c("dbname=chat user=admin123 password=password123 \
      hostaddr=127.0.0.1 port=5432");
    pqxx::work tx{c};

    auto redis = redis::Redis("tcp://127.0.0.1:6379");

    std::unique_ptr<CassSession, decltype(&cass_session_free)> scylla_session = 
      std::unique_ptr<CassSession, decltype(&cass_session_free)>
        (cass_session_new(), &cass_session_free);


    if(req.target() == "/register") {
      auto res = ok();

      std::vector<std::string> reg_info = {
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
        std::vector<std::string> log_info = {
          std::string(req.find("login")->value()),
          std::string(req.find("token")->value())
        };
        if(login_user_via_token(redis, log_info[0], log_info[1])
          && is_login_exists(tx, log_info[0])) {
          auto val = redis.get(log_info[0]);;
          if(val) {
            res.set("type", "ok");
            res.set("login", log_info[0]);
            res.set("token", *val);
          }
        }
        else {
          res.set("type", "bad");
          res.set("error", "wrong_token");
        }
      }
      else if(std::string(req.find("log-type")->value()) == "password"){
        std::vector<std::string> log_info = {
          std::string(req.find("login")->value()),
          std::string(req.find("password")->value())
        };

        if(is_user_exists(tx, log_info[0], log_info[1])) {
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
      else {
        res.set("type", "bad");
        res.set("error", "unknown_log-type");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/chats") {
      auto res = ok();

      std::vector<std::string> info = {
        std::string(req.find("login")->value()),
        std::string(req.find("token")->value())
      };

      if(login_user_via_token(redis, info[0], info[1])) {
        auto id = get_user_id(tx, info[0]);
        auto data = get_user_chats(scylla_session ,id);
        res.body() = data.dump();
        res.set("type", "ok");
      }
      else {
        res.set("type", "bad");
        res.set("error", "wrong_token");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/chat") {
      auto res = ok();

      std::vector<std::string> info = {
        std::string(req.find("login")->value()),
        std::string(req.find("token")->value()),
        std::string(req.find("chat-id")->value())
      };

      if(login_user_via_token(redis, info[0], info[1])) {
        auto data = get_chat(scylla_session ,info[2]);
        res.body() = data.dump();
        res.set("type", "ok");
      }
      else {
        res.set("type", "bad");
        res.set("error", "wrong_token");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/deleteMessage") {
      auto res = ok();

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/sendMessage") {
      auto res = ok();
      
      std::vector<std::string> info = {
        std::string(req.find("login")->value()),
        std::string(req.find("token")->value()),
        std::string(req.find("chat-id")->value()),
        std::string(req.body())
      };

      if(login_user_via_token(redis, info[0], info[1])) {
        if(!is_chat_exists(scylla_session, user_id, info[2])) {
          info[2] = make_chat(scylla_session ,info[2]);
        }
        auto user_id = get_user_id(tx, info[0]);
        send_message(scylla_session, info);
        res.body() = data.dump();
        res.set("type", "ok");
      }
      else {
        res.set("type", "bad");
        res.set("error", "wrong_token");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/editMessage") {
      auto res = ok();

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/deleteChat") {
      auto res = ok();

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/clearHistory") {
      auto res = ok();

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/findUser") { // TODO:
      auto res = ok();

      std::vector<std::string> info = {
        std::string(req.find("login")->value()),
        std::string(req.find("token")->value()),
        std::string(req.find("username")->value())
      };

      if(login_user_via_token(redis, info[0], info[1])) {
        auto data = get_users(tx, info[2]);
        res.body() = data.dump();
        res.set("type", "ok");
      }
      else {
        res.set("type", "bad");
        res.set("error", "wrong_token");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/userInfo") {
      auto res = ok();

      std::vector<std::string> info = {
        std::string(req.find("login")->value()),
        std::string(req.find("token")->value()),
        std::string(req.find("who")->value())
      };

      if(login_user_via_token(redis, info[0], info[1])) {
        auto data = get_user_info(tx, info);
        res.body() = data.dump();
        res.set("type", "ok");
      }
      else {
        res.set("type", "bad");
        res.set("error", "wrong_token");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/deleteAccount") {
      auto res = ok();

      std::vector<std::string> info = {
        std::string(req.find("login")->value()),
        std::string(req.find("password")->value()),
        std::string(req.find("token")->value())
      };

      if(login_user_via_token(redis, info[0], info[2]) 
        && is_user_exists(tx, info[0], info[1])) {
        delete_user(tx, info[0]);
        redis.del(info[0]);
        res.set("type", "ok");
      }
      else {
        res.set("type", "bad");
        res.set("error", "wrong_token_or_login_or_password");
      }

      res.prepare_payload();
      return res;
    }

    if(req.target() == "/changeUserInfo") {
      auto res = ok();

      std::vector<std::string> info = {
        std::string(req.find("login")->value()),
        std::string(req.find("token")->value()),
        std::string(req.find("bio")->value()),
        std::string(req.find("username")->value())
      };

      if(login_user_via_token(redis, info[0], info[1])) {
        change_userInfo(tx, info);
        res.set("type", "ok");
      }
      else {
        res.set("type", "bad");
        res.set("error", "wrong_token_or_login_or_password");
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