--TEST--
TCP server cancel accept.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Context;
use Concurrent\Task;

$t = Task::asyncWithContext(Context::current()->withTimeout(500), function () {
    $server = TcpServer::listen('127.0.0.1', 0);
    
    var_dump('LISTENING...');
    
    try {
        return $server->accept();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
        
        throw $e;
    }
});

try {
    Task::await($t);
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(12) "LISTENING..."
string(17) "Context timed out"
string(17) "Context timed out"
