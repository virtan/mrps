#include <iostream>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <boost/thread.hpp>
#include <boost/asio.hpp>

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;

struct connection {
    connection(io_service &s) :
		sock(s) {}

	void start() {
		//sock.set_option(tcp::no_delay(true));
		sock.async_read_some(buffer(data, max_length),
			bind(&connection::read, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	}

	void read(const system::error_code &e, size_t bytes_transferred) {
		if(!e) { async_write(sock, buffer(data, bytes_transferred),
			bind(&connection::write, this, asio::placeholders::error)); }
		else delete this;
	}

	void write(const system::error_code &e) {
		if(!e) start();
		else delete this;
	}

	tcp::socket sock;
	enum { max_length = 4096 };
	char data[max_length];
};

struct acceptor {
	acceptor(int native_sock) :
		a(s, tcp::v4(), native_sock) { start_accept(); }
	
	void start_accept() {
		connection *c = new connection(s);
		a.async_accept(c->sock, bind(&acceptor::accept, this, c, boost::asio::placeholders::error));
	}

	void accept(connection *c, const system::error_code &e) {
		if(!e) c->start();
		else delete c;
		start_accept();
	}

	io_service s;
	tcp::acceptor a;
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
    size_t threads = args > 2 ? atoi(argv[2]) : 24;
    thread *vthreads = new thread[threads];
	io_service fake_s;
	tcp::acceptor fake_a(fake_s, tcp::endpoint(tcp::v4(), port));
	//int defer_accept = 1;
	//setsockopt(fake_a.native_handle(), IPPROTO_TCP, TCP_DEFER_ACCEPT, &defer_accept, sizeof(defer_accept));
    for(size_t i = 0; i < threads; ++i) vthreads[i] = thread(servicing, fake_a.native_handle());
	sleep((unsigned int) 0 - 1);
    return 0;
}
