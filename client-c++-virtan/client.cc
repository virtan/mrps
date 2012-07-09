#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <boost/thread.hpp>
#include <boost/asio.hpp>

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;

const size_t clients_max = 10000;
const size_t bear_after_mcs = 1000;
const size_t send_each_mcs = 10000;
const size_t client_timeout_mcs = 30*1000*1000;

size_t threads_;
thread *vthreads;
io_service *services;
size_t services_i = 0;
tcp::resolver::iterator connect_to;

volatile size_t children = 0;
volatile size_t connection_active = 0;
volatile size_t connection_errors = 0;
volatile size_t exchange_errors = 0;
volatile size_t connection_summ_mcs = 0;
volatile size_t latency_summ_mcs = 0;
volatile size_t msgs = 0;

class timer {
    public:
        timer() { reset(); }

        void start() { gettimeofday(&start_time, NULL); }

        size_t current() {
            struct timeval now;
            gettimeofday(&now, NULL);
            size_t diff = (((size_t) now.tv_sec) * 1000000) + now.tv_usec - (((size_t) start_time.tv_sec) * 1000000) - start_time.tv_usec;
            return diff;
        }

        void reset() {
            start();
        }

    private:
        struct timeval start_time;
};

void servicing(size_t i) {
    io_service::work worker(services[i]);
    services[i].run();
}

struct connection_handler {
    connection_handler() :
        service_id(__sync_fetch_and_add(&services_i, 1)),
        my_service(services[service_id % threads_]),
        s(my_service),
        bear_tmr(my_service, posix_time::microseconds(bear_after_mcs)),
        exch_tmr(my_service),
        errored(false)
    {
        if(service_id >= clients_max) return;
        bear_tmr.async_wait(bind(&connection_handler::new_connection_handler, this, asio::placeholders::error));
        connect_timer.reset();
        __sync_fetch_and_add(&connection_active, 1);
        async_connect(s, connect_to, bind(&connection_handler::connect, this, asio::placeholders::error));
    }

    void new_connection_handler(const system::error_code &e) {
        // cout << "new connection\n";
        assert(!e);
        new connection_handler;
    }

    void connect(const system::error_code &e) {
        // cout << "connect_staff\n";
        __sync_fetch_and_add(&children, 1);
        __sync_fetch_and_sub(&connection_active, 1);
        if(e) {
            errored = true;
            __sync_fetch_and_add(&connection_errors, 1);
            return;
        }
        size_t spent_mcs = connect_timer.current();
        if(spent_mcs > client_timeout_mcs) {
            errored = true;
            __sync_fetch_and_add(&connection_errors, 1);
            return;
        }
        __sync_fetch_and_add(&connection_summ_mcs, spent_mcs);
        memset(send_buf, ' ', 32);
        snprintf(send_buf, 32, "%d", rand());
        send_buf[31] = '\n';
        send_buf[32] = 0;
        s.set_option(tcp::no_delay(true));
        //typedef boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDBUF> snd_buf;
        //typedef boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVBUF> rcv_buf;
        //s.set_option(snd_buf(18350));
        //s.set_option(rcv_buf(18350));
        read_staff(system::error_code());
        send_staff(system::error_code());
    }

    void exchange(const system::error_code &e) {
        // cout << "exchange_staff\n";
        assert(!e);
    }

    void send_staff(const system::error_code &e) {
        // cout << "send_staff\n";
        assert(!e);
        if(errored) return;
        exch_tmr.expires_from_now(posix_time::microseconds(rand() % (2 * send_each_mcs)));
        exch_tmr.async_wait(bind(&connection_handler::send_staff, this, asio::placeholders::error));
        tqueue.push(timer());
        async_write(s, mutable_buffers_1(send_buf, 32), bind(&connection_handler::stub, this, asio::placeholders::error));
    }

    void stub(const system::error_code &e) {
        // cout << "stub\n";
        if(errored) return;
        if(e) {
            // cerr << "send error\n";
            errored = true;
            __sync_fetch_and_add(&exchange_errors, 1);
            return;
        }
    }

    void read_staff(const system::error_code &e) {
        // cout << "read_staff\n";
        if(errored) return;
        if(e) {
            errored = true;
            __sync_fetch_and_add(&exchange_errors, 1);
            return;
        }
        memset(read_buf, 0, 33);
        async_read(s, mutable_buffers_1(read_buf, 32), bind(&connection_handler::compare_staff, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
    }

    void compare_staff(const system::error_code &e, size_t transferred) {
        // cout << "compare_staff\n";
        if(errored) return;
        if(e || transferred != 32 || strncmp(send_buf, read_buf, 32)) {
            //if(strncmp(send_buf, read_buf, 32)) cerr << transferred << " \"" << send_buf << "\" != \"" << read_buf << "\"" << endl;
            errored = true;
            __sync_fetch_and_add(&exchange_errors, 1);
            return;
        }
        size_t spent_mcs = tqueue.front().current();
        tqueue.pop();
        if(spent_mcs > client_timeout_mcs) {
            errored = true;
            __sync_fetch_and_add(&exchange_errors, 1);
            return;
        }
        __sync_fetch_and_add(&msgs, 1);
        __sync_fetch_and_add(&latency_summ_mcs, spent_mcs);
        read_staff(e);
    }

    size_t service_id;
    io_service &my_service;
    tcp::socket s;
    deadline_timer bear_tmr;
    deadline_timer exch_tmr;
    char send_buf[33];
    char read_buf[33];
    timer connect_timer;
    timer exchange_timer;
    bool errored;
    queue<timer> tqueue;
};

void print_stat(bool final = false) {
    if(final) cout << "\nFinal statistics:\n";
    size_t conn_avg = connection_summ_mcs / max((size_t) children - connection_errors, (size_t) 1);
    size_t lat_avg = latency_summ_mcs / max((size_t) msgs, (size_t) 1);
    cout << "Chld: " << children << " ConnErr: " << (final ? connection_errors + connection_active : connection_errors) << " ExchErr: " << exchange_errors << " ConnAvg: " << (((double) conn_avg) / 1000) << "ms LatAvg: " << (((double) lat_avg) / 1000) << "ms  Msgs: " << msgs << "\n";
}

int main(int args, char **argv) {
    if(args < 2) {
        cout << "Usage: " << argv[0] << " <host> [port = 32000 [threads = 24]]" << endl;
        return 1;
    }
    string host(argv[1]);
    string port(args > 2 ? argv[2] : "32000");
    string threads(args > 3 ? argv[3] : "24");
    threads_ = atoi(threads.c_str());
    vthreads = new thread[threads_];
    services = new io_service[threads_];
    for(size_t i = 0; i < threads_; ++i) vthreads[i] = thread(servicing, i);
    sleep(1);
    cout << "Starting tests" << endl;
    tcp::resolver resolver(services[0]);
    tcp::resolver::query query(host, port);
    connect_to = resolver.resolve(query);
    new connection_handler;
    for(size_t i = 0; i < 60; i += 5) {
        print_stat();
        sleep(5);
    }
    print_stat(true);
    return 0;
}
