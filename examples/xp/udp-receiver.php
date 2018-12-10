<?php

error_reporting(-1);

$errno = null;
$errstr = null;

$sock = stream_socket_server('udp://127.0.0.1:10007', $errno, $errstr, STREAM_SERVER_BIND);

print_r(stream_get_meta_data($sock));
var_dump(stream_socket_get_name($sock, false));

$peer = null;
var_dump(stream_socket_recvfrom($sock, 0xFFFF, 0, $peer));
var_dump($peer);

fclose($sock);
