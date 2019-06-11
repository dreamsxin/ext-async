--TEST--
XP socket UDP connection.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$errno = null;
$errstr = null;

$sock = stream_socket_server('async-udp://127.0.0.1:10007', $errno, $errstr, STREAM_SERVER_BIND);

Task::async(function () {
	$errno = null;
	$errstr = null;
	
	$sock = stream_socket_server('async-udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);
	
	stream_socket_sendto($sock, 'HELLO', 0, '127.0.0.1:10007');
	
	fclose($sock);
	
	$sock = stream_socket_client('async-udp://127.0.0.1:10007', $errno, $errstr, 0);
	
	var_dump(fwrite($sock, 'WORLD :)'));
	var_dump(fread($sock, 8192));
	
	fclose($sock);
});

var_dump(stream_socket_recvfrom($sock, 0xFFFF));

$peer = null;
var_dump(stream_socket_recvfrom($sock, 0xFFFF, 0, $peer));

stream_socket_sendto($sock, 'ACK!', 0, $peer);

fclose($sock);

--EXPECT--
int(8)
string(5) "HELLO"
string(8) "WORLD :)"
string(4) "ACK!"
