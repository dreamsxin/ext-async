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
$group = new ChannelGroup([$channel], 0);

var_dump($channel->isClosed());

var_dump($group->send('X'));
var_dump($group->send('X'));
var_dump($group->send('X'));

$channel->close();

var_dump($channel->isClosed());

foreach ($channel as $v) {
    var_dump($v);
}

var_dump($group->select());

--EXPECT--
bool(false)
int(0)
int(0)
NULL
bool(true)
string(1) "X"
string(1) "X"
NULL
