--TEST--
TCP SSL slow receiver.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;
use Concurrent\Timer;

require_once __DIR__ . '/ssl.inc';

list ($a, $b) = sslPair();

Task::async(function () use ($a) {
    try {
        $timer = new Timer(10);
        $len = 0;
    
        while (null !== ($chunk = $a->read())) {
            $timer->awaitTimeout();
            $len += strlen($chunk);
        }
        
        var_dump($len);
    } finally {
        $a->close();
    }
});

try {
    $chunk = str_repeat('A', 7000);

    for ($i = 0; $i < 1000; $i++) {
        $b->write($chunk);
    }
} finally {
    $b->close();
}

--EXPECT--
int(7000000)
