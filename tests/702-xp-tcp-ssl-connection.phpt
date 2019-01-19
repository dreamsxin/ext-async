--TEST--
XP socket encrypted TCP connection.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$ctx = stream_context_create([
    'ssl' => [
        'local_cert' => dirname(__DIR__) . '/examples/cert/localhost.crt',
        'local_pk' => dirname(__DIR__) . '/examples/cert/localhost.key',
        'passphrase' => 'localhost'
    ]
]);

$errno = null;
$errstr = null;

$server = stream_socket_server('async-tcp://localhost:10009', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $ctx);

Task::async(function () {
	$ctx = stream_context_create([
	    'ssl' => [
	        'allow_self_signed' => true
	    ]
	]);

    $errno = null;
    $errstr = null;

    $socket = stream_socket_client('async-tls://localhost:10009', $errno, $errstr, 1, STREAM_CLIENT_CONNECT, $ctx);
    
    try {
        var_dump(stream_socket_get_name($socket, true));
    
        fwrite($socket, "Hello");
        stream_socket_shutdown($socket, STREAM_SHUT_WR);
        
        var_dump(fgets($socket));
    } catch (\Throwable $e) {
        echo $e;
    } finally {
        fclose($socket);
    }
});

try {
    var_dump(stream_socket_get_name($server, false));

    $socket = stream_socket_accept($server);
} finally {
    fclose($server);
}

try {
    if (false === stream_socket_enable_crypto($socket, true, STREAM_CRYPTO_METHOD_TLSv1_2_SERVER)) {
    	throw new \Error("TLS handshake failed");
    }

    var_dump(stream_socket_get_name($socket, false));
    
    var_dump(feof($socket));
    var_dump(trim(fgets($socket)));
    
    fwrite($socket, 'World');
} finally {
    fclose($socket);
}

--EXPECT--
string(15) "127.0.0.1:10009"
string(15) "127.0.0.1:10009"
bool(false)
string(15) "127.0.0.1:10009"
string(5) "Hello"
string(5) "World"
