--TEST--
Poll read & write combination.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
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

$p1 = new Poll($a);
$p2 = new Poll($b);

Task::async(function () use ($p2) {
    $p2->awaitWritable();
});

$ctx = Context::current();

Task::asyncWithContext(Context::background(), function () use ($a, $p1, $ctx) {
    $ctx->run(function () {
        (new Timer(10))->awaitTimeout();
    });
    
    $p1->awaitWritable();
    fwrite($a, 'Hello');
    
    $p1->awaitReadable();
    var_dump(fread($a, 8192));
    
    $ctx->run(function () {
        (new Timer(10))->awaitTimeout();
    });
    
    fclose($a);
});

$p2->awaitReadable();
var_dump(fread($b, 8192));

$p2->awaitWritable();
fwrite($b, 'World');

$p2->awaitReadable();

var_dump('DONE');

--EXPECT--
string(5) "Hello"
string(5) "World"
string(4) "DONE"
