<?php

error_reporting(-1);

$ssl = empty($_SERVER['argv'][1]) ? 0 : 1;

$errno = null;
$errstr = null;

if ($ssl) {
    $sock = @stream_socket_client('async-tls://127.0.0.1:10008', $errno, $errstr, 1, STREAM_CLIENT_CONNECT, stream_context_create([
        'ssl' => [
            'peer_name' => 'localhost',
            'allow_self_signed' => true,
            'alpn_protocols' => 'test/1.1,test/1.0'
        ]
    ]));
} else {
    $sock = @stream_socket_client('async-tcp://127.0.0.1:10008', $errno, $errstr, 1, STREAM_CLIENT_CONNECT);
}

if ($sock === false) {
    var_dump('Failed to establish connection', $errno, $errstr);

    exit();
}

print_r(stream_get_meta_data($sock));

var_dump(stream_socket_get_name($sock, true));

var_dump(fwrite($sock, 'Hello World :)'));
var_dump(stream_socket_shutdown($sock, STREAM_SHUT_WR));
var_dump(fread($sock, 8192));

fclose($sock);
