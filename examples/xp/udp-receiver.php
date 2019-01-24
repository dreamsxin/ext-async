<?php

error_reporting(-1);

$errno = null;
$errstr = null;

$sock = @stream_socket_server('async-udp://127.0.0.1:10007', $errno, $errstr, STREAM_SERVER_BIND);

if ($sock === false) {
    var_dump($errno, $errstr);

    exit();
}

print_r(stream_get_meta_data($sock));
var_dump(stream_socket_get_name($sock, false));

$peer = null;
var_dump(stream_socket_recvfrom($sock, 0xFFFF, 0, $peer));
var_dump($peer);

$peer = null;
var_dump(stream_socket_recvfrom($sock, 0xFFFF, 0, $peer));
var_dump($peer);

stream_socket_sendto($sock, 'ACK!', 0, $peer);

fclose($sock);
