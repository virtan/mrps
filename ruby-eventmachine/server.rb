#!/usr/bin/env ruby

require 'rubygems'
require 'eventmachine'

module EchoServer
  def receive_data data
    send_data data
  end
end

EventMachine::run {
  EventMachine::start_server "127.0.0.1", 9000, EchoServer
  puts 'running echo server on port 9000'
}