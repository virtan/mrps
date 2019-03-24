#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <thread>
#include <type_traits>
#include <chrono>
#include <memory>
#include <array>
#include <cstdlib>
#include <random>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/numeric/conversion/cast.hpp>

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
public:
  handler_allocator() : in_use_(false) {}
  ~handler_allocator() = default;
  handler_allocator(const handler_allocator&) = delete;
  handler_allocator& operator=(const handler_allocator&) = delete;

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

std::atomic_bool   stopped(false);
std::atomic_size_t connection_index(0);
std::atomic_size_t child_num(0);
std::atomic_size_t active_connection_num(0);
std::atomic_size_t connection_error_num(0);
std::atomic_size_t exchange_error_num(0);
std::atomic_size_t connection_sum_mcs(0);
std::atomic_size_t latency_sum_mcs(0);
std::atomic_size_t msg_num(0);

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
    return boost::numeric_cast<std::size_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now - start_time_).count());
  }

  void reset() {
    start();
  }

private:
  std::chrono::steady_clock::time_point start_time_;
};

struct connection_data_storage {
  connection_data_storage() = default;
  ~connection_data_storage() = default;
  connection_data_storage(const connection_data_storage&) = delete;
  connection_data_storage& operator=(const connection_data_storage&) = delete;

  handler_allocator<256> bear_timer_allocator;
  handler_allocator<256> write_allocator;
  handler_allocator<256> read_allocator;
  handler_allocator<256> exchange_timer_allocator;
  std::array<char, buffer_size> write_buf;
  std::array<char, buffer_size> read_buf;
};

class connection;
typedef std::vector<std::unique_ptr<connection>> connection_vector;

class connection {
public:
  connection(boost::asio::io_service& service,
      connection_data_storage& connection_data,
      const std::mt19937& rand_engine) :
    my_service_(service),
    socket_(my_service_),
    bear_timer_(my_service_),
    exchange_timer_(my_service_),
    errored_(false),
    write_in_progress_(false),
    start_write_when_write_completes_(false),
    exchange_timer_wait_in_progress_(false),
    start_exchange_timer_wait_when_wait_completes_(false),
    rand_engine_(rand_engine),
    data_rand_(0, RAND_MAX),
    exchange_rand_(0, send_each_mcs),
    storage_(connection_data) {
    std::fill(storage_.write_buf.begin(), storage_.write_buf.end() - 1, data_rand_(rand_engine_));
    *(storage_.write_buf.rbegin()) = '\n';
    timer_queue_.reserve(16);
  }

  void async_start(const boost::asio::ip::tcp::resolver::iterator& connect_to, 
      const connection_vector& connections) {
    my_service_.post(make_custom_alloc_handler(storage_.bear_timer_allocator,
        std::bind(&connection::start, this, connect_to, std::cref(connections))));
  }

private:
  void start(const boost::asio::ip::tcp::resolver::iterator& connect_to,
      const connection_vector& connections) {
    bear_timer_.expires_from_now(boost::posix_time::microseconds(bear_after_mcs));
    bear_timer_.async_wait(make_custom_alloc_handler(storage_.bear_timer_allocator,
        std::bind(&connection::handle_bear_timer, this, connect_to,
            std::cref(connections), std::placeholders::_1)));
    connect_timer_.reset();
    ++active_connection_num;
    boost::asio::async_connect(socket_, connect_to, make_custom_alloc_handler(
        storage_.write_allocator, std::bind(&connection::handle_connect,
            this, std::placeholders::_1)));
  }

  void handle_bear_timer(const boost::asio::ip::tcp::resolver::iterator& connect_to,
      const connection_vector& connections, const boost::system::error_code& e) {
    if (stopped) {
      return;
    }
    if (e) {
      return;
    }
    if (connection_index >= clients_max) {
      return;
    }
    auto new_connection_index = connection_index.fetch_add(1);
    if (new_connection_index >= clients_max) {
      return;
    }
    auto& new_connection = *connections[new_connection_index];
    new_connection.async_start(connect_to, connections);
  }

  void handle_connect(const boost::system::error_code& e) {
    if (stopped) {
      return;
    }
    ++child_num;
    --active_connection_num;
    if (e) {
      errored_ = true;
      ++connection_error_num;
      stop_operations();
      return;
    }
    auto spent_mcs = connect_timer_.current();
    if (spent_mcs > client_timeout_mcs) {
      errored_ = true;
      ++connection_error_num;
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
    timer_queue_.emplace_back(timer());
    boost::asio::async_write(socket_, boost::asio::buffer(storage_.write_buf),
        make_custom_alloc_handler(storage_.write_allocator, std::bind(
            &connection::handle_write, this, std::placeholders::_1)));
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
      ++exchange_error_num;
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
    exchange_timer_.async_wait(make_custom_alloc_handler(storage_.exchange_timer_allocator,
        std::bind(&connection::handle_exchange_timer, this, std::placeholders::_1)));
    exchange_timer_wait_in_progress_ = true;
  }

  void start_read() {
    std::fill(storage_.read_buf.begin(), storage_.read_buf.end(), 0);
    boost::asio::async_read(socket_, boost::asio::buffer(storage_.read_buf),
        make_custom_alloc_handler(storage_.read_allocator, std::bind(
            &connection::handle_read, this, std::placeholders::_1, std::placeholders::_2)));
  }

  void handle_read(const boost::system::error_code& e, std::size_t transferred) {
    if (stopped) {
      return;
    }
    if (errored_) {
      return;
    }
    if (e || transferred != 32 || !std::equal(
        storage_.write_buf.begin(), storage_.write_buf.end(),
        storage_.read_buf.begin(), storage_.read_buf.end())) {
      errored_ = true;
      ++exchange_error_num;
      stop_operations();
      return;
    }
    auto spent_mcs = timer_queue_.front().current();
    timer_queue_.erase(timer_queue_.begin());
    if (spent_mcs > client_timeout_mcs) {
      errored_ = true;
      ++exchange_error_num;
      stop_operations();
      return;
    }
    ++msg_num;
    latency_sum_mcs += spent_mcs;
    start_read();
  }

  void stop_operations() {
    boost::system::error_code ignored;
    socket_.close(ignored);
    exchange_timer_.cancel(ignored);
  }

  boost::asio::io_service& my_service_;
  boost::asio::ip::tcp::socket socket_;
  boost::asio::deadline_timer bear_timer_;
  boost::asio::deadline_timer exchange_timer_;
  timer connect_timer_;
  bool errored_;
  bool write_in_progress_;
  bool start_write_when_write_completes_;
  bool exchange_timer_wait_in_progress_;
  bool start_exchange_timer_wait_when_wait_completes_;
  std::vector<timer> timer_queue_;
  std::mt19937 rand_engine_;
  std::uniform_int_distribution<int> data_rand_;
  std::uniform_int_distribution<int> exchange_rand_;
  connection_data_storage& storage_;
};

void print_stat(bool final = false) {
  if (final) {
    std::cout << "\nFinal statistics:\n";
  }
  std::size_t conn_avg = connection_sum_mcs / (std::max)(
      child_num - connection_error_num, static_cast<std::size_t>(1));
  std::size_t lat_avg = latency_sum_mcs / (std::max)(
      static_cast<std::size_t>(msg_num), static_cast<std::size_t>(1));
  std::cout << "Chld: " << child_num
      << " ConnErr: "
          << (final ? static_cast<std::size_t>(connection_error_num + active_connection_num)
              : static_cast<std::size_t>(connection_error_num))
      << " ExchErr: " << exchange_error_num
      << " ConnAvg: " << static_cast<double>(conn_avg) / 1000
      << "ms LatAvg: " << static_cast<double>(lat_avg) / 1000
      << "ms  Msgs: " << msg_num << "\n";
}

#if BOOST_VERSION >= 106600

typedef int io_context_concurrency_hint;

io_context_concurrency_hint to_io_context_concurrency_hint(std::size_t hint) {
  return 1 == hint ? BOOST_ASIO_CONCURRENCY_HINT_UNSAFE_IO
      : boost::numeric_cast<io_context_concurrency_hint>(hint);
}

#else // BOOST_VERSION >= 106600

typedef std::size_t io_context_concurrency_hint;

io_context_concurrency_hint to_io_context_concurrency_hint(std::size_t hint) {
  return hint;
}

#endif // BOOST_VERSION >= 106600

const std::size_t print_stats_interval = 5;

} // anonymous namespace

int main(int args, char** argv) {
  if (args < 2) {
    std::cerr << "Usage: " << argv[0] << " <host> [port = 32000 [threads = 24 [duration_seconds = 60]]]" << std::endl;
    return EXIT_FAILURE;
  }
  std::string host = argv[1];
  std::string port = args > 2 ? argv[2] : "32000";
  auto thread_num = boost::numeric_cast<std::size_t>(args > 3 ? std::stoi(argv[3]) : 24);
  auto duration = boost::numeric_cast<std::size_t>(args > 4 ? std::stoi(argv[4]) : 60);
  std::random_device random_device;
  std::mt19937 rand_engine(random_device());
  std::vector<std::unique_ptr<connection_data_storage>> connection_data;
  connection_data.reserve(clients_max);
  for (std::size_t i = 0; i < clients_max; ++i) {
    connection_data.emplace_back(std::make_unique<connection_data_storage>());
  }
  std::vector<std::unique_ptr<boost::asio::io_service>> services;
  services.reserve(thread_num);
  for (std::size_t i = 0; i < thread_num; ++i) {
    services.emplace_back(std::make_unique<boost::asio::io_service>(
        to_io_context_concurrency_hint(1)));
  }
  connection_vector connections;
  connections.reserve(clients_max);
  for (std::size_t i = 0; i < clients_max; ++i) {
    connections.emplace_back(std::make_unique<connection>(
        *services[i % thread_num], *connection_data[i], rand_engine));
  }
  std::vector<std::thread> threads;
  threads.reserve(thread_num);
  for (std::size_t i = 0; i < thread_num; ++i) {
    boost::asio::io_service& service = *services[i];
    threads.emplace_back([&service] {
      boost::asio::io_service::work work_guard(service);
      service.run();
    });
  }
  std::cout << "Starting tests" << std::endl;
  boost::asio::ip::tcp::resolver resolver(*services[0]);
  boost::asio::ip::tcp::resolver::query query(host, port);
  auto& connection = *connections[connection_index.fetch_add(1)];
  connection.async_start(resolver.resolve(query), connections);
  for (std::size_t i = 0; i < duration; i += print_stats_interval) {
    print_stat();
    std::this_thread::sleep_for(std::chrono::seconds(print_stats_interval));
  }
  stopped = true;
  std::for_each(services.begin(), services.end(), [](std::unique_ptr<boost::asio::io_service>& s) {
    s->stop();
  });
  std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
  print_stat(true);
  return EXIT_SUCCESS;
}
