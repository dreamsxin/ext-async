<?php

error_reporting(-1);

$errno = null;
$errstr = null;

$sock = @stream_socket_server('async-udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);

if ($sock === false) {
    var_dump($errno, $errstr);

    exit();
}

print_r(stream_get_meta_data($sock));
var_dump(stream_socket_get_name($sock, false));

stream_socket_sendto($sock, 'HELLO', 0, '127.0.0.1:10007');

fclose($sock);

$sock = stream_socket_client('async-udp://127.0.0.1:10007', $errno, $errstr, 0);

print_r(stream_get_meta_data($sock));
var_dump(stream_socket_get_name($sock, false));

var_dump(fwrite($sock, 'WORLD :)'));
var_dump(fread($sock, 8192));

fclose($sock);
