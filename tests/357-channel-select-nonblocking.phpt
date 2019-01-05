--TEST--
Channel select will not block if not requested.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    $channel = new Channel(4)
], 0);

var_dump($group->select());

$channel->send('A');
$channel->send('B');
$channel->close();

var_dump(count($group));
$v = null;

var_dump($group->select($v));
var_dump($v);

var_dump($group->select($v));
var_dump($v);

var_dump($group->select($v));
var_dump($v);

var_dump($group->count());

--EXPECT--
NULL
int(1)
int(0)
string(1) "A"
int(0)
string(1) "B"
NULL
NULL
int(0)
