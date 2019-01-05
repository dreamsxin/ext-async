--TEST--
Channel select will respect timeout.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    3 => $channel = new Channel()
], 50);

Task::async(function () use ($channel) {
    (new Timer(80))->awaitTimeout();
    
    $channel->send('DONE');
    $channel->close();
});

var_dump($group->select());

$v = null;
var_dump($group->select($v));
var_dump($v);

var_dump($group->select($v));
var_dump($v);

--EXPECT--
NULL
int(3)
string(4) "DONE"
NULL
NULL
