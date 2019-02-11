<?php

error_reporting(-1);

$ssl = empty($_SERVER['argv'][1]) ? 0 : 1;
$url = $ssl ? 'async-tls://httpbin.org:443' : 'async-tcp://httpbin.org:80';

$errno = null;
$errstr = null;

if ($ssl) {
    $sock = @stream_socket_client($url, $errno, $errstr, 2, STREAM_CLIENT_CONNECT, stream_context_create([
        'ssl' => [
            'alpn_protocols' => 'foo/bar,http/1.1'
        ]
    ]));
} else {
    $sock = @stream_socket_client($url, $errno, $errstr, 2, STREAM_CLIENT_CONNECT);
}

if ($sock === false) {
    var_dump('Failed to establish connection!', $errno, $errstr);
    exit();
}

stream_set_blocking($sock, true);
var_dump(stream_set_timeout($sock, 1, 0));

print_r(stream_get_meta_data($sock));
var_dump(stream_socket_get_name($sock, true));

fwrite($sock, "GET /json HTTP/1.0\r\nHost: httpbin.org\r\n\r\n");

echo "\n";
fpassthru($sock);

if (stream_get_meta_data($sock)['timed_out'] ?? false) {
    var_dump('Connection timed out!');
}

fclose($sock);
