--TEST--
Channel buffer and close interaction
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel(2);

var_dump($channel->isClosed());
var_dump($channel->isReadyForReceive());
var_dump($channel->isReadyForSend());

while ($channel->isReadyForSend()) {
    $channel->send('X');
}

$channel->close();

var_dump($channel->isClosed());
var_dump($channel->isReadyForSend());
var_dump($channel->isReadyForReceive());

foreach ($channel as $v) {
    var_dump($v);
}

var_dump($channel->isReadyForReceive());

--EXPECT--
bool(false)
bool(false)
bool(true)
bool(true)
bool(false)
bool(true)
string(1) "X"
string(1) "X"
bool(false)
