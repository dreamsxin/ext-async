<?php

error_reporting(-1);

$errno = null;
$errstr = null;

$server = @stream_socket_server('async-tcp://127.0.0.1:10008', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN);

if ($server === false) {
    var_dump($errno, $errstr);

    exit();
}

while (true) {
    echo "AWAIT CONNECTION\n\n";

    $sock = stream_socket_accept($server);
    print_r(stream_get_meta_data($sock));
    var_dump($m = fread($sock, 8192));
    var_dump(fwrite($sock, 'ECHO ' . strtoupper($m)));
    fclose($sock);
}

fclose($server);
