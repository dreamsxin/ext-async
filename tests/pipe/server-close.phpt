--TEST--
Pipe server cannot accept after close.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;
use Concurrent\Timer;

if (DIRECTORY_SEPARATOR == '\\') {
    $url = sprintf('\\\\.\\pipe\\%s.sock', bin2hex(random_bytes(16)));
} else {
    $url = sprintf('%s/%s.sock', sys_get_temp_dir(), bin2hex(random_bytes(16)));
    
    if (is_file($url)) {
    	unlink($file);
    }
}

$server = PipeServer::listen($url);

Task::async(function () use ($server) {
    return Pipe::connect($server->getAddress());
});

(new Timer(100))->awaitTimeout();

$server->close(new \Error('FOO'));

try {
    $server->accept();
} catch (SocketException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
string(22) "Server has been closed"
string(3) "FOO"
