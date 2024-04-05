#ifndef LOGS_HPP
#define LOGS_HPP
#pragma warning(push)
#pragma warning(disable:4819)
#   include <boost/shared_ptr.hpp>
#   include <boost/date_time/posix_time/posix_time_types.hpp>
#   include <boost/log/trivial.hpp>
#   include <boost/log/core.hpp>
#   include <boost/log/expressions.hpp>
#   include <boost/log/sources/logger.hpp>
#   include <boost/log/utility/setup/file.hpp>
#   include <boost/log/utility/setup/console.hpp>
#   include <boost/log/utility/setup/common_attributes.hpp>
#   include <boost/log/support/date_time.hpp>
#   include <boost/log/sinks/sync_frontend.hpp>
#   include <boost/log/sinks/text_file_backend.hpp>
#   include <boost/log/sinks/text_ostream_backend.hpp>
#   include <boost/log/attributes/named_scope.hpp>
#pragma warning(pop)
#pragma warning(disable:4503)

/**
 * @brief Initialization of logging.
 */
static void init_log(void) {
  namespace log = boost::log;
  namespace keywords = boost::log::keywords;
  namespace expr = boost::log::expressions;

  log::add_common_attributes();
  log::core::get()->add_global_attribute("Scope", log::attributes::named_scope());
  log::core::get()->set_filter(log::trivial::severity >= log::trivial::trace);

  auto fmt_timestamp = expr::
    format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S.%f");
  auto fmt_thread_id = expr::
    attr<boost::log::attributes::current_thread_id::value_type>("ThreadID");
  auto fmt_severity = expr::
    attr<boost::log::trivial::severity_level>("Severity");
  auto fmt_scope = expr::
    format_named_scope("Scope",
    keywords::format = "%n(%f:%l)",
    keywords::iteration = expr::reverse,
    keywords::depth = 2);
  boost::log::formatter log_fmt =
    expr::format("[%1%] (%2%) [%3%] [%4%] %5%")
    % fmt_timestamp % fmt_thread_id % fmt_severity % fmt_scope % expr::smessage;

  auto console_sink = boost::log::add_console_log(std::clog);
  console_sink->set_formatter(log_fmt);

  // auto fs_sink = boost::log::add_file_log(
  //   keywords::file_name = "test_%Y-%m-%d_%H-%M-%S.%N.log",
  //   keywords::rotation_size = 10 * 1024 * 1024,
  //   keywords::min_free_space = 30 * 1024 * 1024,
  //   keywords::open_mode = std::ios_base::app);
  // fs_sink->set_formatter(log_fmt);
  // fs_sink->locked_backend()->auto_flush(true);
}

// /**
//  * @brief Test log call.
//  */
// static void test(void) {
//   BOOST_LOG_FUNCTION();
//   BOOST_LOG_TRIVIAL(info) << "Info Log in Test()";
// }

#endif // LOGS_HPP