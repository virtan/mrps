#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <thread>
#include <chrono>
#include <memory>
#include <memory>
#include <cstdlib>
#include <algorithm>
#include <boost/asio.hpp>

namespace {

const std::size_t clients_max = 10000;
const std::size_t bear_after_mcs = 1000;
const std::size_t send_each_mcs = 10000;
const std::size_t client_timeout_mcs = 30 * 1000 * 1000;

std::size_t thread_num;

std::vector<std::unique_ptr<boost::asio::io_service>> services;
std::atomic_size_t services_i(0);
boost::asio::ip::tcp::resolver::iterator connect_to;

std::atomic_size_t children(0);
std::atomic_size_t connection_active(0);
std::atomic_size_t connection_errors(0);
std::atomic_size_t exchange_errors(0);
std::atomic_size_t connection_summ_mcs(0);
std::atomic_size_t latency_summ_mcs(0);
std::atomic_size_t msgs(0);

class timer {
public:
  timer() {
    reset();
  }

  void start() {
    start_time = std::chrono::steady_clock::now();
  }

  std::size_t current() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
  }

  void reset() {
    start();
  }

private:
  std::chrono::steady_clock::time_point start_time;
};

class connection_handler : public std::enable_shared_from_this<connection_handler> {
public:
  connection_handler() :
    service_id(++services_i),
    my_service(*services[service_id % thread_num]),
    s(my_service),
    bear_tmr(my_service, boost::posix_time::microseconds(bear_after_mcs)),
    exch_tmr(my_service),
    errored(false) {}

  void start() {
    if (service_id >= clients_max) {
      return;
    }
    bear_tmr.async_wait(std::bind(
        &connection_handler::new_connection_handler, shared_from_this(), std::placeholders::_1));
    connect_timer.reset();
    ++connection_active;
    boost::asio::async_connect(s, connect_to, std::bind(
        &connection_handler::connect, shared_from_this(), std::placeholders::_1));
  }

private:
  void new_connection_handler(const boost::system::error_code& e) {
    assert(!e);
    std::make_shared<connection_handler>()->start();
  }

  void connect(const boost::system::error_code& e) {
    ++children;
    --connection_active;
    if (e) {
      errored = true;
      ++connection_errors;
      return;
    }
    std::size_t spent_mcs = connect_timer.current();
    if (spent_mcs > client_timeout_mcs) {
      errored = true;
      ++connection_errors;
      return;
    }
    connection_summ_mcs += spent_mcs;
    memset(send_buf, ' ', 32);
    snprintf(send_buf, 32, "%d", rand());
    send_buf[31] = '\n';
    send_buf[32] = 0;
    s.set_option(boost::asio::ip::tcp::no_delay(true));
    read_staff(boost::system::error_code());
    send_staff(boost::system::error_code());
  }

  void send_staff(const boost::system::error_code& e) {
    assert(!e);
    if (errored) {
      return;
    }
    exch_tmr.expires_from_now(boost::posix_time::microseconds(rand() % (2 * send_each_mcs)));
    exch_tmr.async_wait(std::bind(
        &connection_handler::send_staff, shared_from_this(), std::placeholders::_1));
    tqueue.push(timer());
    boost::asio::async_write(s, boost::asio::mutable_buffers_1(send_buf, 32), std::bind(
        &connection_handler::stub, shared_from_this(), std::placeholders::_1));
  }

  void stub(const boost::system::error_code& e) {
    if (errored) {
      return;
    }
    if (e) {
      errored = true;
      ++exchange_errors;
      return;
    }
  }

  void read_staff(const boost::system::error_code& e) {
    if (errored) {
      return;
    }
    if (e) {
      errored = true;
      ++exchange_errors;
      return;
    }
    memset(read_buf, 0, 33);
    boost::asio::async_read(s, boost::asio::mutable_buffers_1(read_buf, 32), std::bind(
        &connection_handler::compare_staff, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
  }

  void compare_staff(const boost::system::error_code& e, std::size_t transferred) {
    if (errored) {
      return;
    }
    if (e || transferred != 32 || strncmp(send_buf, read_buf, 32)) {
      errored = true;
      ++exchange_errors;
      return;
    }
    std::size_t spent_mcs = tqueue.front().current();
    tqueue.pop();
    if (spent_mcs > client_timeout_mcs) {
      errored = true;
      ++exchange_errors;
      return;
    }
    ++msgs;
    latency_summ_mcs += spent_mcs;
    read_staff(e);
  }

  std::size_t service_id;
  boost::asio::io_service& my_service;
  boost::asio::ip::tcp::socket s;
  boost::asio::deadline_timer bear_tmr;
  boost::asio::deadline_timer exch_tmr;
  char send_buf[33];
  char read_buf[33];
  timer connect_timer;
  bool errored;
  std::queue<timer> tqueue;
};

void print_stat(bool final = false) {
  if (final) {
    std::cout << "\nFinal statistics:\n";
  }
  std::size_t conn_avg = connection_summ_mcs / std::max((std::size_t) children - connection_errors, (std::size_t) 1);
  std::size_t lat_avg = latency_summ_mcs / std::max((std::size_t) msgs, (std::size_t) 1);
  std::cout << "Chld: " << children << " ConnErr: " << (final ? connection_errors + connection_active : static_cast<std::size_t>(connection_errors)) << " ExchErr: " << exchange_errors << " ConnAvg: " << (((double) conn_avg) / 1000) << "ms LatAvg: " << (((double) lat_avg) / 1000) << "ms  Msgs: " << msgs << "\n";
}

} // anonymous namespace

int main(int args, char** argv) {
  if (args < 2) {
    std::cout << "Usage: " << argv[0] << " <host> [port = 32000 [threads = 24]]" << std::endl;
    return EXIT_FAILURE;
  }
  std::string host = argv[1];
  std::string port = args > 2 ? argv[2] : "32000";
  thread_num = args > 3 ? std::atoi(argv[3]) : 24;
  std::vector<std::thread> threads;
  threads.reserve(thread_num);
  services.reserve(thread_num);
  for(std::size_t i = 0; i < thread_num; ++i) {
    services.emplace_back(std::make_unique<boost::asio::io_service>(1));
    threads.emplace_back([i] {
      boost::asio::io_service::work worker(*services[i]);
      services[i]->run();
    });
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::cout << "Starting tests" << std::endl;
  boost::asio::ip::tcp::resolver resolver(*services[0]);
  boost::asio::ip::tcp::resolver::query query(host, port);
  connect_to = resolver.resolve(query);
  std::make_shared<connection_handler>()->start();
  for (std::size_t i = 0; i < 60; i += 5) {
    print_stat();
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  std::for_each(services.begin(), services.end(), [](std::unique_ptr<boost::asio::io_service>& s) {
    s->stop();
  });
  std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
  print_stat(true);
  return EXIT_SUCCESS;
}
