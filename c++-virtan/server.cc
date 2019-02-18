#include <iostream>
#include <vector>
#include <memory>
#include <utility>
#include <functional>
#include <thread>
#include <type_traits>
#include <sys/types.h>
#include <sys/wait.h>
#include <boost/asio.hpp>

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
    handler_allocator(const handler_allocator&);
    handler_allocator& operator=(const handler_allocator&);

public:
    handler_allocator(const handler_allocator&) : in_use_(false) {}

    bool owns(void* p) const {
        return std::addressof(storage_) == p;
    }

    void* allocate(std::size_t size) {
        if (in_use_ || size > alloc_size)
            return 0;
        in_use_ = true;
        return std::addressof(storage_);
    }

    void deallocate(void* p) {
        if (p)
            in_use_ = false;
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
        if (void* p = context->allocator_->allocate(size))
            return p;
        return allocate(size, context->handler_);
    }

    friend void asio_handler_deallocate(void* pointer, std::size_t size, this_type* context) {
        if (context->allocator_->owns(pointer))
            context->allocator_->deallocate(pointer);
        else
            deallocate(pointer, size, context->handler_);
    }

    template <typename Function>
    friend void asio_handler_invoke(Function&& function, this_type* context) {
        invoke(detail::forward<Function>(function), context->handler_);
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
inline custom_alloc_handler<Allocator, typename std::decay<Handler>::type>
make_custom_alloc_handler(Allocator& allocator, Handler&& handler) {
    typedef typename std::decay<Handler>::type handler_type;
    return custom_alloc_handler<Allocator, handler_type>(allocator, std::forward<Handler>(handler));
}

struct connection {
    connection(io_service &s) : sock(s), pending_ops(0) {}

    void start() {
        sock.async_read_some(boost::asio::buffer(data, max_length), make_custom_alloc_handler(allocator,
            std::bind(&connection::read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)));
    }

    void read(const boost::system::error_code &e, std::size_t bytes_transferred) {
        if (e)
            delete this;
        else
            async_write(sock, boost::asio::buffer(data, bytes_transferred), make_custom_alloc_handler(allocator,
                std::bind(&connection::write, this, boost::asio::placeholders::error));
    }

    void write(const boost::system::error_code &e) {
        if (e)
            delete this;
        else
            start();
    }

    boost::asio::ip::tcp::socket sock;
    enum { max_length = 4096 };
    char data[max_length];
    handler_allocator<128> allocator;
};

struct acceptor {
    acceptor(int native_sock) : a(s, boost::asio::ip::tcp::v4(), native_sock) {
        start_accept();
    }

    void start_accept() {
        connection *c = new connection(s);
        a.async_accept(c->sock, make_custom_alloc_handler(allocator,
            std::bind(&acceptor::accept, this, c, boost::asio::placeholders::error)));
    }

    void accept(connection *c, const system::error_code &e) {
        if (e)
            delete c;
        else
            c->start();
        start_accept();
    }

    boost::asio::io_service s;
    boost::asio::ip::tcp::acceptor a;
    handler_allocator<256> allocator;
};

void servicing(unsigned short port) {
    acceptor accpt(port);
    //io_service::work worker(accpt.s);
    accpt.s.run();
}

int main(int args, char **argv) {
    if(args < 2) {
        cout << "Usage: " << argv[0] << " <port> [threads = 24]" << endl;
        return 1;
    }
    unsigned short port = atoi(argv[1]);
    std::size_t threads = args > 2 ? atoi(argv[2]) : 24;
    std::vector<std::thread> vthreads;
    vthreads.reserve(threads)
    boost::asio::io_service fake_s;
    boost::asio::ip::tcp::acceptor fake_a(fake_s, boost::asio::ip::tcp::endpoint(
        boost::asio::ip::tcp::v4(), port));
    //int defer_accept = 1;
    //setsockopt(fake_a.native_handle(), IPPROTO_TCP, TCP_DEFER_ACCEPT, &defer_accept, sizeof(defer_accept));
    for (std::size_t i = 0; i < threads; ++i)
        vthreads.emplace_back(std::bind(servicing, fake_a.native_handle()));
    for (std::size_t i = 0; i < threads; ++i)
        vthreads[i].join();
    return 0;
}
