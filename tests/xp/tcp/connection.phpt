--TEST--
XP socket TCP connection.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$errno = null;
$errstr = null;

$server = stream_socket_server('async-tcp://127.0.0.1:10009', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN);

Task::async(function () {
    $errno = null;
    $errstr = null;

    $socket = stream_socket_client('async-tcp://127.0.0.1:10009', $errno, $errstr, 1, STREAM_CLIENT_CONNECT);
    
    try {
        var_dump(stream_socket_get_name($socket, true));
    
        fwrite($socket, "Hello");
        stream_socket_shutdown($socket, STREAM_SHUT_WR);
        
        var_dump(fgets($socket));
    } finally {
        fclose($socket);
    }
});

try {
    var_dump(stream_socket_get_name($server, false));

    $socket = stream_socket_accept($server);
} finally {
    fclose($server);
}

try {
    var_dump(stream_socket_get_name($socket, false));
    
    var_dump(feof($socket));
    var_dump(trim(fgets($socket)));
    
    fwrite($socket, 'World');
} finally {
    fclose($socket);
}

--EXPECT--
string(15) "127.0.0.1:10009"
string(15) "127.0.0.1:10009"
bool(false)
string(15) "127.0.0.1:10009"
string(5) "Hello"
string(5) "World"
