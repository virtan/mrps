import socket
str_to_send = "bla bla blasd;lkfja;lsdkj fa"
for _ in xrange(101):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("localhost", 8888))
    for _ in xrange(1000):
        s.send(str_to_send)
        str_received = s.recv(len(str_to_send))
        if str_received != str_to_send:
            print "wrong str: ", str_received
    s.close()

# hotshotProfilers = {}
# def hotshotit(func):
#     def wrapper(*args, **kw):
#         import hotshot
#         global hotshotProfilers
#         # prof_name = func.func_name+".prof"
#         prof_name = "wrtr"+".prof"
#         profiler = hotshotProfilers.get(prof_name)
#         if profiler is None:
#             profiler = hotshot.Profile(prof_name)
#             hotshotProfilers[prof_name] = profiler
#         return profiler.runcall(func, *args, **kw)
#     return wrapper

# srvr.listen(8888)
