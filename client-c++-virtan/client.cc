#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <thread>
#include <type_traits>
#include <chrono>
#include <memory>
#include <memory>
#include <cstdlib>
#include <random>
#include <algorithm>
#include <boost/asio.hpp>

namespace {

template <typename Context>
void* allocate(std::size_t size, Context& context) {
  using namespace boost::asio;
  return asio_handler_allocate(size, std::addressof(context));
}

template <typename Context>
void deallocate(void* pointer, std::size_t size, Context& context) {
  using namespace boost::asio;
  asio_handler_deallocate(pointer, size, std::addressof(context));
}

template <typename Function, typename Context>
void invoke(Function&& function, Context& context) {
  using namespace boost::asio;
  asio_handler_invoke(std::forward<Function>(function), std::addressof(context));
}

template <std::size_t alloc_size>
class handler_allocator {
private:
  handler_allocator(const handler_allocator&) = delete;
  handler_allocator& operator=(const handler_allocator&) = delete;

public:
  handler_allocator() : in_use_(false) {}
  ~handler_allocator() = default;

  bool owns(void* p) const {
    return std::addressof(storage_) == p;
  }

  void* allocate(std::size_t size) {
    if (in_use_ || size > alloc_size) {
      return 0;
    }
    in_use_ = true;
    return std::addressof(storage_);
  }

  void deallocate(void* p) {
    if (p) {
      in_use_ = false;
    }
  }

private:
  typename std::aligned_storage<alloc_size>::type storage_;
  bool in_use_;
};

template <typename Allocator, typename Handler>
class custom_alloc_handler {
private:
  typedef custom_alloc_handler<Allocator, Handler> this_type;

public:
  typedef void result_type;

  template <typename H>
  custom_alloc_handler(Allocator& allocator, H&& handler):
      allocator_(std::addressof(allocator)), handler_(std::forward<H>(handler)) {}

  friend void* asio_handler_allocate(std::size_t size, this_type* context) {
    if (void* p = context->allocator_->allocate(size)) {
      return p;
    }
    return allocate(size, context->handler_);
  }

  friend void asio_handler_deallocate(void* pointer, std::size_t size, this_type* context) {
    if (context->allocator_->owns(pointer)) {
      context->allocator_->deallocate(pointer);
    } else {
      deallocate(pointer, size, context->handler_);
    }
  }

  template <typename Function>
  friend void asio_handler_invoke(Function&& function, this_type* context) {
    invoke(std::forward<Function>(function), context->handler_);
  }

  template <typename Function>
  friend void asio_handler_invoke(Function& function, this_type* context) {
    invoke(function, context->handler_);
  }

  template <typename Function>
  friend void asio_handler_invoke(const Function& function, this_type* context) {
    invoke(function, context->handler_);
  }

  template <typename... Arg>
  void operator()(Arg&&... arg) {
    handler_(std::forward<Arg>(arg)...);
  }

  template <typename... Arg>
  void operator()(Arg&&... arg) const {
    handler_(std::forward<Arg>(arg)...);
  }

private:
  Allocator* allocator_;
  Handler handler_;
};

template <typename Allocator, typename Handler>
custom_alloc_handler<Allocator, typename std::decay<Handler>::type>
make_custom_alloc_handler(Allocator& allocator, Handler&& handler) {
  typedef typename std::decay<Handler>::type handler_type;
  return custom_alloc_handler<Allocator, handler_type>(allocator, std::forward<Handler>(handler));
}

const std::size_t clients_max = 10000;
const std::size_t bear_after_mcs = 1000;
const std::size_t send_each_mcs = 10000;
const std::size_t client_timeout_mcs = 30 * 1000 * 1000;
const std::size_t buffer_size = 32;

std::size_t thread_num;

std::vector<std::unique_ptr<boost::asio::io_service>> services;
std::atomic_size_t services_index(0);
boost::asio::ip::tcp::resolver::iterator connect_to;

std::atomic_bool stopped(false);
std::atomic_size_t children(0);
std::atomic_size_t connection_active(0);
std::atomic_size_t connection_errors(0);
std::atomic_size_t exchange_errors(0);
std::atomic_size_t connection_sum_mcs(0);
std::atomic_size_t latency_sum_mcs(0);
std::atomic_size_t msgs(0);

class timer {
public:
  timer() {
    reset();
  }

  void start() {
    start_time_ = std::chrono::steady_clock::now();
  }

  std::size_t current() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_).count();
  }

  void reset() {
    start();
  }

private:
  std::chrono::steady_clock::time_point start_time_;
};

class connection_handler : public std::enable_shared_from_this<connection_handler> {
public:
  explicit connection_handler(const std::mt19937& rand_engine) :
    service_id_(++services_index),
    my_service_(*services[service_id_ % thread_num]),
    socket_(my_service_),
    bear_timer_(my_service_, boost::posix_time::microseconds(bear_after_mcs)),
    exchange_timer_(my_service_),
    errored_(false),
    write_in_progress_(false),
    start_write_when_write_completes_(false),
    exchange_timer_wait_in_progress_(false),
    start_exchange_timer_wait_when_wait_completes_(false),
    rand_engine_(rand_engine),
    data_rand_(0, RAND_MAX),
    exchange_rand_(0, send_each_mcs) {
    std::fill(write_buf_.begin(), write_buf_.end() - 1, data_rand_(rand_engine_));
    *(write_buf_.rbegin()) = '\n';
  }

  void start() {
    if (service_id_ >= clients_max) {
      return;
    }
    bear_timer_.async_wait(make_custom_alloc_handler(bear_timer_allocator_,
        std::bind(&connection_handler::handle_bear_timer, shared_from_this(),
            std::placeholders::_1)));
    connect_timer_.reset();
    ++connection_active;
    boost::asio::async_connect(socket_, connect_to, make_custom_alloc_handler(
        write_allocator_, std::bind(&connection_handler::handle_connect,
            shared_from_this(), std::placeholders::_1)));
  }

private:
  void handle_bear_timer(const boost::system::error_code& e) {
    if (stopped) {
      return;
    }
    if (!e) {
      std::make_shared<connection_handler>(rand_engine_)->start();
    }
  }

  void handle_connect(const boost::system::error_code& e) {
    if (stopped) {
      return;
    }
    ++children;
    --connection_active;
    if (e) {
      errored_ = true;
      ++connection_errors;
      return;
    }
    std::size_t spent_mcs = connect_timer_.current();
    if (spent_mcs > client_timeout_mcs) {
      errored_ = true;
      ++connection_errors;
      stop_operations();
      return;
    }
    connection_sum_mcs += spent_mcs;
    socket_.set_option(boost::asio::ip::tcp::no_delay(true));
    socket_.set_option(boost::asio::socket_base::linger(true, 0));
    start_read();
    start_write();
  }

  void start_write() {
    if (write_in_progress_) {
      start_write_when_write_completes_ = true;
      return;
    }
    exchange_timer_.expires_from_now(boost::posix_time::microseconds(
        exchange_rand_(rand_engine_)));
    start_exchange_timer_wait();
    timer_queue_.push(timer());
    boost::asio::async_write(socket_, boost::asio::buffer(write_buf_),
        make_custom_alloc_handler(write_allocator_, std::bind(
            &connection_handler::handle_write, shared_from_this(),
            std::placeholders::_1)));
    write_in_progress_ = true;
  }

  void handle_write(const boost::system::error_code& e) {
    write_in_progress_ = false;
    if (stopped) {
      return;
    }
    if (errored_) {
      return;
    }
    if (e) {
      errored_ = true;
      ++exchange_errors;
      stop_operations();
      return;
    }
    if (start_write_when_write_completes_) {
      start_write_when_write_completes_ = false;
      start_write();
    }
  }

  void handle_exchange_timer(const boost::system::error_code& e) {
    exchange_timer_wait_in_progress_ = false;
    if (stopped) {
      return;
    }
    if (errored_) {
      return;
    }
    if (e && e != boost::asio::error::operation_aborted) {
      errored_ = true;
      stop_operations();
      return;
    }
    if (start_exchange_timer_wait_when_wait_completes_) {
      start_exchange_timer_wait_when_wait_completes_ = false;
      start_exchange_timer_wait();
      return;
    }
    if (e != boost::asio::error::operation_aborted) {
      start_write();
    }
  }

  void start_exchange_timer_wait() {
    if (exchange_timer_wait_in_progress_) {
      start_exchange_timer_wait_when_wait_completes_ = true;
      return;
    }
    exchange_timer_.async_wait(make_custom_alloc_handler(exchange_timer_allocator_,
        std::bind(&connection_handler::handle_exchange_timer, shared_from_this(),
            std::placeholders::_1)));
    exchange_timer_wait_in_progress_ = true;
  }

  void start_read() {
    std::fill(read_buf_.begin(), read_buf_.end(), 0);
    boost::asio::async_read(socket_, boost::asio::buffer(read_buf_),
        make_custom_alloc_handler(read_allocator_, std::bind(
            &connection_handler::handle_read, shared_from_this(),
            std::placeholders::_1, std::placeholders::_2)));
  }

  void handle_read(const boost::system::error_code& e, std::size_t transferred) {
    if (stopped) {
      return;
    }
    if (errored_) {
      return;
    }
    if (e || transferred != 32 || !std::equal(
        write_buf_.begin(), write_buf_.end(),
        read_buf_.begin(), read_buf_.end())) {
      errored_ = true;
      ++exchange_errors;
      stop_operations();
      return;
    }
    std::size_t spent_mcs = timer_queue_.front().current();
    timer_queue_.pop();
    if (spent_mcs > client_timeout_mcs) {
      errored_ = true;
      ++exchange_errors;
      stop_operations();
      return;
    }
    ++msgs;
    latency_sum_mcs += spent_mcs;
    start_read();
  }

  void stop_operations() {
    boost::system::error_code ignored;
    socket_.close(ignored);
    exchange_timer_.cancel(ignored);
  }

  std::size_t service_id_;
  boost::asio::io_service& my_service_;
  boost::asio::ip::tcp::socket socket_;
  boost::asio::deadline_timer bear_timer_;
  boost::asio::deadline_timer exchange_timer_;
  std::array<char, buffer_size> write_buf_;
  std::array<char, buffer_size> read_buf_;
  timer connect_timer_;
  bool errored_;
  bool write_in_progress_;
  bool start_write_when_write_completes_;
  bool exchange_timer_wait_in_progress_;
  bool start_exchange_timer_wait_when_wait_completes_;
  std::queue<timer> timer_queue_;
  std::mt19937 rand_engine_;
  std::uniform_int_distribution<int> data_rand_;
  std::uniform_int_distribution<int> exchange_rand_;
  handler_allocator<256> bear_timer_allocator_;
  handler_allocator<256> write_allocator_;
  handler_allocator<256> read_allocator_;
  handler_allocator<256> exchange_timer_allocator_;
};

void print_stat(bool final = false) {
  if (final) {
    std::cout << "\nFinal statistics:\n";
  }
  std::size_t conn_avg = connection_sum_mcs / (std::max)((std::size_t) children - connection_errors, (std::size_t) 1);
  std::size_t lat_avg = latency_sum_mcs / (std::max)((std::size_t) msgs, (std::size_t) 1);
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
  thread_num = static_cast<std::size_t>(args > 3 ? std::atoi(argv[3]) : 24);
  std::random_device random_device;
  std::mt19937 rand_engine(random_device());
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
  std::make_shared<connection_handler>(rand_engine)->start();
  for (std::size_t i = 0; i < 60; i += 5) {
    print_stat();
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  stopped = true;
  std::for_each(services.begin(), services.end(), [](std::unique_ptr<boost::asio::io_service>& s) {
    s->stop();
  });
  std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
  print_stat(true);
  return EXIT_SUCCESS;
}
