--TEST--
Channel select will not block if not requested.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel(4);

$group = new ChannelGroup([
    $channel->getIterator()
]);

var_dump($group->select(0));

$channel->send('A');
$channel->send('B');
$channel->close();

var_dump(count($group));

$val = $group->select(0);
var_dump($val->key);
var_dump($val->value);

$val = $group->select(0);
var_dump($val->key);
var_dump($val->value);

var_dump($group->select(0));
var_dump($group->count());

--EXPECT--
NULL
int(1)
int(0)
string(1) "A"
int(0)
string(1) "B"
NULL
int(0)
