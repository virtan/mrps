#!/usr/bin/env escript
%% -*- erlang -*-
%%! -smp enable +S 24 +K true

-mode(native).
-export([main/1, starter/4, connector/3]).


-define(TIMEOUT, 30000).
-define(BEAR_AFTER, 1).
-define(SEND_EACH, 10).
-define(LOG_STAMP, 5000).
-define(TEST_TIME, 60000).
-define(MAX_CHILDREN, 10000).

main([Host, Port]) ->
    {ok, Address} = inet:getaddr(Host, inet),
    spawn(?MODULE, starter, [self(), Address, list_to_integer(Port), 0]),
    timer:send_after(?TEST_TIME, timeout),
    timer:send_after(?LOG_STAMP, log_stamp),
    io:format("Test started~n", []),
    gather_log(0, 0, 0, 0, 0, 0);
main(_) ->
    io:format("Usage: ./client.erl <host> <port>~n", []).

gather_log(Chld, ConErr, ExchErr, ConSum, LatSum, Msgs) ->
    receive
        timeout -> print_log(final, Chld, ConErr, ExchErr, ConSum / max(Chld - ConErr, 1), LatSum / max(Msgs, 1), Msgs);
        log_stamp -> 
            timer:send_after(?LOG_STAMP, log_stamp),
            print_log(Chld, ConErr, ExchErr, ConSum / max(Chld - ConErr, 1), LatSum / max(Msgs, 1), Msgs),
            gather_log(Chld, ConErr, ExchErr, ConSum, LatSum, Msgs);
        {connect_time, TimeMcs} -> gather_log(Chld + 1, ConErr, ExchErr, ConSum + TimeMcs, LatSum, Msgs);
        connect_error -> gather_log(Chld + 1, ConErr + 1, ExchErr, ConSum, LatSum, Msgs);
        {exchange_time, TimeMcs} -> gather_log(Chld, ConErr, ExchErr, ConSum, LatSum + TimeMcs, Msgs + 1);
        exchange_error -> gather_log(Chld, ConErr, ExchErr + 1, ConSum, LatSum, Msgs)
    end.

starter(_, _, _, ?MAX_CHILDREN) -> done;
starter(Logger, Host, Port, Running) ->
    timer:send_after(?BEAR_AFTER, go_on),
    spawn(?MODULE, connector, [Logger, Host, Port]),
    receive go_on -> going_on end,
    starter(Logger, Host, Port, Running + 1).

connector(Logger, Host, Port) ->
    StartTime = tstamp(),
    case gen_tcp:connect(Host, Port,
            [binary, {packet, 0}, {active, true}, {reuseaddr, true}], ?TIMEOUT) of
        {ok, Socket} -> 
            Logger ! {connect_time, tstamp() - StartTime},
            sporadic_exchange(Logger, Socket);
        {error, _} -> Logger ! connect_error
    end.

sporadic_exchange(Logger, Socket) ->
    timer:send_after(random:uniform(?SEND_EACH * 2), go_on),
    receive go_on -> going_on end,
    String = random_string(),
    gen_tcp:send(Socket, String),
    SentTime = tstamp(),
    receive
        close -> done;
        {tcp, Socket, String} ->
            Logger ! {exchange_time, tstamp() - SentTime},
            sporadic_exchange(Logger, Socket);
        {tcp, Socket, _} -> Logger ! exchange_error;
        {tcp_closed, Socket} -> Logger ! exchange_error;
        {tcp_error, Socket, _} -> Logger ! exchange_error
    after ?TIMEOUT -> Logger ! exchange_error
    end,
    gen_tcp:close(Socket).

random_string() -> list_to_binary(float_to_list(random:uniform())).

tstamp() ->
    {Mega, Secs, Micro} = now(),
    Mega * 1000000000000 + Secs * 1000000 + Micro.

print_log(final, Chld, ConErr, ExchErr, ConnAvg, LatAvg, Msgs) ->
    io:format("~nFinal statistics:~n", []),
    print_log(Chld, ConErr, ExchErr, ConnAvg, LatAvg, Msgs).

print_log(Chld, ConErr, ExchErr, ConnAvg, LatAvg, Msgs) ->
    io:format("Chld: ~p ConnErr: ~p ExchErr: ~p ConnAvg: ~pms LatAvg: ~pms  Msgs: ~p~n", [Chld, ConErr, ExchErr, round(ConnAvg) / 1000, round(LatAvg) / 1000, Msgs]).
