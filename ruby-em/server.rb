require 'eventmachine'

 module EchoServer

   def receive_data data
     send_data data
     close_connection
   end

end

# Note that this will block current thread.
EventMachine.run {
  EventMachine.start_server "127.0.0.1", 9000, EchoServer
}