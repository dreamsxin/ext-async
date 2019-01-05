--TEST--
Channel single producers multiple consumer
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$consumer = function (string $label, iterable $it) {
    foreach ($it as $v) {
        var_dump($label, $v);
    }
};

$channel = new Channel();

Task::async($consumer, 'A', $channel);
Task::async($consumer, 'B', $channel);

$timer = new Timer(10);

for ($i = 0; $i < 8; $i++) {
    $timer->awaitTimeout();
    
    $channel->send($i);
}

--EXPECT--
string(1) "A"
int(0)
string(1) "B"
int(1)
string(1) "A"
int(2)
string(1) "B"
int(3)
string(1) "A"
int(4)
string(1) "B"
int(5)
string(1) "A"
int(6)
string(1) "B"
int(7)
