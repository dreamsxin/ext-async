<?php

error_reporting(- 1);

$ssl = empty($_SERVER['argv'][1]) ? 0 : 1;

$context = stream_context_create([
    'ssl' => [
        'verify_depth' => 6
    ]
]);

$url = $ssl ? 'tls://httpbin.org:443' : 'tcp://httpbin.org:80';

$errno = null;
$errstr = null;

$sock = stream_socket_client($url, $errno, $errstr, 1, STREAM_CLIENT_CONNECT, $context);

stream_set_blocking($sock, true);
print_r(stream_get_meta_data($sock));

var_dump(stream_socket_get_name($sock, true));

fwrite($sock, "GET /json HTTP/1.0\r\nHost: httpbin.org\r\n\r\n");

echo "\n";
fpassthru($sock);

fclose($sock);
