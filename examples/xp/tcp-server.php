<?php

error_reporting(-1);

$ssl = empty($_SERVER['argv'][1]) ? 0 : 1;

$errno = null;
$errstr = null;

if ($ssl) {
    $server = stream_socket_server('async-tcp://127.0.0.1:10008', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, stream_context_create([
        'ssl' => [
            'local_cert' => __DIR__ . '/../cert/localhost.pem',
            'passphrase' => 'localhost',
            'alpn_protocols' => 'foo/bar,test/1.0'
        ]
    ]));
} else {
    $server = @stream_socket_server('async-tcp://127.0.0.1:10008', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, stream_context_create([
        'socket' => [
            'so_reuseport' => true
        ]
    ]));
}

if ($server === false) {
    var_dump('Failed to create server', $errno, $errstr);

    exit();
}

while (true) {
    echo "AWAIT CONNECTION\n\n";

    $sock = stream_socket_accept($server);
    var_dump($sock);
    if ($ssl && !stream_socket_enable_crypto($sock, true, STREAM_CRYPTO_METHOD_TLSv1_2_SERVER)) {
        var_dump('Failed to enable encryption');
        
        fclose($sock);
        continue;
    }
    
    print_r(stream_get_meta_data($sock));
    
    var_dump($m = fread($sock, 8192));
    var_dump(fwrite($sock, 'ECHO ' . strtoupper($m)));
    fclose($sock);
}

fclose($server);
