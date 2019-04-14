--TEST--
Pipe can connect to server.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

require_once dirname(__DIR__) . '/assets/functions.php';

use Concurrent\Task;

if (DIRECTORY_SEPARATOR == '\\') {
    $url = sprintf('\\\\.\\pipe\\%s.sock', bin2hex(random_bytes(16)));
} else {
    $url = sprintf('%s/%s.sock', sys_get_temp_dir(), bin2hex(random_bytes(16)));
    
    if (is_file($url)) {
    	unlink($file);
    }
}

$server = PipeServer::listen($url);

var_dump($server->getAddress() == $url);
var_dump($server->getPort());
var_dump($server->setOption(7, 'foo'));

try {
    $t1 = Task::async(function () use ($url) {
        return Pipe::connect($url);
    });

    $t2 = Task::async(function () use ($server) {
        return $server->accept();
    });
    
    list ($a, $b) = Task::await(\Concurrent\all([$t1, $t2]));
} finally {
    $server->close();
}

var_dump($a->getAddress() == $url);
var_dump($a->getAddress() == $a->getRemoteAddress());
var_dump($a->getPort());

var_dump($b->getAddress() == $url);
var_dump($b->getAddress() == $b->getRemoteAddress());
var_dump($b->getPort());

Task::async(function () use ($a) {
    try {
        var_dump($a->read());
        $a->writeAsync('World!');
        $a->flush();
    } finally {
        $a->close();
    }
});

try {
    $b->write('Hello');
    
    while (null !== ($chunk = $b->read())) {
        var_dump($chunk);
    }
} finally {
    $b->close();
}

--EXPECT--
bool(true)
NULL
bool(false)
bool(true)
bool(true)
NULL
bool(true)
bool(true)
NULL
string(5) "Hello"
string(6) "World!"
