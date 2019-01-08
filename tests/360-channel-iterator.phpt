--TEST--
Channel iterator API.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel(2);

Task::async(function () use ($channel) {
    try {
        for ($i = 0; $i < 4; $i++) {
            $channel->send('X');
        }
    } finally {
        $channel->close();
    }
});

(new Timer(20))->awaitTimeout();

$it = $channel->getIterator();
var_dump($it->valid());

$it->rewind();
var_dump($it->valid(), $it->key(), $it->current());

$it->next();
var_dump($it->valid(), $it->key(), $it->current());

$it->next();
var_dump($it->valid(), $it->key(), $it->current());

$it->next();
var_dump($it->valid(), $it->key(), $it->current());

$it->next();
var_dump($it->valid());

--EXPECT--
bool(false)
bool(true)
int(0)
string(1) "X"
bool(true)
int(1)
string(1) "X"
bool(true)
int(2)
string(1) "X"
bool(true)
int(3)
string(1) "X"
bool(false)
