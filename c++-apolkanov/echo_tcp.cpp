// Author Alexey Polkanov. E-mail: aleksey.polkanov@gmail.com

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#include <iostream>
#include <stdio.h>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

class TestWorker
{
  public:
    TestWorker() : mStarted(false)
    {
      mEpollFd = epoll_create1(0);
      if (mEpollFd == -1) 
        throw std::runtime_error("epoll_create failed");
    }

    void addConnection(int clientSock)
    {
      epoll_event ev;
      int oldVal = fcntl(clientSock, F_GETFL, 0);
      if(fcntl(clientSock, F_SETFL, oldVal | O_NONBLOCK))
        throw std::runtime_error("fcntl failed");
      ev.events = EPOLLIN | EPOLLRDHUP| EPOLLET;
      ev.data.fd = clientSock;
      if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, clientSock, &ev) == -1) 
        throw std::runtime_error("epoll_ctl failed");
      if(!mStarted)
      {
        mProcessThread = boost::thread(boost::bind(&TestWorker::process, this));
        mStarted = true;
      }
    }

  private:
    void processConnection(int socket)
    {
      const int BUFF_SIZE = 100;
      char buffer[BUFF_SIZE];
      size_t data_size = read(socket, buffer, sizeof(buffer));		
      if (data_size <= 0)
      {
        close(socket);
      }
      else
        write(socket, buffer, data_size);       
    }

    void process()
    {
      const int MAX_EVENTS = 128;
      epoll_event events[MAX_EVENTS];
      int nfds = 0;
      for (;;) 
      {
        nfds = epoll_wait(mEpollFd, events, MAX_EVENTS, -1);
        for (int n = 0; n < nfds; ++n) 
          processConnection(events[n].data.fd);
      }
    }
  private:
    boost::thread mProcessThread;
    int mEpollFd;
    bool mStarted;
};


int main(int argc, char *argv[])
{
  if(argc < 3)
    throw std::runtime_error("Usage: echo_tcp PORT WORKERS");	
  int PORT = boost::lexical_cast<int>(argv[1]);
  int workerNumber = boost::lexical_cast<int>(argv[2]);
  TestWorker workers[workerNumber];	
  int servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int on = 1;
  if(-1 == servSock)
    throw std::runtime_error("creating socket failed");    
  setsockopt(servSock, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on));
    
  sockaddr_in addr;
  memset(&addr, 0 ,sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(PORT);
  if(-1 == bind(servSock, (sockaddr*) &addr, sizeof(addr)) )
    throw std::runtime_error("bind failed");
  const int MAX_Q_SIZE = 1000;
  if(-1 == listen(servSock, MAX_Q_SIZE))
    throw std::runtime_error("listen failed");

  uint64_t counter = 0;
  while (1) 
  {
    int clientSock = accept(servSock, NULL, NULL);
    if (clientSock == -1) 
      throw std::runtime_error("accept failed");
    workers[counter % workerNumber].addConnection(clientSock);
    counter++;
  }    
}

