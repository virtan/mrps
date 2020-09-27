#if defined(WIN32)
#include <tchar.h>
#endif

#include <cstddef>
#include <iostream>
#include <vector>
#include <memory>
#include <array>
#include <string>
#include <utility>
#include <functional>
#include <thread>
#include <type_traits>
#include <algorithm>
#include <cstdlib>
#include <boost/asio.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/program_options.hpp>

namespace asio_helpers {

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

#if BOOST_VERSION >= 105400

template <typename Context>
bool is_continuation(Context& context) {
  using namespace boost::asio;
  return asio_handler_is_continuation(std::addressof(context));
}

#else  // BOOST_VERSION >= 105400

template <typename Context>
bool is_continuation(Context& /*context*/) {
  return false;
}

#endif // BOOST_VERSION >= 105400

}

namespace {

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
    return asio_helpers::allocate(size, context->handler_);
  }

  friend void asio_handler_deallocate(void* pointer, std::size_t size, this_type* context) {
    if (context->allocator_->owns(pointer)) {
      context->allocator_->deallocate(pointer);
    } else {
      asio_helpers::deallocate(pointer, size, context->handler_);
    }
  }

  template <typename Function>
  friend void asio_handler_invoke(Function&& function, this_type* context) {
    asio_helpers::invoke(std::forward<Function>(function), context->handler_);
  }

  template <typename Function>
  friend void asio_handler_invoke(Function& function, this_type* context) {
    asio_helpers::invoke(function, context->handler_);
  }

  template <typename Function>
  friend void asio_handler_invoke(const Function& function, this_type* context) {
    asio_helpers::invoke(function, context->handler_);
  }

  friend bool asio_handler_is_continuation(this_type* context) {
    return asio_helpers::is_continuation(context->handler_);
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

class connection {
public:
  explicit connection(boost::asio::io_service& service) : socket_(service) {}
  ~connection() = default;
  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

  void start() {
    socket_.async_read_some(boost::asio::buffer(data_),
        make_custom_alloc_handler(allocator_, std::bind(&connection::read, this,
            std::placeholders::_1, std::placeholders::_2)));
  }

  boost::asio::ip::tcp::socket& socket() {
    return socket_;
  }

private:
  void read(const boost::system::error_code& e, std::size_t bytes_transferred) {
    if (e) {
      delete this;
    } else {
      boost::asio::async_write(socket_, boost::asio::buffer(data_, bytes_transferred),
          make_custom_alloc_handler(allocator_, std::bind(&connection::write, this,
              std::placeholders::_1)));
    }
  }

  void write(const boost::system::error_code& e) {
    if (e) {
      delete this;
    } else {
      start();
    }
  }

  handler_allocator<128> allocator_;
  std::array<char, 4096> data_;
  boost::asio::ip::tcp::socket socket_;
};

class acceptor {
public:
  acceptor(boost::asio::io_service& service,
      const boost::asio::ip::tcp::acceptor::native_handle_type& native_acceptor) :
      service_(service), acceptor_(service_, boost::asio::ip::tcp::v4(), native_acceptor) {
    start_accept();
  }

  ~acceptor() = default;
  acceptor(const acceptor&) = delete;
  acceptor& operator=(const acceptor&) = delete;

private:
  void start_accept() {
    auto c = new connection(service_);
    acceptor_.async_accept(c->socket(), make_custom_alloc_handler(allocator_,
        std::bind(&acceptor::accept, this, c, std::placeholders::_1)));
  }

  void accept(connection* c, const boost::system::error_code& e) {
    if (e) {
      delete c;
    } else {
      c->start();
    }
    start_accept();
  }

  boost::asio::io_service& service_;
  boost::asio::ip::tcp::acceptor acceptor_;
  handler_allocator<256> allocator_;
};

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

const char* help_option_name     = "help";
const char* host_option_name     = "host";
const char* port_option_name     = "port";
const char* threads_option_name  = "threads";

boost::program_options::options_description build_program_options_description() {
  boost::program_options::options_description description("Usage");
  description.add_options()
      (
        help_option_name,
        "produce help message"
      )
      (
        host_option_name,
        boost::program_options::value<std::string>()->default_value(
            boost::asio::ip::address_v4::any().to_string()),
        "address to listen"
      )
      (
        port_option_name,
        boost::program_options::value<unsigned short>(),
        "port to listen"
      )
      (
        threads_option_name,
        boost::program_options::value<std::size_t>()->default_value(24),
        "number of threads"
      );
  return std::move(description);
}

#if defined(WIN32)
boost::program_options::variables_map parse_program_options(
    const boost::program_options::options_description& options_description,
    int argc, _TCHAR* argv[]) {
#else
boost::program_options::variables_map parse_program_options(
    const boost::program_options::options_description& options_description,
    int argc, char* argv[]) {
#endif
  boost::program_options::variables_map values;
  boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, options_description),
      values);
  boost::program_options::notify(values);
  return std::move(values);
}

} // anonymous namespace

#if defined(WIN32)
int _tmain(int argc, _TCHAR** argv) {
#else
int main(int argc, char* argv[]) {
#endif
  try {
    auto po_description = build_program_options_description();
    auto po_values = parse_program_options(po_description, argc, argv);
    if (po_values.count(help_option_name)) {
      std::cout << po_description;
      return EXIT_SUCCESS;
    }
    if (!po_values.count(port_option_name)) {
      std::cerr << "port is required\n" << po_description;
      return EXIT_FAILURE;
    }
    auto host = po_values[host_option_name].as<std::string>();
    auto port = po_values[port_option_name].as<unsigned short>();
    auto thread_num = po_values[threads_option_name].as<std::size_t>();
    std::vector<std::thread> threads;
    threads.reserve(thread_num);
    boost::asio::io_service fake_s;
    boost::asio::ip::tcp::acceptor fake_a(fake_s, boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string(host), port));
    auto native_handle = fake_a.native_handle();
    for (std::size_t i = 0; i < thread_num; ++i) {
      threads.emplace_back([native_handle]() {
        boost::asio::io_service service(to_io_context_concurrency_hint(1));
        acceptor a(service, native_handle);
        service.run();
      });
    }
    std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    return EXIT_SUCCESS;
  }
  catch (const boost::program_options::error& e) {
    std::cerr << "Error reading options: " << e.what() << std::endl;
  }
  catch (const std::exception& e) {
    std::cerr << "Unexpected error: " << e.what() << std::endl;
  }
  catch (...) {
    std::cerr << "Unknown error" << std::endl;
  }
  return EXIT_FAILURE;
}
