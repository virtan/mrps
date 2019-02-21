#include <iostream>
#include <vector>
#include <memory>
#include <utility>
#include <functional>
#include <thread>
#include <type_traits>
#include <algorithm>
#include <cstdlib>
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

class connection {
private:
  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

public:
  explicit connection(boost::asio::io_service& service) : socket_(service) {}
  ~connection() = default;

  void start() {
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
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
  enum { max_length = 4096 };
  char data_[max_length];
  boost::asio::ip::tcp::socket socket_;
};

class acceptor {
private:
  acceptor(const acceptor&) = delete;
  acceptor& operator=(const acceptor&) = delete;

public:
  acceptor(boost::asio::io_service& service,
      boost::asio::ip::tcp::acceptor::native_handle_type native_acceptor) :
      service_(service), acceptor_(service_, boost::asio::ip::tcp::v4(), native_acceptor) {
    start_accept();
  }

  ~acceptor() = default;

private:
  void start_accept() {
    connection* c = new connection(service_);
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

} // anonymous namespace

int main(int args, char** argv) {
  if (args < 2) {
    std::cout << "Usage: " << argv[0] << " <port> [threads = 24]" << std::endl;
    return EXIT_FAILURE;
  }
  unsigned short port = std::atoi(argv[1]);
  std::size_t thread_num = args > 2 ? std::atoi(argv[2]) : 24;
  std::vector<std::thread> threads;
  threads.reserve(thread_num);
  boost::asio::io_service fake_s;
  boost::asio::ip::tcp::acceptor fake_a(fake_s, boost::asio::ip::tcp::endpoint(
      boost::asio::ip::tcp::v4(), port));
  auto native_handle = fake_a.native_handle();
  for (std::size_t i = 0; i < thread_num; ++i) {
    threads.emplace_back([native_handle]() {
      boost::asio::io_service service(1);
      acceptor a(service, native_handle);
      service.run();
    });
  }
  std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
  return EXIT_SUCCESS;
}
