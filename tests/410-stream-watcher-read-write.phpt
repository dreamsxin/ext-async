--TEST--
Stream watcher read & write combination.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

function pair()
{
    return array_map(function ($s) {
        stream_set_blocking($s, false);
        stream_set_read_buffer($s, 0);
        stream_set_write_buffer($s, 0);
        
        return $s;
    }, stream_socket_pair((DIRECTORY_SEPARATOR == '\\') ? STREAM_PF_INET : STREAM_PF_UNIX, STREAM_SOCK_STREAM, STREAM_IPPROTO_IP));
}

list ($a, $b) = pair();

$w1 = new StreamWatcher($a);
$w2 = new StreamWatcher($b);

Task::async(function () use ($w2) {
    $w2->awaitWritable();
});

$ctx = Context::current();

Task::asyncWithContext($ctx->background(), function () use ($a, $w1, $ctx) {
    $ctx->run(function () {
        (new Timer(10))->awaitTimeout();
    });
    
    $w1->awaitWritable();
    fwrite($a, 'Hello');
    
    $w1->awaitReadable();
    var_dump(fread($a, 8192));
    
    $ctx->run(function () {
        (new Timer(10))->awaitTimeout();
    });
    
    fclose($a);
});

$w2->awaitReadable();
var_dump(fread($b, 8192));

$w2->awaitWritable();
fwrite($b, 'World');

$w2->awaitReadable();

var_dump('DONE');

--EXPECT--
string(5) "Hello"
string(5) "World"
string(4) "DONE"
