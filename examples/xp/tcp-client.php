<?php

error_reporting(-1);

$errno = null;
$errstr = null;

$sock = @stream_socket_client('async-tcp://127.0.0.1:10008', $errno, $errstr, 1, STREAM_CLIENT_CONNECT);

if ($sock === false) {
    var_dump($errno, $errstr);

    exit();
}

print_r(stream_get_meta_data($sock));

var_dump(stream_socket_get_name($sock, true));

var_dump(fwrite($sock, 'Hello World :)'));
var_dump(stream_socket_shutdown($sock, STREAM_SHUT_WR));
var_dump(fread($sock, 8192));

fclose($sock);
