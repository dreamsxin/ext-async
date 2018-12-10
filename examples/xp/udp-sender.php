<?php

error_reporting(-1);

$errno = null;
$errstr = null;

$sock = stream_socket_server('udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);

print_r(stream_get_meta_data($sock));
var_dump(stream_socket_get_name($sock, false));

stream_socket_sendto($sock, 'HELLO WORLD :)', 0, '127.0.0.1:10007');

fclose($sock);
