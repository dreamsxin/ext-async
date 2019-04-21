--TEST--
Pipe server can be closed during pending accept.
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

$server = PipeServer::bind($url);

Task::async(function () use ($server) {
    (new Timer(50))->awaitTimeout();
    
    $server->close();
});

try {
    $server->accept();
} catch (SocketException $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(22) "Server has been closed"
