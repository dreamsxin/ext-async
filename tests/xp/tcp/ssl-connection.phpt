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
        'local_cert' => dirname(__DIR__, 3) . '/examples/cert/localhost.pem',
        'passphrase' => 'localhost',
        'alpn_protocols' => 'bar'
    ]
]);

$errno = null;
$errstr = null;

$server = stream_socket_server('async-tcp://localhost:10009', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $ctx);

Task::async(function () {
	$ctx = stream_context_create([
	    'ssl' => [
	        'allow_self_signed' => true,
	        'alpn_protocols' => 'foo,bar'
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
    
    $meta = stream_get_meta_data($socket);

    var_dump(stream_socket_get_name($socket, false));
    
    (new Timer(100))->awaitTimeout();
    
    var_dump(feof($socket));
    var_dump(trim(fgets($socket)));
    var_dump(feof($socket));
    
    fwrite($socket, 'World');
} finally {
    fclose($socket);
}

(new Timer(100))->awaitTimeout();

var_dump($meta['stream_type']);
var_dump(isset($meta['crypto']['protocol']));
var_dump(ASYNC_SSL_ALPN_SUPPORTED ? $meta['crypto']['alpn_protocol'] : 'bar');

--EXPECT--
string(15) "127.0.0.1:10009"
string(15) "127.0.0.1:10009"
string(15) "127.0.0.1:10009"
bool(false)
string(5) "Hello"
bool(true)
string(5) "World"
string(16) "tcp_socket/async"
bool(true)
string(3) "bar"
