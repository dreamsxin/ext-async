--TEST--
Channel group can perform non-blocking sends.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    $channel = new Channel(2)    
]);

var_dump($group->send('A', 0));
var_dump($group->send('B', 0));
var_dump($group->send('C', 0));

$channel->close();

foreach ($channel as $v) {
    var_dump($v);
}

--EXPECT--
int(0)
int(0)
NULL
string(1) "A"
string(1) "B"
