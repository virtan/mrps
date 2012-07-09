#!/usr/bin/env escript
%% -*- erlang -*-
%%! -smp enable +K true +S 24

-mode(native).
-export([main/1]).

-define(TIMEOUT, 30000).

main([Port, Backlog]) ->
        {ok, Listen} = gen_tcp:listen(list_to_integer(Port), [binary, {packet, 0}, {active, false},
                {reuseaddr, true}, {backlog, list_to_integer(Backlog)}]),
        tcp_acceptor(Listen);
main(_) -> io:format("Usage: ./server.erl <port> <backlog>~n", []).

tcp_acceptor(Listen) ->
    case gen_tcp:accept(Listen) of
        {ok, Sock} ->
            Pid = spawn(fun() -> bind(Sock) end),
            gen_tcp:controlling_process(Sock, Pid),
            Pid ! perm,
            tcp_acceptor(Listen);
        {error, econnaborted} -> tcp_acceptor(Listen);
        _ -> gen_tcp:close(Listen)
    end.


bind(Sock) ->
    receive perm -> inet:setopts(Sock, [binary, {packet, 0}, {active, true}]), handle(Sock)
    after ?TIMEOUT -> error
    end.

handle(Sock) ->
    receive {tcp, Sock, Data} -> gen_tcp:send(Sock, Data), handle(Sock);
        _ -> error
    after ?TIMEOUT -> error
    end,
    gen_tcp:close(Sock).
