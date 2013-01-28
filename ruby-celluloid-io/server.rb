require 'celluloid/io'

class EchoServer
  include Celluloid::IO

  def initialize(host, port)
    @server = TCPServer.new(host, port)
    run
  end

  def finalize
    @server.close if @server
  end

  def run
    loop { handle_connection! @server.accept }
  end

  def handle_connection(socket)
    loop { socket.write socket.readpartial(4096) }
  rescue EOFError
  end
end

EchoServer.new('127.0.0.1', 9000)