import os
from tornado.netutil import TCPServer,IOLoop,IOStream
class Echo(TCPServer):
    def handle_stream(self,strm):
        strm.read_until_close(lambda data: strm.write(data))
Echo().bind(8888).start(12)
IOLoop.instance().start()
