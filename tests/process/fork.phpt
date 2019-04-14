--TEST--
Process can spawn a PHP worker process with IPC support.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

use Concurrent\Network\TcpSocket;

$builder = ProcessBuilder::fork(__DIR__ . '/assets/fork.php');

var_dump('START');

$process = $builder->start('foo', 'bar');
$ipc = $process->getIpc();

$ipc->write('Hello');
var_dump($ipc->read());

list ($a, $b) = TcpSocket::pair();

$a->export($ipc);
$a->close();

$ini = '';

while (null !== ($chunk = $b->read())) {
	$ini .= $chunk;
}

var_dump($process->join());
var_dump($ini == (php_ini_loaded_file() ?: 'NO INI FILE'));

--EXPECT--
string(5) "START"
string(3) "foo"
string(3) "bar"
bool(true)
string(5) "Hello"
string(5) "World"
int(0)
bool(true)
